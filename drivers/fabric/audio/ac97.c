/**
 * @file ac97.c
 * @brief AC'97 (Intel 82801AA) — звуковой тракт воспроизведения.
 *
 * Демонстрация тезиса ОС: путь от пользовательского потока реального
 * времени до ЦАП без единой точки неограниченного ожидания.
 *
 * Форма тракта — кольцо без копирования:
 *
 *   - 32 дескриптора BDL смотрят в один непрерывный физический буфер
 *     (32 периода по 512 кадров s16le stereo = ровно 64 КиБ);
 *   - кольцо и страница состояния отображаются писателю в userland;
 *     писатель кладёт сэмплы и публикует свой индекс записи в странице
 *     состояния — ни одного сисколла в устоявшемся режиме;
 *   - обработчик прерывания короткий по построению: подтвердить статус,
 *     продвинуть счётчик, подтянуть LVI под индекс писателя;
 *   - underrun — не догадка, а аппаратный факт: LVBCI, «последний
 *     действительный буфер доигран». Считается и виден писателю.
 *
 * Страницы кольца закреплены (vm_page_wire): возврат памяти не имеет
 * права трогать то, по чему идёт DMA.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../kernel/fabric/fabric.h"
#include "../../../kernel/fabric/device/device.h"
#include "../../../kernel/fabric/driver/driver.h"
#include "../../../kernel/fabric/bus/pci.h"
#include "../../../kernel/arch/pmm.h"
#include "../../../kernel/arch/x86_64/config.h"
#include "../../../mm/vm_page.h"
#include "../../../include/audio.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../include/error.h"
#include "../../../trace/bootlog.h"

/* ---- порты: NAM (микшер, BAR0 io) ---- */
#define NAM_RESET        0x00u
#define NAM_MASTER_VOL   0x02u
#define NAM_PCM_OUT_VOL  0x18u
#define NAM_EXT_CAPS     0x28u
#define NAM_EXT_CTRL     0x2Au
#define NAM_PCM_DAC_RATE 0x2Cu

/* ---- порты: NABM (bus master, BAR1 io) ---- */
#define PO_BDBAR 0x10u   /* физадрес BDL */
#define PO_CIV   0x14u   /* текущий дескриптор (RO) */
#define PO_LVI   0x15u   /* последний действительный дескриптор */
#define PO_SR    0x16u   /* статус, биты W1C */
#define PO_PICB  0x18u   /* сэмплов осталось в текущем */
#define PO_CR    0x1Bu   /* управление */
#define GLOB_CNT 0x2Cu
#define GLOB_STA 0x30u

#define SR_DCH    0x01u  /* DMA остановлен (RO) */
#define SR_LVBCI  0x04u  /* доигран последний действительный: underrun */
#define SR_BCIS   0x08u  /* период доигран (IOC) */
#define SR_FIFOE  0x10u

#define CR_RPBM  0x01u   /* run */
#define CR_RR    0x02u   /* reset регистров бокса */
#define CR_LVBIE 0x04u
#define CR_FEIE  0x08u
#define CR_IOCE  0x10u

#define GLOB_CNT_COLD 0x02u

typedef struct __attribute__((packed)) {
    uint32_t addr;
    uint16_t samples;      /* сэмплов (u16), не байтов */
    uint16_t flags;        /* 0x8000 IOC */
} ac97_bdl_entry_t;

#define BDL_IOC 0x8000u

typedef struct {
    bool present;
    uint16_t nam;
    uint16_t nabm;
    fabric_device_t* fdev;

    uint64_t bdl_phys;
    ac97_bdl_entry_t* bdl;
    uint64_t ring_phys;
    uint64_t status_phys;
    audio_status_page_t* status;   /* ядро пишет hw-половину */
    bool running;
} ac97_dev_t;

static ac97_dev_t g_ac97;

