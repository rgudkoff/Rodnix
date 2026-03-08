/**
 * @file console.c
 * @brief Console implementation (minimal)
 */

#include "../../include/console.h"
#include <stdarg.h>

/* Simple VGA text mode implementation */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

/* Serial (COM1) */
#define SERIAL_COM1_BASE 0x3F8
#define SERIAL_DATA      0x0
#define SERIAL_IER       0x1
#define SERIAL_LCR       0x3
#define SERIAL_MCR       0x4
#define SERIAL_LSR       0x5

static bool serial_enabled = false;

/* VGA cursor control ports */
#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5
#define VGA_CURSOR_LOW  0x0F
#define VGA_CURSOR_HIGH 0x0E

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static uint8_t vga_row = 0;
static uint8_t vga_col = 0;
static uint8_t vga_color = 0x0F; /* White on black */
static volatile bool kputs_in_progress = false; /* Prevent recursive calls from exception handlers */
static bool log_prefix_enabled = true;
static bool log_at_line_start = true;
static bool log_prefix_in_progress = false;

typedef enum {
    ANSI_STATE_NORMAL = 0,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_STATE_NORMAL;
static char ansi_csi_buf[16];
static uint8_t ansi_csi_len = 0;
static bool tsc_uptime_base_set = false;
static uint64_t tsc_uptime_base = 0;
static bool rtc_uptime_base_set = false;
static uint64_t rtc_uptime_base_sec = 0;
static const char* uptime_source_name = "unknown";

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %%al, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, (uint8_t)(0x80u | (reg & 0x7Fu)));
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0Fu) + ((v >> 4) * 10u));
}

static bool rtc_read_datetime(uint32_t* year,
                              uint32_t* month,
                              uint32_t* day,
                              uint32_t* hour,
                              uint32_t* min,
                              uint32_t* sec)
{
    if (!year || !month || !day || !hour || !min || !sec) {
        return false;
    }

    uint8_t sec0, min0, hour0, day0, mon0, year0, regb0;
    uint8_t sec1, min1, hour1, day1, mon1, year1, regb1;
    for (uint32_t tries = 0; tries < 8; tries++) {
        while (cmos_read(0x0A) & 0x80u) {
            /* wait while RTC update in progress */
        }

        sec0 = cmos_read(0x00);
        min0 = cmos_read(0x02);
        hour0 = cmos_read(0x04);
        day0 = cmos_read(0x07);
        mon0 = cmos_read(0x08);
        year0 = cmos_read(0x09);
        regb0 = cmos_read(0x0B);

        while (cmos_read(0x0A) & 0x80u) {
        }
        sec1 = cmos_read(0x00);
        min1 = cmos_read(0x02);
        hour1 = cmos_read(0x04);
        day1 = cmos_read(0x07);
        mon1 = cmos_read(0x08);
        year1 = cmos_read(0x09);
        regb1 = cmos_read(0x0B);

        if (sec0 == sec1 && min0 == min1 && hour0 == hour1 &&
            day0 == day1 && mon0 == mon1 && year0 == year1 && regb0 == regb1) {
            break;
        }
        if (tries == 7) {
            return false;
        }
    }

    uint8_t s = sec1, m = min1, h = hour1, d = day1, mo = mon1, y = year1;
    uint8_t regb = regb1;

    if ((regb & 0x04u) == 0) {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin((uint8_t)(h & 0x7Fu)) | (h & 0x80u);
        d = bcd_to_bin(d);
        mo = bcd_to_bin(mo);
        y = bcd_to_bin(y);
    }

    if ((regb & 0x02u) == 0 && (h & 0x80u)) {
        h = (uint8_t)(((h & 0x7Fu) + 12u) % 24u);
    } else {
        h = (uint8_t)(h & 0x7Fu);
    }

    uint32_t full_year = (y < 70u) ? (2000u + y) : (1900u + y);
    *year = full_year;
    *month = mo;
    *day = d;
    *hour = h;
    *min = m;
    *sec = s;
    return true;
}

