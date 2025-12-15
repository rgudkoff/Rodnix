/**
 * @file keyboard.c
 * @brief PS/2 Keyboard driver implementation for x86_64
 * 
 * This module implements PS/2 keyboard support following fabric principles
 * principles:
 * - Event-driven input system
 * - Scan code translation (set 1 to ASCII)
 * - Input buffer management
 * - Key state tracking
 * 
 * @note This implementation is adapted for RodNIX.
 * @note Interrupt handler is minimal - only reads scan code and adds to buffer.
 *       Translation is done in non-interrupt context.
 */

#include "types.h"
#include "pic.h"
#include "../../core/interrupts.h"
#include "../../include/console.h"
#include "../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration */
extern int interrupt_register(uint32_t vector, interrupt_handler_t handler);

/* ============================================================================
 * PS/2 Keyboard I/O Ports
 * ============================================================================ */

#define KEYBOARD_DATA_PORT    0x60    /* Keyboard data port */
#define KEYBOARD_STATUS_PORT  0x64    /* Keyboard status/command port */

/* Keyboard status register bits */
#define KEYBOARD_STATUS_OUTPUT_FULL  0x01    /* Output buffer full (data available) */
#define KEYBOARD_STATUS_INPUT_FULL   0x02    /* Input buffer full (don't write) */
#define KEYBOARD_STATUS_SYSTEM_FLAG  0x04    /* System flag */
#define KEYBOARD_STATUS_CMD_DATA    0x08    /* Command/data (0=data, 1=command) */
#define KEYBOARD_STATUS_TIMEOUT     0x40    /* Timeout error */
#define KEYBOARD_STATUS_PARITY      0x80    /* Parity error */

/* ============================================================================
 * Keyboard Constants
 * ============================================================================ */

#define KEYBOARD_BUFFER_SIZE  256    /* Input buffer size (power of 2) */
#define KEYBOARD_IRQ          1      /* Keyboard IRQ number */
#define KEYBOARD_VECTOR       33     /* Keyboard interrupt vector (32 + 1) */

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
 * Keyboard State
 * ============================================================================ */

/**
 * @struct keyboard_state
 * @brief Keyboard driver state
 */
struct keyboard_state {
    uint8_t buffer[KEYBOARD_BUFFER_SIZE];  /* Input buffer */
    uint32_t buffer_head;                   /* Buffer head index */
    uint32_t buffer_tail;                   /* Buffer tail index */
    uint32_t buffer_count;                  /* Number of characters in buffer */
    bool shift_pressed;                     /* Shift key state */
    bool ctrl_pressed;                      /* Ctrl key state */
    bool alt_pressed;                       /* Alt key state */
    bool caps_lock;                         /* Caps Lock state */
    bool num_lock;                          /* Num Lock state */
    bool scroll_lock;                       /* Scroll Lock state */
    bool extended;                          /* Extended scan code flag */
};

/* Global keyboard state */
static struct keyboard_state kb_state = {0};

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
 * @function keyboard_buffer_put
 * @brief Put a character into the input buffer
 * 
 * @param c Character to add
 * 
 * @return 0 on success, -1 if buffer is full
 * 
 * @note Minimal operation in interrupt context
 * @note Uses bitwise AND instead of modulo (buffer size is power of 2)
 */
static int keyboard_buffer_put(uint8_t c)
{
    /* Check if buffer is full */
    if (kb_state.buffer_count >= KEYBOARD_BUFFER_SIZE) {
        return -1; /* Buffer full */
    }
    
    /* Add character to buffer */
    kb_state.buffer[kb_state.buffer_tail] = c;
    
    /* Update tail index using bitwise AND (no division) */
    /* KEYBOARD_BUFFER_SIZE is 256 (2^8), so (x & 255) == (x % 256) */
    kb_state.buffer_tail = (kb_state.buffer_tail + 1) & 0xFF;
    kb_state.buffer_count++;
    
    return 0;
}

/**
 * @function keyboard_buffer_get
 * @brief Get a character from the input buffer
 * 
 * @return Character, or 0 if buffer is empty
 * 
 * @note Safe to call from non-interrupt context
 */
static uint8_t keyboard_buffer_get(void)
{
    if (kb_state.buffer_count == 0) {
        return 0; /* Buffer empty */
    }
    
    uint8_t c = kb_state.buffer[kb_state.buffer_head];
    
    /* Update head index using bitwise AND (no division) */
    kb_state.buffer_head = (kb_state.buffer_head + 1) & 0xFF;
    kb_state.buffer_count--;
    
    return c;
}

/**
 * @function keyboard_translate_scan_code
 * @brief Translate scan code to ASCII character
 * 
 * @param scan_code Raw scan code from keyboard
 * @param is_release Whether this is a key release (0x80 bit set)
 * 
 * @return ASCII character, or 0 if not a printable character
 * 
 * @note Translation done in non-interrupt context
 * @note No division operations - only table lookups and bitwise operations
 */