static inline void nam_w16(ac97_dev_t* d, uint16_t reg, uint16_t v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"((uint16_t)(d->nam + reg)));
}
static inline uint16_t nam_r16(ac97_dev_t* d, uint16_t reg)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"((uint16_t)(d->nam + reg)));
    return v;
}
static inline void nabm_w8(ac97_dev_t* d, uint16_t reg, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"((uint16_t)(d->nabm + reg)));
}
static inline uint8_t nabm_r8(ac97_dev_t* d, uint16_t reg)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"((uint16_t)(d->nabm + reg)));
    return v;
}
static inline void nabm_w16(ac97_dev_t* d, uint16_t reg, uint16_t v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"((uint16_t)(d->nabm + reg)));
}
static inline uint16_t nabm_r16(ac97_dev_t* d, uint16_t reg)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"((uint16_t)(d->nabm + reg)));
    return v;
}
static inline void nabm_w32(ac97_dev_t* d, uint16_t reg, uint32_t v)
{
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"((uint16_t)(d->nabm + reg)));
}

/*
 * Подтянуть LVI под индекс писателя. Вызывается из опроса и из start.
 * Писатель публикует произведённые периоды в user_write_pos (страница
 * состояния); аппарату действителен последний ПОЛНОСТЬЮ записанный.
 */
static void ac97_advance_lvi(ac97_dev_t* d)
{
    uint64_t wr = __atomic_load_n(&d->status->user_write_pos, __ATOMIC_ACQUIRE);
    if (wr == 0) {
        return;
    }
    uint8_t lvi = (uint8_t)((wr - 1u) % AUDIO_RING_PERIODS);
    nabm_w8(d, PO_LVI, lvi);
}

/*
 * Опрос движка из softclock — каденция тика (10 мс) при периоде 10.7 мс.
 *
 * v0 сознательно без INTx: линия 11 на QEMU разделяется с e1000, а
 * level-RTE на разделяемой линии устраивает шторм с загрузки (edge — не
 * доставляется вовсе; см. сагу в коммите). Часы тракта читаются из CIV —
 * они всё равно аппаратные; underrun остаётся аппаратным фактом (LVBCI).
 * Прерывания вернутся на реальном железе вместе с MSI.
 */
static uint8_t g_last_civ;

void audio_out_poll(void)
{
    ac97_dev_t* d = &g_ac97;
    if (!d->present || !d->running) {
        return;
    }
    uint8_t civ = nabm_r8(d, PO_CIV);
    uint8_t delta = (uint8_t)((civ - g_last_civ) & (AUDIO_RING_PERIODS - 1u));
    g_last_civ = civ;
    if (delta) {
        __atomic_fetch_add(&d->status->hw_pos, delta, __ATOMIC_RELEASE);
    }

    uint16_t sr = nabm_r16(d, PO_SR);
    if (sr & SR_LVBCI) {
        __atomic_fetch_add(&d->status->underruns, 1u, __ATOMIC_RELEASE);
    }
    if (sr & (SR_BCIS | SR_LVBCI | SR_FIFOE)) {
        nabm_w16(d, PO_SR, (uint16_t)(sr & (SR_BCIS | SR_LVBCI | SR_FIFOE)));
    }

    ac97_advance_lvi(d);
    if ((nabm_r16(d, PO_SR) & SR_DCH) != 0) {
        uint64_t wr = __atomic_load_n(&d->status->user_write_pos, __ATOMIC_ACQUIRE);
        if (wr > __atomic_load_n(&d->status->hw_pos, __ATOMIC_ACQUIRE)) {
            /* Встал на underrun'е, а писатель уже впереди: продолжить. */
            nabm_w8(d, PO_CR, CR_RPBM);
        }
    }
}

/* ---- сервис для сисколл-слоя ---- */

int audio_out_open(audio_out_info_t* out)
{
    ac97_dev_t* d = &g_ac97;
    if (!d->present) {
        return RDNX_E_NOTFOUND;
    }
    out->ring_phys = d->ring_phys;
    out->status_phys = d->status_phys;
    out->ring_bytes = AUDIO_RING_BYTES;
    out->period_frames = AUDIO_PERIOD_FRAMES;
    out->periods = AUDIO_RING_PERIODS;
    out->rate = AUDIO_RATE;
    return RDNX_OK;
}