static bool rtc_is_leap(uint32_t y)
{
    return ((y % 4u) == 0u && (y % 100u) != 0u) || ((y % 400u) == 0u);
}

static uint64_t rtc_unix_seconds(uint32_t y, uint32_t mo, uint32_t d,
                                 uint32_t h, uint32_t mi, uint32_t s)
{
    static const uint16_t month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (y < 1970u || mo < 1u || mo > 12u || d < 1u || d > 31u || h > 23u || mi > 59u || s > 59u) {
        return 0;
    }
    uint64_t days = 0;
    for (uint32_t yy = 1970u; yy < y; yy++) {
        days += rtc_is_leap(yy) ? 366u : 365u;
    }
    for (uint32_t mm = 1u; mm < mo; mm++) {
        days += month_days[mm - 1u];
        if (mm == 2u && rtc_is_leap(y)) {
            days += 1u;
        }
    }
    days += (uint64_t)(d - 1u);
    return days * 86400ULL + (uint64_t)h * 3600ULL + (uint64_t)mi * 60ULL + (uint64_t)s;
}

static uint64_t console_get_uptime_us_internal(void)
{
    extern bool apic_is_available(void);
    extern uint32_t apic_timer_get_ticks(void);
    extern uint32_t apic_timer_get_frequency(void);
    extern uint64_t pit_get_uptime_us(void);
    extern uint32_t pit_get_frequency(void);
    extern uint64_t scheduler_get_ticks(void);
    extern uint64_t cpu_get_time(void);
    extern uint64_t cpu_get_frequency(void);

    uint64_t apic_us = 0;
    uint64_t pit_us = 0;
    uint64_t sched_us = 0;
    uint64_t tsc_us = 0;
    uint64_t rtc_us = 0;

    if (apic_is_available()) {
        uint32_t af = apic_timer_get_frequency();
        if (af > 0) {
            apic_us = ((uint64_t)apic_timer_get_ticks() * 1000000ULL) / (uint64_t)af;
        }
    }

    pit_us = pit_get_uptime_us();

    {
        uint32_t hz = 0;
        if (apic_is_available()) {
            hz = apic_timer_get_frequency();
        }
        if (hz == 0) {
            hz = pit_get_frequency();
        }
        if (hz == 0) {
            hz = 100;
        }
        sched_us = (scheduler_get_ticks() * 1000000ULL) / (uint64_t)hz;
    }

    {
        uint64_t tsc_freq = cpu_get_frequency();
        if (tsc_freq > 0) {
            uint64_t now = cpu_get_time();
            if (!tsc_uptime_base_set) {
                tsc_uptime_base = now;
                tsc_uptime_base_set = true;
            }
            if (now >= tsc_uptime_base) {
                tsc_us = ((now - tsc_uptime_base) * 1000000ULL) / tsc_freq;
            }
        }
    }

    {
        uint32_t y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (rtc_read_datetime(&y, &mo, &d, &h, &mi, &s)) {
            uint64_t now_sec = rtc_unix_seconds(y, mo, d, h, mi, s);
            if (!rtc_uptime_base_set) {
                rtc_uptime_base_sec = now_sec;
                rtc_uptime_base_set = true;
            }
            if (now_sec >= rtc_uptime_base_sec) {
                rtc_us = (now_sec - rtc_uptime_base_sec) * 1000000ULL;
            }
        }
    }

    uint64_t mono_best = apic_us;
    const char* mono_name = "apic";
    if (pit_us > mono_best) {
        mono_best = pit_us;
        mono_name = "pit";
    }
    if (sched_us > mono_best) {
        mono_best = sched_us;
        mono_name = "scheduler";
    }
    if (tsc_us > mono_best) {
        mono_best = tsc_us;
        mono_name = "tsc";
    }

    uint64_t best = mono_best;
    const char* best_name = mono_name;
    if (rtc_uptime_base_set) {
        /*
         * RTC has 1-second granularity; keep it as primary source, but use
         * monotonic timer value as sub-second refinement near second edges.
         */
        if (mono_best >= rtc_us && (mono_best - rtc_us) < 2000000ULL) {
            rtc_us = mono_best;
        }
        best = rtc_us;
        best_name = "bios-rtc";
    }
    uptime_source_name = best_name;
    return best;
}
uint64_t console_get_uptime_us(void)
{
    return console_get_uptime_us_internal();
}

