/**
 * @file input_core.c
 * @brief InputCore - системный слой ввода (аналог IOHIDSystem в XNU)
 * 
 * Этот модуль реализует системный слой ввода, который:
 * - принимает сырые события от драйверов (scancode)
 * - ведёт состояние клавиатуры (модификаторы, caps lock и т.д.)
 * - переводит scancode → ASCII
 * - предоставляет стабильный API для потребителей (shell, console)
 * 
 * Архитектура:
 * [ hardware driver ] → [ InputCore ] → [ shell / console / UI ]
 */

#include "input.h"
#include "../fabric/spin.h"
#include "../../include/console.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

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
    
    kputs("[InputCore] Initialization complete\n");
}

/**
 * @function input_push_scancode
 * @brief Push raw scan code from driver (entry point for drivers)
 * 
 * @param scancode Raw scan code from keyboard
 * @param pressed Whether key is pressed (true) or released (false)
 * 
 * @note This is called from IRQ context by drivers
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
    
    /* Lock for thread safety */
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

/**
 * @function input_has_char
 * @brief Check if input buffer has characters
 * 
 * @return true if characters are available, false otherwise
 */
bool input_has_char(void)
{
    /* XNU-style: Process queued scan codes first */
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
    /* XNU-style: Process queued scan codes first */
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
        /* XNU-style: Process queued scan codes first */
        extern void input_process_queue(void);
        input_process_queue();
        
        /* Get character */
        int c = input_read_char();
        
        if (c == -1) {
            /* No character available, wait for interrupt */
            /* Use interrupt_wait() which properly handles interrupts */
            extern void interrupt_wait(void);
            interrupt_wait();
            /* Process queue again after interrupt - scan codes may have arrived */
            input_process_queue();
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