static char keyboard_translate_scan_code(uint8_t scan_code, bool is_release)
{
    /* Handle key release */
    if (is_release) {
        uint8_t key_code = scan_code & 0x7F;
        
        /* Update modifier key states */
        switch (key_code) {
            case KEY_LSHIFT:
            case KEY_RSHIFT:
                kb_state.shift_pressed = false;
                break;
            case KEY_CTRL:
                kb_state.ctrl_pressed = false;
                break;
            case KEY_ALT:
                kb_state.alt_pressed = false;
                break;
        }
        
        return 0; /* No character on release */
    }
    
    /* Handle special keys */
    switch (scan_code) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            kb_state.shift_pressed = true;
            return 0;
            
        case KEY_CTRL:
            kb_state.ctrl_pressed = true;
            return 0;
            
        case KEY_ALT:
            kb_state.alt_pressed = true;
            return 0;
            
        case KEY_CAPSLOCK:
            kb_state.caps_lock = !kb_state.caps_lock;
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
    if (scan_code < 128) {
        bool shift = kb_state.shift_pressed;
        bool caps = kb_state.caps_lock;
        
        /* Determine which map to use */
        const char* map = shift ? scan_code_shift : scan_code_normal;
        
        /* Get character from map (simple array lookup, no division) */
        char c = map[scan_code];
        
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
 * Interrupt Handler (Minimal work in interrupt context)
 * ============================================================================ */

/**
 * @function keyboard_interrupt_handler
 * @brief Keyboard interrupt handler (minimal implementation)
 * 
 * This function is called on each keyboard interrupt (IRQ 1). It:
 * 1. Reads scan code from keyboard (I/O operation)
 * 2. Adds raw scan code to buffer (minimal operation)
 * 
 * Translation to ASCII is done later in non-interrupt context.
 * 
 * @param ctx Interrupt context
 * 
 * @note Minimal work in interrupt handler
 * @note No division, no complex operations, only I/O and buffer management
 */
static void keyboard_interrupt_handler(interrupt_context_t* ctx)
{
    (void)ctx;
    
    /* Read scan code from keyboard */
    uint8_t scan_code;
    __asm__ volatile ("inb %1, %0" : "=a"(scan_code) : "Nd"(KEYBOARD_DATA_PORT));
    
    /* Check for extended scan code prefix */
    if (scan_code == 0xE0) {
        kb_state.extended = true;
        return;
    }
    
    /* Add raw scan code to buffer, translate later */
    /* This avoids any complex operations in interrupt context */
    if (kb_state.buffer_count < KEYBOARD_BUFFER_SIZE) {
        kb_state.buffer[kb_state.buffer_tail] = scan_code;
        kb_state.buffer_tail = (kb_state.buffer_tail + 1) & 0xFF;
        kb_state.buffer_count++;
    }
    
    kb_state.extended = false;
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function keyboard_hw_init
 * @brief Initialize keyboard hardware state
 * 
 * This function initializes keyboard state only.
 * IRQ registration is done by Fabric driver.
 * 
 * @note This is called by Fabric HID keyboard driver.
 */
void keyboard_hw_init(void)
{
    /* Initialize keyboard state */
    kb_state.buffer_head = 0;
    kb_state.buffer_tail = 0;
    kb_state.buffer_count = 0;
    kb_state.shift_pressed = false;
    kb_state.ctrl_pressed = false;
    kb_state.alt_pressed = false;
    kb_state.caps_lock = false;
    kb_state.num_lock = false;
    kb_state.scroll_lock = false;
    kb_state.extended = false;
}

/**
 * @function keyboard_buffer_put_raw
 * @brief Put raw scan code into keyboard buffer
 * 
 * @param scan_code Raw scan code from keyboard
 * 
 * @note This is called by Fabric IRQ handler
 * @note Handles extended scan code prefix (0xE0)
 */
void keyboard_buffer_put_raw(uint8_t scan_code)
{
    /* Check for extended scan code prefix */
    if (scan_code == 0xE0) {
        kb_state.extended = true;
        return;
    }
    
    /* Add scan code to buffer */
    keyboard_buffer_put(scan_code);
    
    /* Clear extended flag after processing */
    kb_state.extended = false;
}

/**
 * @function keyboard_read_char
 * @brief Read a character from keyboard buffer
 * 
 * @return Character, or 0 if no character available
 * 
 * @note This is a non-blocking function. Returns 0 if buffer is empty.
 * @note Translation done here, not in interrupt handler
 */
char keyboard_read_char(void)
{
    /* Get raw scan code from buffer */
    uint8_t scan_code = keyboard_buffer_get();
    
    if (scan_code == 0) {
        return 0; /* Buffer empty */
    }
    
    /* Translate scan code to character (non-interrupt context) */
    bool is_release = (scan_code & 0x80) != 0;
    return keyboard_translate_scan_code(scan_code, is_release);
}

/**
 * @function keyboard_read_line
 * @brief Read a line from keyboard (blocking)
 * 
 * @param buffer Buffer to store the line
 * @param size Size of buffer
 * 
 * @return Number of characters read, or -1 on error
 * 
 * @note Blocks until Enter is pressed
 * @note Translation and echo done in non-interrupt context
 */
int keyboard_read_line(char* buffer, uint32_t size)
{
    if (!buffer || size == 0) {
        return -1;
    }
    
    uint32_t pos = 0;
    buffer[0] = '\0';
    
    while (pos < size - 1) {
        /* Get character (translated from scan code) */
        char c = keyboard_read_char();
        
        if (c == 0) {
            /* No character available, wait for interrupt */
            __asm__ volatile ("hlt"); /* Wait for next interrupt */
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            /* Enter pressed */
            buffer[pos] = '\0';
            extern void kputc(char c);
            kputc('\n');
            return (int)pos;
        }
        
        if (c == '\b' || c == 0x7F) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                /* Handle backspace visually */
                extern void kputc(char c);
                kputc('\b');
                kputc(' ');
                kputc('\b');
            }
            continue;
        }
        
        if (c >= 32 && c < 127) {
            /* Printable character */
            buffer[pos] = c;
            pos++;
            buffer[pos] = '\0';
            /* Display character as user types */
            extern void kputc(char c);
            kputc(c);
        }
    }
    
    buffer[size - 1] = '\0';
    return (int)pos;
}

/**
 * @function keyboard_has_input
 * @brief Check if keyboard buffer has input
 * 
 * @return true if characters are available, false otherwise
 */
bool keyboard_has_input(void)
{
    return kb_state.buffer_count > 0;
}