const char* console_get_uptime_source(void)
{
    return uptime_source_name;
}

uint64_t console_get_realtime_us(void)
{
    uint32_t y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (rtc_read_datetime(&y, &mo, &d, &h, &mi, &s)) {
        uint64_t sec = rtc_unix_seconds(y, mo, d, h, mi, s);
        uint64_t sub = console_get_uptime_us_internal() % 1000000ULL;
        return sec * 1000000ULL + sub;
    }
    return console_get_uptime_us_internal();
}

static void console_write_dec_fixed(uint64_t value, int width)
{
    char buf[32];
    int idx = 0;
    do {
        buf[idx++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && idx < (int)sizeof(buf));

    while (idx < width && idx < (int)sizeof(buf)) {
        buf[idx++] = '0';
    }

    for (int i = idx - 1; i >= 0; i--) {
        kputc(buf[i]);
    }
}

static void console_write_hex_fixed(uint64_t value, int width)
{
    const char* hex = "0123456789abcdef";
    char buf[32];
    int idx = 0;
    do {
        buf[idx++] = hex[value & 0xF];
        value >>= 4;
    } while (value && idx < (int)sizeof(buf));

    while (idx < width && idx < (int)sizeof(buf)) {
        buf[idx++] = '0';
    }

    for (int i = idx - 1; i >= 0; i--) {
        kputc(buf[i]);
    }
}

static void console_write_log_prefix(void)
{
    uint64_t us = console_get_uptime_us_internal();
    uint64_t sec = us / 1000000ULL;
    uint64_t micros = us % 1000000ULL;
    uint64_t hours = (sec / 3600ULL) % 24ULL;
    uint64_t mins = (sec / 60ULL) % 60ULL;
    uint64_t secs = sec % 60ULL;

    /* Use fixed date until RTC is implemented */
    console_write_dec_fixed(1970, 4);
    kputc('-');
    console_write_dec_fixed(1, 2);
    kputc('-');
    console_write_dec_fixed(1, 2);
    kputc(' ');
    console_write_dec_fixed(hours, 2);
    kputc(':');
    console_write_dec_fixed(mins, 2);
    kputc(':');
    console_write_dec_fixed(secs, 2);
    kputc('.');
    console_write_dec_fixed(micros, 6);
    kputc('+');
    console_write_dec_fixed(0, 4);
    kputc(' ');

    /* pid */
    kputc('0');
    kputc('x');
    console_write_hex_fixed(0, 3);
    kputs("      ");

    /* level */
    kputs("Info       ");

    /* code */
    kputc('0');
    kputc('x');
    console_write_hex_fixed(0, 3);
    kputs("                ");

    /* pid/tid placeholders */
    console_write_dec_fixed(0, 3);
    kputs("    ");
    console_write_dec_fixed(0, 2);
    kputs("   ");

    /* process */
    kputs("rodnix: ");
}

static void serial_init(void)
{
    /* Disable interrupts */
    outb(SERIAL_COM1_BASE + SERIAL_IER, 0x00);
    /* Enable DLAB */
    outb(SERIAL_COM1_BASE + SERIAL_LCR, 0x80);
    /* Set divisor to 3 (38400 baud) */
    outb(SERIAL_COM1_BASE + SERIAL_DATA, 0x03);
    outb(SERIAL_COM1_BASE + SERIAL_IER, 0x00);
    /* 8 bits, no parity, one stop bit */
    outb(SERIAL_COM1_BASE + SERIAL_LCR, 0x03);
    /* Enable FIFO, clear, 14-byte threshold */
    outb(SERIAL_COM1_BASE + 2, 0xC7);
    /* IRQs enabled, RTS/DSR set */
    outb(SERIAL_COM1_BASE + SERIAL_MCR, 0x0B);

    /* Basic presence check: LSR should not read as 0xFF on absent port. */
    if (inb(SERIAL_COM1_BASE + SERIAL_LSR) != 0xFF) {
        serial_enabled = true;
    }
}

static void serial_write_char(char c)
{
    if (!serial_enabled) {
        return;
    }
    /* Wait for transmitter holding register empty */
    for (int i = 0; i < 10000; i++) {
        if (inb(SERIAL_COM1_BASE + SERIAL_LSR) & 0x20) {
            break;
        }
    }
    outb(SERIAL_COM1_BASE + SERIAL_DATA, (uint8_t)c);
}

/**
 * @function update_cursor
 * @brief Update VGA text mode cursor position
 * 
 * This function updates the hardware cursor position in VGA text mode.
 * The cursor is controlled via I/O ports 0x3D4 (index) and 0x3D5 (data).
 * 
 * @param row Cursor row (0-24)
 * @param col Cursor column (0-79)
 */
static void update_cursor(uint8_t row, uint8_t col)
{
    uint16_t pos = row * VGA_WIDTH + col;
    uint8_t pos_low = (uint8_t)(pos & 0xFF);
    uint8_t pos_high = (uint8_t)((pos >> 8) & 0xFF);
    
    /* Send low byte of cursor position */
    /* Use same approach as pic.c - direct port constants in constraint */
    __asm__ volatile ("outb %%al, %1" : : "a"(VGA_CURSOR_LOW), "Nd"((uint16_t)0x3D4));
    __asm__ volatile ("outb %%al, %1" : : "a"(pos_low), "Nd"((uint16_t)0x3D5));
    
    /* Send high byte of cursor position */
    __asm__ volatile ("outb %%al, %1" : : "a"(VGA_CURSOR_HIGH), "Nd"((uint16_t)0x3D4));
    __asm__ volatile ("outb %%al, %1" : : "a"(pos_high), "Nd"((uint16_t)0x3D5));
}

static void console_set_cursor(uint8_t row, uint8_t col)
{
    if (row >= VGA_HEIGHT) {
        row = VGA_HEIGHT - 1;
    }
    if (col >= VGA_WIDTH) {
        col = VGA_WIDTH - 1;
    }
    vga_row = row;
    vga_col = col;
    update_cursor(vga_row, vga_col);
}

static int ansi_parse_uint(const char* s, int* out)
{
    if (!s || !out) {
        return -1;
    }
    int v = 0;
    int has_digits = 0;
    for (int i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        has_digits = 1;
        v = (v * 10) + (c - '0');
    }
    if (!has_digits) {
        return -1;
    }
    *out = v;
    return 0;
}

static int ansi_parse_row_col(const char* csi, int* out_row, int* out_col)
{
    if (!csi || !out_row || !out_col) {
        return -1;
    }

    int sep = -1;
    for (int i = 0; csi[i] != '\0'; i++) {
        if (csi[i] == ';') {
            sep = i;
            break;
        }
    }

    if (sep < 0) {
        if (csi[0] == '\0') {
            *out_row = 1;
            *out_col = 1;
            return 0;
        }
        if (ansi_parse_uint(csi, out_row) != 0) {
            return -1;
        }
        *out_col = 1;
        return 0;
    }

    char row_buf[8];
    char col_buf[8];
    int rlen = sep;
    int clen = 0;

    if (rlen < 0 || rlen >= (int)sizeof(row_buf)) {
        return -1;
    }
    for (int i = 0; i < rlen; i++) {
        row_buf[i] = csi[i];
    }
    row_buf[rlen] = '\0';

    for (int i = sep + 1; csi[i] != '\0' && clen < (int)sizeof(col_buf) - 1; i++) {
        col_buf[clen++] = csi[i];
    }
    col_buf[clen] = '\0';

    if (row_buf[0] == '\0') {
        *out_row = 1;
    } else if (ansi_parse_uint(row_buf, out_row) != 0) {
        return -1;
    }
    if (col_buf[0] == '\0') {
        *out_col = 1;
    } else if (ansi_parse_uint(col_buf, out_col) != 0) {
        return -1;
    }
    return 0;
}

static bool console_handle_ansi_char(char c)
{
    if (ansi_state == ANSI_STATE_NORMAL) {
        if ((unsigned char)c == 0x1B) {
            ansi_state = ANSI_STATE_ESC;
            return true;
        }
        return false;
    }

    if (ansi_state == ANSI_STATE_ESC) {
        if (c == '[') {
            ansi_state = ANSI_STATE_CSI;
            ansi_csi_len = 0;
            ansi_csi_buf[0] = '\0';
            return true;
        }
        ansi_state = ANSI_STATE_NORMAL;
        return true;
    }

    if (ansi_state == ANSI_STATE_CSI) {
        if ((c >= '0' && c <= '9') || c == ';') {
            if ((size_t)(ansi_csi_len + 1) < sizeof(ansi_csi_buf)) {
                ansi_csi_buf[ansi_csi_len++] = c;
                ansi_csi_buf[ansi_csi_len] = '\0';
            }
            return true;
        }

        if (c == 'J') {
            /* ESC[2J - clear entire screen and home cursor */
            if (ansi_csi_buf[0] == '2' && ansi_csi_buf[1] == '\0') {
                console_clear();
            }
        } else if (c == 'H' || c == 'f') {
            /* ESC[row;colH - absolute cursor position (1-based) */
            int row = 1;
            int col = 1;
            if (ansi_parse_row_col(ansi_csi_buf, &row, &col) == 0) {
                if (row < 1) row = 1;
                if (col < 1) col = 1;
                console_set_cursor((uint8_t)(row - 1), (uint8_t)(col - 1));
            }
        }

        ansi_state = ANSI_STATE_NORMAL;
        ansi_csi_len = 0;
        ansi_csi_buf[0] = '\0';
        return true;
    }

    ansi_state = ANSI_STATE_NORMAL;
    return false;
}

void console_init(void)
{
    vga_row = 0;
    vga_col = 0;
    vga_color = 0x0F;

    {
        extern uint64_t cpu_get_time(void);
        extern uint64_t cpu_get_frequency(void);
        uint64_t tsc_freq = cpu_get_frequency();
        if (tsc_freq > 0) {
            tsc_uptime_base = cpu_get_time();
            tsc_uptime_base_set = true;
        }
    }

    {
        uint32_t y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (rtc_read_datetime(&y, &mo, &d, &h, &mi, &s)) {
            rtc_uptime_base_sec = rtc_unix_seconds(y, mo, d, h, mi, s);
            rtc_uptime_base_set = true;
        }
    }

    serial_init();
    /* Initialize cursor position */
    update_cursor(vga_row, vga_col);
}

void console_set_vga_buffer(void* buffer)
{
    if (!buffer) {
        return;
    }
    vga_buffer = (uint16_t*)buffer;
}

void console_clear(void)
{
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    }
    vga_row = 0;
    vga_col = 0;
    update_cursor(vga_row, vga_col);
}

