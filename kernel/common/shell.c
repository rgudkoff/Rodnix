/**
 * @file shell.c
 * @brief Simple shell implementation
 * 
 * This module implements a basic command-line shell following
 * command processing principles:
 * - Command parsing and execution
 * - Built-in commands
 * - Command history (future)
 * - Tab completion (future)
 * 
 * @note This implementation is adapted for RodNIX.
 */

#include "shell.h"
#include "../input/input.h"
#include "../../include/console.h"
#include "../../include/debug.h"
#include "../../include/common.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Shell Constants
 * ============================================================================ */

#define SHELL_MAX_LINE_LENGTH  256
#define SHELL_MAX_ARGS         16
#define SHELL_PROMPT           "rodnix> "

/* ============================================================================
 * Shell State
 * ============================================================================ */

/**
 * @struct shell_state
 * @brief Shell internal state
 */
struct shell_state {
    char current_line[SHELL_MAX_LINE_LENGTH];
    bool running;
    uint32_t command_count;
};

static struct shell_state shell_state = {0};

/* ============================================================================
 * Built-in Commands
 * ============================================================================ */

/**
 * @function shell_cmd_help
 * @brief Display help information
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_help(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    kputs("RodNIX Shell - Available commands:\n");
    kputs("  help      - Show this help message\n");
    kputs("  clear     - Clear the screen\n");
    kputs("  info      - Show system information\n");
    kputs("  memory    - Show memory statistics\n");
    kputs("  timer     - Show timer information\n");
    kputs("  echo      - Echo arguments\n");
    kputs("  exit      - Exit shell (reboot)\n");
    kputs("\n");
    
    return 0;
}

/**
 * @function shell_cmd_clear
 * @brief Clear the screen
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_clear(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    console_clear();
    return 0;
}

/**
 * @function shell_cmd_info
 * @brief Show system information
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_info(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    kputs("RodNIX Kernel v0.1\n");
    kputs("Architecture: x86_64 (64-bit)\n");
    kputs("Build: " __DATE__ " " __TIME__ "\n");
    kputs("\n");
    
    return 0;
}

/**
 * @function shell_cmd_memory
 * @brief Show memory statistics
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_memory(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    extern uint64_t pmm_get_total_pages(void);
    extern uint64_t pmm_get_free_pages(void);
    extern uint64_t pmm_get_used_pages(void);
    
    uint64_t total = pmm_get_total_pages();
    uint64_t free = pmm_get_free_pages();
    uint64_t used = pmm_get_used_pages();
    
    kprintf("Physical Memory:\n");
    kprintf("  Total: %llu pages (%llu KB)\n", total, (total * 4));
    kprintf("  Free:  %llu pages (%llu KB)\n", free, (free * 4));
    kprintf("  Used:  %llu pages (%llu KB)\n", used, (used * 4));
    kputs("\n");
    
    return 0;
}

/**
 * @function shell_cmd_timer
 * @brief Show timer information
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_timer(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    extern uint32_t pit_get_ticks(void);
    extern uint32_t pit_get_frequency(void);
    
    uint32_t ticks = pit_get_ticks();
    uint32_t freq = pit_get_frequency();
    /* Prevent division by zero */
    if (freq == 0) {
        freq = 1;
    }
    
    uint32_t seconds = ticks / freq;
    uint32_t ms = (ticks % freq) * 1000 / freq;
    
    kprintf("Timer Information:\n");
    kprintf("  Frequency: %u Hz\n", freq);
    kprintf("  Ticks:     %u\n", ticks);
    kprintf("  Uptime:    %u.%03u seconds\n", seconds, ms);
    kputs("\n");
    
    return 0;
}

/**
 * @function shell_cmd_echo
 * @brief Echo arguments
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_echo(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        kputs(argv[i]);
        if (i < argc - 1) {
            kputc(' ');
        }
    }
    kputc('\n');
    return 0;
}

/**
 * @function shell_cmd_exit
 * @brief Exit shell (reboot system)
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success
 */
static int shell_cmd_exit(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    kputs("Rebooting...\n");
    /* TODO: Implement actual reboot */
    for (;;) {
        __asm__ volatile ("hlt");
    }
    
    return 0;
}

