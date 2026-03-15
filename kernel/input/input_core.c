/**
 * @file input_core.c
 * @brief InputCore - system input layer
 * 
 * This module implements the system input layer, which:
 * - receives raw events from drivers (scancode)
 * - maintains keyboard state (modifiers, caps lock, etc.)
 * - translates scancode → ASCII
 * - provides stable API for consumers (shell, console)
 * 
 * Architecture:
 * [ hardware driver ] → [ InputCore ] → [ shell / console / UI ]
 */

#include "input.h"
#include "../fabric/spin.h"
#include "../common/scheduler.h"
#include "../../include/console.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Serial Input (COM1) - optional fallback for headless QEMU
 * ============================================================================ */

#ifndef SERIAL_INPUT_ENABLED
#define SERIAL_INPUT_ENABLED 1
#endif

#define SERIAL_COM1_BASE 0x3F8
#define SERIAL_DATA      0
#define SERIAL_LSR       5

#if SERIAL_INPUT_ENABLED
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int serial_read_char(void)
{
    uint8_t lsr = inb(SERIAL_COM1_BASE + SERIAL_LSR);
    /* If port absent, LSR may read as 0xFF */
    if (lsr == 0xFF) {
        return -1;
    }
    if ((lsr & 0x01) == 0) {
        return -1;
    }
    uint8_t c = inb(SERIAL_COM1_BASE + SERIAL_DATA);
    if (c == '\r') {
        c = '\n';
    }
    return (int)c;
}

static bool serial_has_char(void)
{
    uint8_t lsr = inb(SERIAL_COM1_BASE + SERIAL_LSR);
    if (lsr == 0xFF) {
        return false;
    }
    return (lsr & 0x01) != 0;
}
#else
static int __attribute__((unused)) serial_read_char(void)
{
    return -1;
}

static bool __attribute__((unused)) serial_has_char(void)
{
    return false;
}
#endif

/* ============================================================================
 * InputCore Constants
 * ============================================================================ */

#define INPUT_BUFFER_SIZE  256    /* Input buffer size (power of 2) */

/* Special key codes (scan code set 1) */
#define KEY_LSHIFT           0x2A
#define KEY_RSHIFT           0x36
#define KEY_CTRL             0x1D
#define KEY_ALT              0x38
#define KEY_CAPSLOCK         0x3A
#define KEY_ENTER            0x1C
#define KEY_BACKSPACE        0x0E
#define KEY_TAB              0x0F
#define KEY_ESC              0x01

/* ============================================================================
 * InputCore State
 * ============================================================================ */

/**
 * @struct input_state
 * @brief InputCore internal state
 */
struct input_state {
    uint8_t buffer[INPUT_BUFFER_SIZE];  /* Input buffer (ASCII characters) */
    uint32_t buffer_head;                /* Buffer head index */
    uint32_t buffer_tail;                /* Buffer tail index */
    uint32_t buffer_count;               /* Number of characters in buffer */
    bool shift_pressed;                  /* Shift key state */
    bool ctrl_pressed;                   /* Ctrl key state */
    bool alt_pressed;                    /* Alt key state */
    bool caps_lock;                      /* Caps Lock state */
    bool num_lock;                       /* Num Lock state */
    bool scroll_lock;                     /* Scroll Lock state */
    bool extended;                        /* Extended scan code flag */
    spinlock_t lock;                     /* Spinlock for thread safety */
};

/* Global input state */
static struct input_state input_state = {0};
static bool input_polling_enabled = true;

static bool input_cpu_interrupts_enabled(void)
{
    uint64_t rflags = 0;
    __asm__ volatile ("pushfq; popq %0" : "=r"(rflags));
    return (rflags & (1ull << 9)) != 0;
}

static bool input_should_poll_ps2(void)
{
    if (input_polling_enabled) {
        return true;
    }
    /* Fast syscall path can run with IF=0; fall back to direct polling. */
    return !input_cpu_interrupts_enabled();
}