/**
 * @function scroll_screen
 * @brief Scroll the screen up by one line
 * 
 * This function scrolls the entire screen content up by one line,
 * clearing the bottom line. This is used when the screen is full.
 * 
 * Efficient scrolling using word-sized operations.
 */
static void scroll_screen(void)
{
    /* Safety check: ensure vga_buffer is valid */
    if (!vga_buffer) {
        return;
    }
    
    /* Copy entire screen buffer up by one line using word operations */
    /* This is more efficient than byte-by-byte copying */
    uint16_t* src = vga_buffer + VGA_WIDTH;  /* Start from line 1 */
    uint16_t* dst = vga_buffer;              /* Copy to line 0 */
    uint32_t words_to_copy = (VGA_HEIGHT - 1) * VGA_WIDTH;
    
    /* Copy all lines up by one */
    for (uint32_t i = 0; i < words_to_copy; i++) {
        dst[i] = src[i];
    }
    
    /* Clear the last line */
    uint16_t clear_char = (uint16_t)' ' | ((uint16_t)vga_color << 8);
    uint16_t* last_line = vga_buffer + (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (uint8_t col = 0; col < VGA_WIDTH; col++) {
        last_line[col] = clear_char;
    }
}

void kputc(char c)
{
    /* Allow minimal ANSI cursor/clear control for userspace shell UX. */
    if (console_handle_ansi_char(c)) {
        return;
    }

    if (log_prefix_enabled && log_at_line_start && !log_prefix_in_progress) {
        log_prefix_in_progress = true;
        console_write_log_prefix();
        log_prefix_in_progress = false;
        log_at_line_start = false;
    }

    /* Mirror output to serial for logging */
    if (c == '\n') {
        serial_write_char('\r');
    }
    serial_write_char(c);

    /* Handle backspace */
    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        } else {
            update_cursor(vga_row, vga_col);
            return;
        }
        uint32_t index = vga_row * VGA_WIDTH + vga_col;
        vga_buffer[index] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
        update_cursor(vga_row, vga_col);
        return;
    }

    /* Handle newline */
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            /* Scroll screen when reaching bottom */
            scroll_screen();
            vga_row = VGA_HEIGHT - 1;  /* Stay on last line after scroll */
        }
        update_cursor(vga_row, vga_col);
        log_at_line_start = true;
        return;
    }
    
    /* Handle carriage return */
    if (c == '\r') {
        vga_col = 0;
        update_cursor(vga_row, vga_col);
        return;
    }
    
    /* Handle tab (expand to spaces) */
    if (c == '\t') {
        do {
            uint32_t index = vga_row * VGA_WIDTH + vga_col;
            vga_buffer[index] = (uint16_t)' ' | ((uint16_t)vga_color << 8);
            vga_col++;
            if (vga_col >= VGA_WIDTH) {
                vga_col = 0;
                vga_row++;
                if (vga_row >= VGA_HEIGHT) {
                    scroll_screen();
                    vga_row = VGA_HEIGHT - 1;
                }
            }
        } while ((vga_col & 7) != 0);  /* Tab stops every 8 columns (use bitwise AND instead of modulo) */
        return;
    }
    
    /* Write character to screen */
    uint32_t index = vga_row * VGA_WIDTH + vga_col;
    vga_buffer[index] = (uint16_t)c | ((uint16_t)vga_color << 8);
    
    /* Advance cursor */
    vga_col++;
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            /* Scroll screen when reaching bottom */
            scroll_screen();
            vga_row = VGA_HEIGHT - 1;  /* Stay on last line after scroll */
        }
    }
    
    /* Update hardware cursor position */
    update_cursor(vga_row, vga_col);
}

