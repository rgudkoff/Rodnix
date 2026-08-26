/**
 * @file audio.h
 * @brief Контракт звукового тракта воспроизведения (v0: AC'97).
 *
 * Кольцо без копирования: писатель в userland получает отображение кольца
 * и страницы состояния, кладёт кадры s16le stereo и публикует индекс
 * записи; ядро в прерывании двигает hw_pos и считает underrun'ы. Ни одного
 * сисколла в устоявшемся режиме.
 */
#ifndef _RODNIX_AUDIO_H
#define _RODNIX_AUDIO_H

#include <stdint.h>

#define AUDIO_RATE          48000u
#define AUDIO_PERIOD_FRAMES 512u                       /* ~10.7 мс */
#define AUDIO_RING_PERIODS  32u
#define AUDIO_PERIOD_BYTES  (AUDIO_PERIOD_FRAMES * 4u) /* s16le stereo */
#define AUDIO_RING_BYTES    (AUDIO_PERIOD_BYTES * AUDIO_RING_PERIODS)

#define AUDIO_STATUS_MAGIC  0x52644155u                /* 'RdAU' */

/* Страница состояния, разделяемая с писателем. Ядро пишет hw-половину,
 * писатель — только user_write_pos (в периодах, монотонно). */
typedef struct {
    uint32_t magic;
    uint32_t rate;
    uint32_t period_frames;
    uint32_t periods;
    volatile uint32_t running;
    uint32_t pad;
    volatile uint64_t hw_pos;         /* доиграно периодов, монотонно */
    volatile uint64_t underruns;      /* аппаратные LVBCI */
    volatile uint64_t user_write_pos; /* произведено периодов (писатель) */
} audio_status_page_t;

typedef struct {
    uint64_t ring_phys;
    uint64_t status_phys;
    uint32_t ring_bytes;
    uint32_t period_frames;
    uint32_t periods;
    uint32_t rate;
} audio_out_info_t;

typedef struct {
    uint64_t hw_pos;
    uint64_t underruns;
    uint32_t running;
} audio_out_stats_t;

int audio_out_open(audio_out_info_t* out);
int audio_out_start(void);
int audio_out_stop(void);
int audio_out_stats(audio_out_stats_t* st);
/* Опрос движка; зовётся softclock'ом каждый тик. Без устройства — no-op. */
void audio_out_poll(void);

#endif /* _RODNIX_AUDIO_H */