int audio_out_start(void)
{
    ac97_dev_t* d = &g_ac97;
    if (!d->present) {
        return RDNX_E_NOTFOUND;
    }
    if (d->running) {
        return RDNX_OK;
    }
    /* Сброс бокса, свежий BDL, LVI от писателя, поехали. */
    nabm_w8(d, PO_CR, CR_RR);
    for (int i = 0; i < 1000 && (nabm_r8(d, PO_CR) & CR_RR); i++) {
    }
    nabm_w32(d, PO_BDBAR, (uint32_t)d->bdl_phys);
    __atomic_store_n(&d->status->hw_pos, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&d->status->underruns, 0u, __ATOMIC_RELEASE);
    ac97_advance_lvi(d);
    g_last_civ = 0;
    d->running = true;
    __atomic_store_n(&d->status->running, 1u, __ATOMIC_RELEASE);
    nabm_w8(d, PO_CR, CR_RPBM);
    return RDNX_OK;
}

int audio_out_stop(void)
{
    ac97_dev_t* d = &g_ac97;
    if (!d->present) {
        return RDNX_E_NOTFOUND;
    }
    klog_dbg("ac97", "stop: civ=%u lvi=%u sr=%x picb=%u cr=%x glob=%x hw=%llu wr=%llu\n",
            (unsigned)nabm_r8(d, PO_CIV), (unsigned)nabm_r8(d, PO_LVI),
            (unsigned)nabm_r16(d, PO_SR), (unsigned)nabm_r16(d, PO_PICB),
            (unsigned)nabm_r8(d, PO_CR),
            (unsigned)nabm_r16(d, GLOB_STA),
            (unsigned long long)d->status->hw_pos,
            (unsigned long long)d->status->user_write_pos);
    d->running = false;
    __atomic_store_n(&d->status->running, 0u, __ATOMIC_RELEASE);
    nabm_w8(d, PO_CR, 0);
    return RDNX_OK;
}

int audio_out_stats(audio_out_stats_t* st)
{
    ac97_dev_t* d = &g_ac97;
    if (!d->present) {
        return RDNX_E_NOTFOUND;
    }
    st->hw_pos = __atomic_load_n(&d->status->hw_pos, __ATOMIC_ACQUIRE);
    st->underruns = __atomic_load_n(&d->status->underruns, __ATOMIC_ACQUIRE);
    st->running = d->running ? 1u : 0u;
    return RDNX_OK;
}

/* ---- fabric ---- */

#define PCI_CLASS_MULTIMEDIA 0x04u

static bool ac97_probe(fabric_device_t* dev)
{
    return dev && dev->class_code == PCI_CLASS_MULTIMEDIA &&
           dev->vendor_id == 0x8086u && dev->device_id == 0x2415u;
}

static int ac97_match_score(fabric_device_t* dev)
{
    if (ac97_probe(dev)) {
        return FABRIC_MATCH_DEVICE_EXACT;
    }
    if (dev && dev->class_code == PCI_CLASS_MULTIMEDIA) {
        return FABRIC_MATCH_BUS_EXACT;
    }
    return FABRIC_MATCH_NONE;
}

static const fabric_property_t ac97_match_properties[] = {
    { .key = "bus", .type = FABRIC_PROP_STR, .value.str = "pci" },
    { .key = "class-code", .type = FABRIC_PROP_U32, .value.u32 = PCI_CLASS_MULTIMEDIA },
};