void kputs(const char* str)
{
    /* Prevent recursive calls from exception handlers */
    if (kputs_in_progress) {
        /* If already in kputs, just write directly to VGA to avoid recursion */
        volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;
        static uint8_t safe_row = 0;
        static uint8_t safe_col = 0;
        
        while (*str && safe_row < VGA_HEIGHT) {
            /* Still mirror to serial in the safe path */
            if (*str == '\n') {
                serial_write_char('\r');
            }
            serial_write_char(*str);
            if (*str == '\n') {
                safe_col = 0;
                safe_row++;
            } else if (*str != '\r') {
                uint32_t idx = safe_row * VGA_WIDTH + safe_col;
                if (idx < VGA_WIDTH * VGA_HEIGHT) {
                    vga[idx] = (uint16_t)*str | ((uint16_t)0x0F << 8);
                }
                safe_col++;
                if (safe_col >= VGA_WIDTH) {
                    safe_col = 0;
                    safe_row++;
                }
            }
            str++;
        }
        return;
    }
    
    kputs_in_progress = true;
    while (*str) {
        kputc(*str);
        str++;
    }
    kputs_in_progress = false;
    /* Force immediate output - no buffering */
    __asm__ volatile ("" ::: "memory");
}

void console_set_log_prefix_enabled(bool enabled)
{
    log_prefix_enabled = enabled;
    log_at_line_start = true;
}