/* ============================================================================
 * Scan Code Translation Tables
 * ============================================================================ */

/* Normal (non-shifted) key map */
static const char scan_code_normal[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

/* Shifted key map */
static const char scan_code_shift[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,   0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @function input_buffer_put
 * @brief Put a character into the input buffer
 * 
 * @param c Character to add
 * 
 * @return 0 on success, -1 if buffer is full
 * 
 * @note Must be called with lock held
 * @note Uses bitwise AND instead of modulo (buffer size is power of 2)
 */
static int input_buffer_put(uint8_t c)
{
    (void)c;
    /* Check if buffer is full */
    if (input_state.buffer_count >= INPUT_BUFFER_SIZE) {
        return -1; /* Buffer full */
    }
    
    /* Add character to buffer */
    input_state.buffer[input_state.buffer_tail] = c;
    
    /* Update tail index using bitwise AND (no division) */
    /* INPUT_BUFFER_SIZE is 256 (2^8), so (x & 255) == (x % 256) */
    input_state.buffer_tail = (input_state.buffer_tail + 1) & 0xFF;
    input_state.buffer_count++;
    
    return 0;
}

/**
 * @function input_buffer_get
 * @brief Get a character from the input buffer
 * 
 * @return Character, or 0 if buffer is empty
 * 
 * @note Must be called with lock held
 */
static uint8_t input_buffer_get(void)
{
    if (input_state.buffer_count == 0) {
        return 0; /* Buffer empty */
    }
    
    uint8_t c = input_state.buffer[input_state.buffer_head];
    
    /* Update head index using bitwise AND (no division) */
    input_state.buffer_head = (input_state.buffer_head + 1) & 0xFF;
    input_state.buffer_count--;
    
    return c;
}

/**
 * @function input_translate_scancode
 * @brief Translate scan code to ASCII character
 * 
 * @param scancode Raw scan code from keyboard
 * @param pressed Whether key is pressed (true) or released (false)
 * 
 * @return ASCII character, or 0 if not a printable character
 * 
 * @note Must be called with lock held
 * @note No division operations - only table lookups and bitwise operations
 */
static char input_translate_scancode(uint8_t scancode, bool pressed)
{
    /* Handle key release */
    if (!pressed) {
        /* Update modifier key states */
        switch (scancode) {
            case KEY_LSHIFT:
            case KEY_RSHIFT:
                input_state.shift_pressed = false;
                break;
            case KEY_CTRL:
                input_state.ctrl_pressed = false;
                break;
            case KEY_ALT:
                input_state.alt_pressed = false;
                break;
        }
        
        return 0; /* No character on release */
    }
    
    /* Handle special keys */
    switch (scancode) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            input_state.shift_pressed = true;
            return 0;
            
        case KEY_CTRL:
            input_state.ctrl_pressed = true;
            return 0;
            
        case KEY_ALT:
            input_state.alt_pressed = true;
            return 0;
            
        case KEY_CAPSLOCK:
            input_state.caps_lock = !input_state.caps_lock;
            return 0;
            
        case KEY_ENTER:
            return '\n';
            
        case KEY_BACKSPACE:
            return '\b';
            
        case KEY_TAB:
            return '\t';
            
        case KEY_ESC:
            return 0x1B; /* ESC character */
            
        default:
            break;
    }
    
    /* Translate printable characters */
    if (scancode < 128) {
        bool shift = input_state.shift_pressed;
        bool caps = input_state.caps_lock;
        
        /* Determine which map to use */
        const char* map = shift ? scan_code_shift : scan_code_normal;
        
        /* Get character from map (simple array lookup, no division) */
        char c = map[scancode];
        
        /* Handle Caps Lock for letters (only bitwise operations) */
        if (caps && !shift && c >= 'a' && c <= 'z') {
            /* Convert to uppercase: c - 'a' + 'A' (no division) */
            c = c - 'a' + 'A';
        } else if (caps && shift && c >= 'A' && c <= 'Z') {
            /* Convert to lowercase: c - 'A' + 'a' (no division) */
            c = c - 'A' + 'a';
        }
        
        return c;
    }
    
    return 0; /* Unknown scan code */
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function input_init_keyboard
 * @brief Initialize keyboard input subsystem
 * 
 * This function initializes InputCore state.
 * Called once when keyboard driver attaches.
 */
void input_init_keyboard(void)
{
    extern void kputs(const char* str);
    kputs("[InputCore] Initializing keyboard input subsystem\n");
    
    /* Initialize spinlock */
    spinlock_init(&input_state.lock);
    
    /* Initialize input state */
    input_state.buffer_head = 0;
    input_state.buffer_tail = 0;
    input_state.buffer_count = 0;
    input_state.shift_pressed = false;
    input_state.ctrl_pressed = false;
    input_state.alt_pressed = false;
    input_state.caps_lock = false;
    input_state.num_lock = false;
    input_state.scroll_lock = false;
    input_state.extended = false;
    input_polling_enabled = true;
    
    kputs("[InputCore] Initialization complete\n");
}

/**
 * @function input_push_scancode
 * @brief Push raw scan code from driver (entry point for drivers)
 * 
 * @param scancode Raw scan code from keyboard
 * @param pressed Whether key is pressed (true) or released (false)
 * 
 * @note Called from non-IRQ context (IRQ handlers should enqueue and defer)
 * @note Handles extended scan code prefix (0xE0)
 * @note Translation to ASCII is done here
 */
void input_push_scancode(uint16_t scancode, bool pressed)
{
    /* Handle extended scan code prefix */
    if (scancode == 0xE0) {
        spinlock_lock(&input_state.lock);
        input_state.extended = true;
        spinlock_unlock(&input_state.lock);
        return;
    }
    
    /* Lock for thread safety (non-IRQ context) */
    spinlock_lock(&input_state.lock);
    
    /* Translate scan code to character */
    char c = input_translate_scancode((uint8_t)scancode, pressed);
    
    /* If we got a character, add it to buffer */
    if (c != 0) {
        input_buffer_put((uint8_t)c);
    }
    
    /* Clear extended flag after processing */
    input_state.extended = false;
    
    spinlock_unlock(&input_state.lock);
}

/* ============================================================================
 * Helper: poll the PS/2 keyboard in polling mode
 * ============================================================================
 *
 * Used as a fallback when IRQ1 is unavailable or disabled.
 * Reads scan codes directly from ports 0x64/0x60 and forwards them through
 * input_push_scancode(), so translation and buffering stay on the common path.
 */
static void input_poll_keyboard_ps2(void)
{
    uint8_t status;
    /* Read keyboard controller status from port 0x64. */
    __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));
    
    /* No data is available when the output buffer bit is clear. */
    if ((status & 0x01) == 0) {
        return;
    }
    
    /* Read the scan code from port 0x60. */
    uint8_t scan_code;
    __asm__ volatile ("inb %1, %0" : "=a"(scan_code) : "Nd"((uint16_t)0x60));
    
    bool pressed = (scan_code & 0x80) == 0;
    uint16_t code = (uint16_t)(scan_code & 0x7F);
    
    /* Handle the extended scan code prefix. */
    if (scan_code == 0xE0) {
        code = 0xE0;
        pressed = true;
    }
    
    extern void input_push_scancode(uint16_t scancode, bool pressed);
    input_push_scancode(code, pressed);

}