static int ac97_attach(fabric_device_t* dev)
{
    ac97_dev_t* d = &g_ac97;
    if (d->present) {
        return RDNX_E_BUSY;
    }
    pci_device_info_t* pci = (pci_device_info_t*)dev->bus_private;
    if (!pci) {
        return RDNX_E_INVALID;
    }
    uint32_t bar0 = pci->bars[0];
    uint32_t bar1 = pci->bars[1];
    if ((bar0 & 1u) == 0 || (bar1 & 1u) == 0) {
        klog_err("ac97", "unexpected BAR layout %x %x\n", bar0, bar1);
        return RDNX_E_INVALID;
    }
    d->nam = (uint16_t)(bar0 & ~0x3u);
    d->nabm = (uint16_t)(bar1 & ~0x3u);
    d->fdev = dev;

    /* Включить доступ к портам и bus mastering: без второго BDL-движок
     * читает дескрипторы в никуда, и тракт мёртв при живых регистрах. */
    pci_command_set(pci, 0x0001u /* IO */ | 0x0004u /* BUS MASTER */);

    /* Память тракта: BDL+status на одной странице, кольцо — 16 страниц
     * подряд. Всё закреплено: DMA и возврат памяти несовместимы. */
    uint64_t meta_phys = pmm_alloc_pages(1);
    uint64_t ring_phys = pmm_alloc_pages(AUDIO_RING_BYTES / 4096u);
    if (!meta_phys || !ring_phys) {
        klog_err("ac97", "no memory for ring\n");
        return RDNX_E_NOMEM;
    }
    for (uint64_t off = 0; off < AUDIO_RING_BYTES; off += 4096u) {
        (void)vm_page_hold(ring_phys + off);
        (void)vm_page_wire(ring_phys + off);
    }
    (void)vm_page_hold(meta_phys);
    (void)vm_page_wire(meta_phys);

    d->bdl_phys = meta_phys;
    d->bdl = (ac97_bdl_entry_t*)(void*)X86_64_PHYS_TO_VIRT(meta_phys);
    memset(d->bdl, 0, 4096);
    /* Страница состояния — вторая половина той же страницы недопустима:
     * писателю отображается страница целиком, а BDL писателю не place.
     * Отдельная страница. */
    uint64_t st_phys = pmm_alloc_pages(1);
    if (!st_phys) {
        return RDNX_E_NOMEM;
    }
    (void)vm_page_hold(st_phys);
    (void)vm_page_wire(st_phys);
    d->status_phys = st_phys;
    d->status = (audio_status_page_t*)(void*)X86_64_PHYS_TO_VIRT(st_phys);
    memset(d->status, 0, 4096);
    d->status->magic = AUDIO_STATUS_MAGIC;
    d->status->rate = AUDIO_RATE;
    d->status->period_frames = AUDIO_PERIOD_FRAMES;
    d->status->periods = AUDIO_RING_PERIODS;

    d->ring_phys = ring_phys;
    memset((void*)X86_64_PHYS_TO_VIRT(ring_phys), 0, AUDIO_RING_BYTES);
    for (uint32_t i = 0; i < AUDIO_RING_PERIODS; i++) {
        d->bdl[i].addr = (uint32_t)(ring_phys + (uint64_t)i * AUDIO_PERIOD_BYTES);
        d->bdl[i].samples = AUDIO_PERIOD_FRAMES * 2u;  /* сэмплов, стерео */
        d->bdl[i].flags = BDL_IOC;
    }

    /* Кодек: холодный сброс, размьютить, 48 кГц. */
    nabm_w32(d, GLOB_CNT, GLOB_CNT_COLD);
    nam_w16(d, NAM_RESET, 0);
    for (volatile int i = 0; i < 100000; i++) {
    }
    nam_w16(d, NAM_MASTER_VOL, 0x0000);
    nam_w16(d, NAM_PCM_OUT_VOL, 0x0808);
    nam_w16(d, NAM_EXT_CTRL, nam_r16(d, NAM_EXT_CTRL) | 1u); /* VRA */
    nam_w16(d, NAM_PCM_DAC_RATE, AUDIO_RATE);

    d->present = true;
    klog("ac97", "nam=%x nabm=%x ring=%llx %u periods x %u frames @ %u Hz\n",
            d->nam, d->nabm, (unsigned long long)ring_phys,
            AUDIO_RING_PERIODS, AUDIO_PERIOD_FRAMES, AUDIO_RATE);
    return RDNX_OK;
}

static fabric_driver_t g_driver = {
    .name = "ac97",
    .match_properties = ac97_match_properties,
    .match_property_count = 2,
    .match_priority = 25,
    .match_score = ac97_match_score,
    .probe = ac97_probe,
    .attach = ac97_attach,
};

void ac97_audio_init(void)
{
    if (fabric_driver_register(&g_driver) == RDNX_OK) {
        klog("ac97", "driver registered\n");
    }
}