static void kprint_uint(uint64_t num, uint64_t base)
{
    char buffer[32];
    int i = 0;
    
    /* Prevent division by zero */
    if (base == 0 || base > 36) {
        base = 16; /* Default to hexadecimal */
    }
    
    if (num == 0) {
        kputc('0');
        return;
    }
    
    while (num > 0) {
        uint64_t digit = num % base;
        buffer[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        num /= base;
    }
    
    for (int j = i - 1; j >= 0; j--) {
        kputc(buffer[j]);
    }
}

void kprint_dec(uint64_t num)
{
    kprint_uint(num, 10);
}

void kprint_hex(uint64_t num)
{
    kputs("0x");
    kprint_uint(num, 16);
}

void kprint_bin(uint64_t num)
{
    kputs("0b");
    kprint_uint(num, 2);
}

void kprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}

void kvprintf(const char* fmt, va_list args)
{
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            
            /* Handle length modifiers (ll, l, h, hh) */
            int is_long = 0;
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    is_long = 2; /* ll */
                    fmt++;
                } else {
                    is_long = 1; /* l */
                }
            }
            
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (is_long == 2) {
                        val = (int64_t)va_arg(args, int64_t);
                    } else if (is_long == 1) {
                        val = (int64_t)va_arg(args, long);
                    } else {
                        val = (int64_t)va_arg(args, int);
                    }
                    if (val < 0) {
                        kputc('-');
                        val = -val;
                    }
                    kprint_uint((uint64_t)val, 10);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (is_long == 2) {
                        val = va_arg(args, uint64_t);
                    } else if (is_long == 1) {
                        val = (uint64_t)va_arg(args, unsigned long);
                    } else {
                        val = (uint64_t)va_arg(args, unsigned int);
                    }
                    kprint_uint(val, 10);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (is_long == 2) {
                        val = va_arg(args, uint64_t);
                    } else if (is_long == 1) {
                        val = (uint64_t)va_arg(args, unsigned long);
                    } else {
                        val = (uint64_t)va_arg(args, unsigned int);
                    }
                    kprint_hex(val);
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    kputs(str ? str : "(null)");
                    break;
                }
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    kprint_hex((uint64_t)(uintptr_t)ptr);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    kputc(c);
                    break;
                }
                case '%': {
                    kputc('%');
                    break;
                }
                default:
                    kputc('%');
                    if (is_long == 2) {
                        kputc('l');
                        kputc('l');
                    } else if (is_long == 1) {
                        kputc('l');
                    }
                    kputc(*fmt);
                    break;
            }
        } else {
            kputc(*fmt);
        }
        fmt++;
    }
}

void console_set_fg_color(uint8_t color)
{
    vga_color = (vga_color & 0xF0) | (color & 0x0F);
}

void console_set_bg_color(uint8_t color)
{
    vga_color = (vga_color & 0x0F) | ((color & 0x0F) << 4);
}

void console_reset_color(void)
{
    vga_color = 0x0F;
}