/**
 * @function input_has_char
 * @brief Check if input buffer has characters
 * 
 * @return true if characters are available, false otherwise
 */
bool input_has_char(void)
{
    /* Serial fallback (useful for headless QEMU with -serial stdio) */
#if SERIAL_INPUT_ENABLED
    if (serial_has_char()) {
        return true;
    }
#endif

    if (input_should_poll_ps2()) {
        input_poll_keyboard_ps2();
    }
    
    /* Then drain the IRQ driver scan code queue if it is active. */
    extern void input_process_queue(void);
    input_process_queue();
    
    bool result;
    
    spinlock_lock(&input_state.lock);
    result = input_state.buffer_count > 0;
    spinlock_unlock(&input_state.lock);
    
    return result;
}

/**
 * @function input_read_char
 * @brief Read a character from input buffer
 * 
 * @return Character, or -1 if no character available
 * 
 * @note This is a non-blocking function. Returns -1 if buffer is empty.
 */
int input_read_char(void)
{
    /* Serial fallback (useful for headless QEMU with -serial stdio) */
#if SERIAL_INPUT_ENABLED
    int serial_c = serial_read_char();
    if (serial_c != -1) {
        return serial_c;
    }
#endif

    if (input_should_poll_ps2()) {
        input_poll_keyboard_ps2();
    }
    
    /* Then drain the IRQ driver scan code queue if it is active. */
    extern void input_process_queue(void);
    input_process_queue();
    
    int result = -1;
    
    spinlock_lock(&input_state.lock);
    
    if (input_state.buffer_count > 0) {
        uint8_t c = input_buffer_get();
        result = (int)c;
        /* Character read - no diagnostic output to avoid cluttering console */
    }
    
    spinlock_unlock(&input_state.lock);
    
    return result;
}

