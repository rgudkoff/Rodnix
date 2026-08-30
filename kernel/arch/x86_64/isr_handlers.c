/**
 * @file isr_handlers.c
 * @brief ISR and IRQ handler implementations
 * 
 * This implementation follows the approach to interrupt handling:
 * - Single unified interrupt handler entry point
 * - Proper IRQ routing and EOI handling
 * - Silent handling of spurious interrupts
 * - No panic on unhandled IRQ (mask instead)
 */

#include "../../../include/console.h"
#include "../../core/hygiene.h"

#include "../../../include/debug.h"
#include "../../../include/error.h"
#include "../../core/interrupts.h"
#include "../../../sched/scheduler.h"
#include "../../syscall.h"
#include "../../../trace/tracev2.h"
#include "../../linux/linux_compat.h"
#include "../../core/task.h"
#include "../../../mm/vm_fault.h"
#include "../../unix/unix_layer.h"
#include "interrupt_frame.h"
#include "types.h"
#include "config.h"
#include "pic.h"
#include "../pmm.h"
#include "apic.h"
#include "lapic_regs.h"
#include "vectors.h"
#include "irqstat.h"
#include "../../core/giant.h"
#include "percpu.h"
#include "syscall_fast.h"
#include <stddef.h>


/* Minimal serial output for exception diagnostics (COM1). */
#define SERIAL_COM1_BASE 0x3F8
static inline void serial_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %%al, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t serial_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_write_char(char c)
{
    /* Wait for THR empty (bit 5) */
    for (int i = 0; i < 10000; i++) {
        if (serial_inb(SERIAL_COM1_BASE + 5) & 0x20) {
            break;
        }
    }
    serial_outb(SERIAL_COM1_BASE + 0, (uint8_t)c);
}

static void serial_write_str(const char* s)
{
    while (*s) {
        if (*s == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*s++);
    }
}