/* ============================================================================
 * Command Table
 * ============================================================================ */

/**
 * @struct shell_command
 * @brief Shell command entry
 */
struct shell_command {
    const char* name;              /* Command name */
    int (*handler)(int argc, char** argv);  /* Command handler */
    const char* description;        /* Command description */
};

/* Built-in commands table */
static const struct shell_command commands[] = {
    {"help",    shell_cmd_help,    "Show help information"},
    {"clear",   shell_cmd_clear,    "Clear the screen"},
    {"info",    shell_cmd_info,    "Show system information"},
    {"memory",  shell_cmd_memory,  "Show memory statistics"},
    {"timer",   shell_cmd_timer,   "Show timer information"},
    {"echo",    shell_cmd_echo,    "Echo arguments"},
    {"exit",    shell_cmd_exit,    "Exit shell and reboot"},
    {NULL, NULL, NULL}  /* End marker */
};

/* ============================================================================
 * Command Parsing
 * ============================================================================ */

/**
 * @function shell_parse_command
 * @brief Parse command line into arguments
 * 
 * @param line Command line string
 * @param argv Array to store arguments
 * @param max_args Maximum number of arguments
 * 
 * @return Number of arguments parsed
 */
static int shell_parse_command(char* line, char* argv[], int max_args)
{
    int argc = 0;
    bool in_word = false;
    
    for (uint32_t i = 0; line[i] != '\0' && argc < max_args; i++) {
        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') {
            if (in_word) {
                line[i] = '\0';
                in_word = false;
            }
        } else {
            if (!in_word) {
                argv[argc++] = &line[i];
                in_word = true;
            }
        }
    }
    
    return argc;
}

/**
 * @function shell_execute_command
 * @brief Execute a command
 * 
 * @param argc Number of arguments
 * @param argv Argument array
 * 
 * @return 0 on success, -1 if command not found
 */
static int shell_execute_command(int argc, char** argv)
{
    if (argc == 0 || !argv[0]) {
        return 0; /* Empty command */
    }
    
    /* Search for command in command table */
    for (uint32_t i = 0; commands[i].name != NULL; i++) {
        extern int strcmp(const char* s1, const char* s2);
        if (strcmp(argv[0], commands[i].name) == 0) {
            return commands[i].handler(argc, argv);
        }
    }
    
    /* Command not found */
    kprintf("Command not found: %s\n", argv[0]);
    kputs("Type 'help' for available commands.\n");
    
    return -1;
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function shell_init
 * @brief Initialize shell
 * 
 * @return 0 on success, -1 on failure
 */
int shell_init(void)
{
    shell_state.running = true;
    shell_state.command_count = 0;
    shell_state.current_line[0] = '\0';
    
    return 0;
}

/**
 * @function shell_run
 * @brief Run shell main loop
 * 
 * This function runs the shell's main loop, reading commands and executing them.
 * It follows command processing but is adapted for RodNIX.
 * 
 * @note This function blocks and runs indefinitely until shell is stopped.
 */
void shell_run(void)
{
    char line[SHELL_MAX_LINE_LENGTH];
    char* argv[SHELL_MAX_ARGS];
    
    extern void kputs(const char* str);
    extern void kputc(char c);
    
    kputs("[SHELL] shell_run() called\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("\nRodNIX Shell v0.1\n");
    kputs("Type 'help' for available commands.\n\n");
    __asm__ volatile ("" ::: "memory");
    
    while (shell_state.running) {
        /* Display prompt */
        kputs(SHELL_PROMPT);
        __asm__ volatile ("" ::: "memory"); /* Ensure prompt is flushed */
        
        /* Read command line */
        size_t len = input_read_line(line, SHELL_MAX_LINE_LENGTH);
        
        if (len == 0) {
            kputc('\n');
            continue; /* Empty line */
        }
        
        /* Parse command */
        int argc = shell_parse_command(line, argv, SHELL_MAX_ARGS);
        
        if (argc > 0) {
            /* Execute command */
            shell_execute_command(argc, argv);
            shell_state.command_count++;
        }
    }
}

/**
 * @function shell_stop
 * @brief Stop shell execution
 */
void shell_stop(void)
{
    shell_state.running = false;
}