void input_set_polling_enabled(bool enabled)
{
    input_polling_enabled = enabled;
}

void input_flush(void)
{
    extern void input_process_queue(void);
    extern void hid_kbd_flush_queue(void) __attribute__((weak));

    if (hid_kbd_flush_queue) {
        hid_kbd_flush_queue();
    }
    input_process_queue();
    spinlock_lock(&input_state.lock);
    input_state.buffer_head = 0;
    input_state.buffer_tail = 0;
    input_state.buffer_count = 0;
    spinlock_unlock(&input_state.lock);
}


/**
 * @function input_read_line
 * @brief Read a line from input (blocking)
 * 
 * @param buf Buffer to store the line
 * @param n Size of buffer
 * 
 * @return Number of characters read (excluding null terminator)
 * 
 * @note Blocks until Enter is pressed
 */
size_t input_read_line(char *buf, size_t n)
{
    if (!buf || n == 0) {
        return 0;
    }
    
    size_t pos = 0;
    buf[0] = '\0';
    
    /* External console functions */
    extern void kputc(char c);
    
    
    while (pos < n - 1) {
        /* Process queued scan codes first */
        extern void input_process_queue(void);
        input_process_queue();
        
        /* Get character */
        int c = input_read_char();
        
        if (c == -1) {
            /* No character available. In polling mode IRQs may be disabled,
             * so avoid HLT which can block forever. Use a short pause loop. */
            scheduler_ast_check();
            for (volatile int i = 0; i < 10000; i++) {
                __asm__ volatile ("pause");
            }
            /* Process queue again after a short delay */
            input_process_queue();
            scheduler_ast_check();
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            /* Enter pressed */
            buf[pos] = '\0';
            kputc('\n');
            return pos;
        }
        
        if (c == '\b' || c == 0x7F) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                /* Handle backspace visually */
                kputc('\b');
                kputc(' ');
                kputc('\b');
            }
            continue;
        }
        
        if (c >= 32 && c < 127) {
            /* Printable character */
            buf[pos] = (char)c;
            pos++;
            buf[pos] = '\0';
            /* Display character as user types (echo) */
            kputc((char)c);
            /* Ensure character is displayed immediately */
            __asm__ volatile ("" ::: "memory");
        } else if (c != 0) {
            /* Non-printable but valid character (like control chars) */
            /* Still add to buffer but don't echo */
            buf[pos] = (char)c;
            pos++;
            buf[pos] = '\0';
        }
    }
    
    buf[n - 1] = '\0';
    return pos;
}