static void serial_write_hex64(uint64_t value)
{
    const char* hex = "0123456789abcdef";
    serial_write_str("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

/* Forward declaration */
extern interrupt_handler_t interrupt_handlers[256];
/* The entry stub captures the iretq frame it was about to return through into
 * this CPU's percpu slot; these read it back for the exception dump. */
#define iret_rsp    (percpu_self()->iret_rsp)
#define iret_rip    (percpu_self()->iret_rip)
#define iret_cs     (percpu_self()->iret_cs)
#define iret_rflags (percpu_self()->iret_rflags)

static void irq_send_eoi(uint32_t irq)
{
    /* EOI logic:
     * - If I/O APIC is available: use only LAPIC EOI (I/O APIC routes to LAPIC)
     * - If LAPIC is available but I/O APIC not: use both PIC and LAPIC EOI
     *   (PIC routes interrupt, but LAPIC is active, so need both)
     * - If no APIC: use only PIC EOI
     */
    extern bool ioapic_is_available(void);
    if (apic_is_available()) {
        if (ioapic_is_available()) {
            /* I/O APIC available - use only LAPIC EOI */
            apic_send_eoi();
        } else {
            /* LAPIC available but I/O APIC not - use both PIC and LAPIC EOI */
            pic_send_eoi(irq);
            apic_send_eoi();
        }
    } else {
        /* No APIC - use only PIC EOI */
        pic_send_eoi(irq);
    }
}

/*
 * How many firings with nobody to take them before a line is shut off.
 *
 * The old policy shut it off on the first one, silently. That loses the line
 * for good to a single stray during init -- a device asserting before its
 * driver registers -- and leaves nothing to explain why the device later
 * appears dead.
 *
 * Consecutive is what makes the threshold safe to set low: a handler running
 * even once resets the count, so a slow or intermittent source never
 * approaches it, while a line stuck asserted re-enters here as fast as the
 * processor can acknowledge and crosses it in milliseconds. Something has to
 * shut that off, or the machine makes no further progress.
 */
#define UNHANDLED_MASK_THRESHOLD 10000u

/* Lines this code masked, so a driver registering later can have them back
 * -- masking is a defence against a runaway, not a verdict on the device. */
static bool g_masked_unhandled[256];

void interrupt_unmask_if_we_masked(uint32_t vector);

static void interrupt_note_unhandled(uint32_t vector)
{
    if (vector >= 256u || g_masked_unhandled[vector]) {
        return;
    }
    if (irqstat_unhandled_streak(vector) < UNHANDLED_MASK_THRESHOLD) {
        return;
    }

    /* Only the legacy range maps to a line this kernel can mask. An MSI
     * vector is masked at the device, which needs the driver that is by
     * definition absent here -- so say so rather than pretend. */
    if (vector < 48u) {
        uint32_t irq = vector - 32u;
        extern bool ioapic_is_available(void);
        if (apic_is_available() && ioapic_is_available()) {
            apic_disable_irq((uint8_t)irq);
        } else {
            pic_disable_irq((uint8_t)irq);
        }
        g_masked_unhandled[vector] = true;
        kprintf("[IRQ] vector 0x%x (IRQ %u) fired %u times with no handler; "
                "line masked\n",
                (unsigned)vector, (unsigned)irq,
                (unsigned)UNHANDLED_MASK_THRESHOLD);
    } else {
        /* Latch so this reports once rather than every firing. */
        g_masked_unhandled[vector] = true;
        kprintf("[IRQ] vector 0x%x fired %u times with no handler and cannot "
                "be masked here; it will keep firing\n",
                (unsigned)vector, (unsigned)UNHANDLED_MASK_THRESHOLD);
    }
}

void interrupt_unmask_if_we_masked(uint32_t vector)
{
    if (vector >= 256u || !g_masked_unhandled[vector]) {
        return;
    }
    g_masked_unhandled[vector] = false;

    if (vector < 48u) {
        uint32_t irq = vector - 32u;
        extern bool ioapic_is_available(void);
        if (apic_is_available() && ioapic_is_available()) {
            apic_enable_irq((uint8_t)irq);
        } else {
            pic_enable_irq((uint8_t)irq);
        }
        kprintf("[IRQ] vector 0x%x (IRQ %u) unmasked: a handler registered\n",
                (unsigned)vector, (unsigned)irq);
    }
}

static inline bool interrupt_is_tick(uint32_t vector)
{
    return vector == VECTOR_LAPIC_TIMER || vector == VECTOR_PIT_TIMER;
}

static void interrupt_send_eoi(uint32_t vector)
{
    /* The spurious vector sets no ISR bit, so there is nothing to retire.
     * An EOI here would retire whatever genuinely was in service instead
     * (docs/ru/irq_audit.md, F10). */
    if (vector == VECTOR_SPURIOUS) {
        return;
    }
    if (vector >= 32 && vector < 48) {
        irq_send_eoi(vector - 32);
        return;
    }
    if (apic_is_available()) {
        apic_send_eoi();
    }
}

/* Safe VGA output function - does not use kputs/kprintf to avoid recursion */
static void safe_vga_puts(uint8_t row, uint8_t col, const char* str, uint8_t color)
{
    volatile uint16_t* vga = (volatile uint16_t*)X86_64_PHYS_TO_VIRT(0xB8000);
    uint8_t r = row;
    uint8_t c = col;
    
    while (*str && r < 25) {
        if (*str == '\n') {
            c = 0;
            r++;
        } else if (*str != '\r') {
            uint32_t idx = r * 80 + c;
            if (idx < 80 * 25) {
                vga[idx] = (uint16_t)*str | ((uint16_t)color << 8);
            }
            c++;
            if (c >= 80) {
                c = 0;
                r++;
            }
        }
        str++;
    }
}

/* Safe hex output - direct VGA write */
static void safe_vga_hex(uint8_t row, uint8_t col, uint64_t value, uint8_t color)
{
    volatile uint16_t* vga = (volatile uint16_t*)X86_64_PHYS_TO_VIRT(0xB8000);
    char hex_chars[] = "0123456789ABCDEF";
    uint8_t r = row;
    uint8_t c = col;
    
    /* Print "0x" prefix */
    if (r < 25 && c < 80) {
        uint32_t idx = r * 80 + c;
        vga[idx] = (uint16_t)'0' | ((uint16_t)color << 8);
        c++;
    }
    if (r < 25 && c < 80) {
        uint32_t idx = r * 80 + c;
        vga[idx] = (uint16_t)'x' | ((uint16_t)color << 8);
        c++;
    }
    
    /* Skip leading zeros, but always print at least one digit */
    int start = 60;
    while (start > 0 && ((value >> start) & 0xF) == 0) {
        start -= 4;
    }
    
    /* Print hex digits */
    for (int i = start; i >= 0; i -= 4) {
        if (r >= 25) break; /* Screen limit */
        if (c >= 80) {
            c = 0;
            r++;
            if (r >= 25) break;
        }
        uint8_t digit = (value >> i) & 0xF;
        uint32_t idx = r * 80 + c;
        vga[idx] = (uint16_t)hex_chars[digit] | ((uint16_t)color << 8);
        c++;
    }
}

static interrupt_frame_t* handle_syscall(interrupt_frame_t* regs)
{
    (void)x86_64_syscall_dispatch_frame(regs, 0);
    return regs;
}

/* Exception names */
static const char* exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt", // 15 - Reserved
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception", // 21 - Reserved
    "Reserved", // 22
    "Reserved", // 23
    "Reserved", // 24
    "Reserved", // 25
    "Reserved", // 26
    "Reserved", // 27
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved" // 31
};

/**
 * @function interrupt_dispatch
 * @brief Unified interrupt dispatcher
 * 
 * This is the main interrupt handler that routes interrupts to their
 * appropriate handlers. It handles exceptions (0-31) and hardware interrupts
 * (32-255, excluding the syscall gate on 128).
 * 
 * @param regs Pointer to saved CPU registers
 * 
 * @note Single entry point, proper routing, silent spurious handling
 */
static interrupt_frame_t* interrupt_dispatch(interrupt_frame_t* regs)
{
    uint32_t vector = regs->int_no;

    if (vector == SYSCALL_VECTOR) {
        return handle_syscall(regs);
    }
    
    /*
     * Reschedule trap. Deliberately short: no handler, and above all no EOI.
     * It is a software interrupt, so nothing is in service, and an EOI here
     * would retire whatever genuinely was.
     */
    if (vector == VECTOR_RESCHED) {
        /* Counted like any other entry: its rate is how often the kernel
         * switches voluntarily, which is worth being able to see next to the
         * tick rate rather than inferring. */
        irqstat_count(percpu_index(), vector, true);
        /* The vector means "switch now", so the request is implicit in
         * having been raised; callers need not set the flag themselves. */
        percpu_self()->sched_resched_pending = true;
        return scheduler_switch_from_irq(regs);
    }

    /* Handle hardware interrupts (32-255), except the syscall gate on 128. */
    if (vector >= 32 && vector != SYSCALL_VECTOR) {
        irqstat_count(percpu_index(), vector, interrupt_handlers[vector] != NULL);

        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = 0;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_IRQ;
            ctx.arch_specific = (void*)regs;
            interrupt_handlers[vector](&ctx);
        } else {
            interrupt_note_unhandled(vector);
        }

        interrupt_send_eoi(vector);
        /* Preemption follows from the source being a tick, not from a
         * particular number. There are two tick vectors -- the LAPIC timer
         * and the PIT it falls back to -- and asking "is this 32?" quietly
         * stopped being the same question (docs/ru/irq_audit.md, F7). */
        if (interrupt_is_tick(vector)) {
            percpu_irq_selftest();
            hygiene_preempt_tick_probe(regs->rip,
                                       percpu_self()->preempt_count);
            scheduler_tick();
            regs = scheduler_switch_from_irq(regs);
        }
        return regs;
    }
    
    /* Handle exception (0-31) */
    if (vector < 32) {
        if (vector == 14) {
            uint64_t cr2 = 0;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            task_t* task = task_get_current();
            /* A fault in thread context runs kernel code, so it runs under
             * the kernel-wide lock like any other kernel entry. Recursive,
             * so a fault taken by a thread that already holds it is fine. */
            giant_lock();
            int frc = vm_fault_handle(task, cr2, regs->err_code, regs->rip);
            giant_unlock();
            if (frc == RDNX_OK) {
                return regs;
            }
            if (task && (regs->cs & 3) == 3 && task->parent_task_id != 0) {
                /* init и задачи ядра не убиваются отсюда: их неразрешимый
                 * отказ — смерть системы, и честнее упасть в панику ниже,
                 * чем тихо снять единицу, без которой ничего не живёт. */
                /* Неразрешимый отказ пользовательского режима — смерть
                 * процесса, не машины. Штатный случай с этапа 8: отказ
                 * возвращает E_NOMEM, когда памяти нет и лучшая жертва
                 * убийцы — сам проситель. До ближайшего переключения
                 * инструкция может отказать повторно — терминация
                 * идемпотентна, а окно ограничено тиком. */
                kprintf("[FAULT] task=%llu killed: unresolvable user fault "
                        "va=%llx err=%llx rc=%d free=%llu\n",
                        (unsigned long long)task->task_id,
                        (unsigned long long)cr2,
                        (unsigned long long)regs->err_code, frc,
                        (unsigned long long)pmm_free_pages_count());
                /* Умереть самому, безусловно. Просьба к force-пути здесь
                 * тихо ничего не делала для задачи с уже выставленным
                 * exited (жертва OOM после истечения окна) — и инструкция
                 * перезапускалась в вечный цикл отказов без единого
                 * сисколла: чекпойнт был недостижим, поток неубиваем. Мы —
                 * и есть тот поток; штатный выход отдаёт всё, что держит. */
                giant_lock();
                unix_proc_exit(128u + 9u /* SIGKILL */);
                /* не возвращается; страховка на случай возврата: */
                giant_unlock();
                return scheduler_switch_from_irq(regs);
            }
        }
        tracev2_emit(TR2_CAT_FAULT, TR2_EV_FAULT_EXCEPTION, vector, regs->err_code);
        /* Call registered handler if available */
        if (interrupt_handlers[vector]) {
            interrupt_context_t ctx;
            ctx.pc = regs->rip;
            ctx.sp = 0;
            ctx.flags = regs->rflags;
            ctx.error_code = regs->err_code;
            ctx.vector = vector;
            ctx.type = INTERRUPT_TYPE_EXCEPTION;
            ctx.arch_specific = regs;
            
            interrupt_handlers[vector](&ctx);
            return regs;
        }
        
        /* Reserved exceptions (15, 22-31) - ignore silently */
        /* Exception 21 (Control Protection Exception) - can occur on some CPUs, ignore */
        if (vector == 15 || vector == 21 || (vector >= 22 && vector <= 31)) {
            /* These are reserved or can occur spuriously and should not cause panic */
            return regs;
        }
        
        /* Serial exception dump for boot.log */
        serial_write_str("\n[EXC] iret rsp=");
        serial_write_hex64(iret_rsp);
        serial_write_str(" rip=");
        serial_write_hex64(iret_rip);
        serial_write_str(" cs=");
        serial_write_hex64(iret_cs);
        serial_write_str(" rflags=");
        serial_write_hex64(iret_rflags);
        serial_write_str("\n");
        if (iret_rsp) {
            uint64_t* p = (uint64_t*)(uintptr_t)iret_rsp;
            serial_write_str("[EXC] iretq stack rip/cs/rflags=");
            serial_write_hex64(p[0]);
            serial_write_str(" ");
            serial_write_hex64(p[1]);
            serial_write_str(" ");
            serial_write_hex64(p[2]);
            serial_write_str("\n");
            serial_write_str("[EXC] iretq stack window=");
            serial_write_hex64(p[-2]);
            serial_write_str(" ");
            serial_write_hex64(p[-1]);
            serial_write_str(" ");
            serial_write_hex64(p[0]);
            serial_write_str(" ");
            serial_write_hex64(p[1]);
            serial_write_str(" ");
            serial_write_hex64(p[2]);
            serial_write_str(" ");
            serial_write_hex64(p[3]);
            serial_write_str(" ");
            serial_write_hex64(p[4]);
            serial_write_str("\n");
        }

        serial_write_str("\n[EXC] v=");
        serial_write_hex64(vector);
        serial_write_str(" rip=");
        serial_write_hex64(regs->rip);
        serial_write_str(" err=");
        serial_write_hex64(regs->err_code);
        serial_write_str("\n");
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            tracev2_emit(TR2_CAT_FAULT, TR2_EV_FAULT_PAGE, cr2, regs->rip);
            serial_write_str("[EXC] cr2=");
            serial_write_hex64(cr2);
            serial_write_str("\n");
            task_t* pf_task = task_get_current();
            if (pf_task && task_get_abi(pf_task) == TASK_ABI_LINUX) {
                uint64_t fs_base = 0;
                uint32_t lo = 0;
                uint32_t hi = 0;
                __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100u));
                fs_base = ((uint64_t)hi << 32) | lo;
                serial_write_str("[EXC] linux tls task=");
                serial_write_hex64(pf_task->tls_fs_base);
                serial_write_str(" hw=");
                serial_write_hex64(fs_base);
                serial_write_str(" rsp=");
                serial_write_hex64(regs->rsp);
                serial_write_str("\n");
            }
        }
        if (vector == 13) {
            struct {
                uint16_t limit;
                uint64_t base;
            } __attribute__((packed)) gdtr;
            __asm__ volatile ("sgdt %0" : "=m"(gdtr));
            serial_write_str("[EXC] gdtr base=");
            serial_write_hex64(gdtr.base);
            serial_write_str(" limit=");
            serial_write_hex64(gdtr.limit);
            serial_write_str("\n");
            if (gdtr.base) {
                uint64_t* gdt = (uint64_t*)(uintptr_t)gdtr.base;
                serial_write_str("[EXC] gdt[1]=");
                serial_write_hex64(gdt[1]);
                serial_write_str(" gdt[2]=");
                serial_write_hex64(gdt[2]);
                serial_write_str("\n");
            }
        }

        /* Extra minimal dump at top-left to avoid being scrolled out */
        safe_vga_puts(0, 0, "EXC v=", 0x0C);
        safe_vga_hex(0, 6, vector, 0x0C);
        safe_vga_puts(1, 0, "RIP=", 0x0C);
        safe_vga_hex(1, 4, regs->rip, 0x0C);
        safe_vga_puts(2, 0, "ERR=", 0x0C);
        safe_vga_hex(2, 4, regs->err_code, 0x0C);

        /* Use safe VGA output to avoid recursion - do NOT call kputs/kprintf */
        safe_vga_puts(16, 0, "*** Exception ***", 0x0C); /* Red */
        safe_vga_puts(17, 0, "Exception: ", 0x0F); /* White */
        if (exception_names[vector]) {
            safe_vga_puts(17, 11, exception_names[vector], 0x0F);
        } else {
            safe_vga_puts(17, 11, "Unknown", 0x0F);
        }
        safe_vga_hex(17, 40, vector, 0x0F);
        
        safe_vga_puts(18, 0, "Error Code: ", 0x0F);
        safe_vga_hex(18, 12, regs->err_code, 0x0F);
        
        safe_vga_puts(19, 0, "RIP: ", 0x0F);
        safe_vga_hex(19, 5, regs->rip, 0x0F);
        
        safe_vga_puts(20, 0, "RSP: ", 0x0F);
        safe_vga_puts(20, 5, "n/a", 0x0F);
        
        safe_vga_puts(21, 0, "RFLAGS: ", 0x0F);
        safe_vga_hex(21, 8, regs->rflags, 0x0F);
        
        /* Page fault special handling */
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            safe_vga_puts(22, 0, "CR2: ", 0x0F);
            safe_vga_hex(22, 5, cr2, 0x0F);
        }
        
        safe_vga_puts(23, 0, "*** KERNEL PANIC ***", 0x0C); /* Red */
        safe_vga_puts(24, 0, "Message: Unhandled exception", 0x0C); /* Red */
        
        /* Halt system */
        __asm__ volatile ("cli; hlt");
        for (;;) {
            __asm__ volatile ("hlt");
        }
        return regs;
    }
    
    /* Unknown vector outside exception/syscall/hardware ranges - ignore silently */
    return regs;
}

/* The one entry point from assembly, for every vector. */
interrupt_frame_t* interrupt_entry(interrupt_frame_t* regs)
{
    /*
     * The gate cleared IF, so a handler's running time is time this processor
     * is unavailable to everyone else -- the same latency window as a masked
     * region, just entered by hardware. Measured here because this is the one
     * place every vector passes through.
     *
     * The syscall gate is left out on purpose: the fast SYSCALL path does not
     * come through here, so counting only the int 0x80 half would produce a
     * number for "syscall duration" that is true of some syscalls and not
     * others. Syscall latency wants its own measurement at its own two
     * entries, and is currently unmeasured.
     */
    uint32_t vector = regs->int_no;
    bool measured = hygiene_enabled() && vector != SYSCALL_VECTOR;

    if (measured) {
        hygiene_irq_enter(vector);
    }
    interrupt_frame_t* out = interrupt_dispatch(regs);
    if (measured) {
        hygiene_irq_exit(vector);
    }
    return out;
}
