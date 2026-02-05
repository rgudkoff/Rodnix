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
#include "../core/boot.h"
#include "../fs/vfs.h"
#include "../../include/console.h"
#include "../../include/debug.h"
#include "../../include/common.h"
#include "../common/scheduler.h"
#include "../core/interrupts.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Shell Constants
 * ============================================================================ */

#define SHELL_MAX_LINE_LENGTH  256
#define SHELL_MAX_ARGS         16
#define SHELL_PROMPT           "  rodnix> "

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
    kputs("  mem       - Alias for memory\n");
    kputs("  timer     - Show timer information\n");
    kputs("  echo      - Echo arguments\n");
    kputs("  sched     - Show scheduler statistics\n");
    kputs("  exit      - Exit shell (reboot)\n");
    kputs("\n");
    
    return 0;
}

/**
 * @function shell_cmd_sched
 * @brief Show scheduler statistics
 */
static int shell_cmd_sched(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    extern int scheduler_get_stats(scheduler_stats_t* out_stats);
    scheduler_stats_t stats;
    if (scheduler_get_stats(&stats) != 0) {
        kputs("scheduler stats unavailable\n");
        return -1;
    }

    kprintf("Scheduler Stats:\n");
    kprintf("  total_switches: %llu\n", (unsigned long long)stats.total_switches);
    kprintf("  total_tasks:    %llu\n", (unsigned long long)stats.total_tasks);
    kprintf("  running_tasks:  %llu\n", (unsigned long long)stats.running_tasks);
    kprintf("  ready_tasks:    %llu\n", (unsigned long long)stats.ready_tasks);
    kprintf("  blocked_tasks:  %llu\n", (unsigned long long)stats.blocked_tasks);
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
    extern int pmm_get_zone_stats(int zone, void* out);
    extern int pmm_get_free_regions(int zone, void* out, uint32_t max, uint32_t* out_count);
    extern int pmm_get_usable_regions(void* out, uint32_t max, uint32_t* out_count);
    extern int pmm_get_reserved_regions(void* out, uint32_t max, uint32_t* out_count);
    extern boot_info_t* boot_get_info(void);
    
    uint64_t total = pmm_get_total_pages();
    uint64_t free = pmm_get_free_pages();
    uint64_t used = pmm_get_used_pages();
    boot_info_t* bi = boot_get_info();

    typedef struct {
        uint64_t total_pages;
        uint64_t free_pages;
        uint64_t used_pages;
    } pmm_zone_stats_t;

    typedef struct {
        uint64_t base;
        uint64_t length;
    } pmm_region_t;

    enum { PMM_ZONE_LOW = 0, PMM_ZONE_NORMAL = 1, PMM_ZONE_MMIO = 2, PMM_ZONE_COUNT = 3 };

    const char* zone_names[] = { "low", "normal", "mmio" };
    
    kprintf("Physical Memory:\n");
    kprintf("  Total: %llu pages (%llu KB)\n", total, (total * 4));
    kprintf("  Free:  %llu pages (%llu KB)\n", free, (free * 4));
    kprintf("  Used:  %llu pages (%llu KB)\n", used, (used * 4));
    if (bi) {
        kprintf("Boot Memory Info:\n");
        kprintf("  Usable (MB2): %llu KB\n", (unsigned long long)(bi->mem_lower / 1024ULL));
        kprintf("  Max Address:  %llu KB\n", (unsigned long long)(bi->mem_upper / 1024ULL));
        kprintf("  MMAP Tag:     %p\n", bi->mmap_addr);
        kprintf("  MMAP Size:    %u bytes\n", bi->mmap_size);
        kprintf("  Entry Size:   %u bytes\n", bi->mmap_entry_size);
    }

    kprintf("Free Ranges:\n");
    for (int z = 0; z < PMM_ZONE_COUNT; z++) {
        pmm_zone_stats_t stats;
        if (pmm_get_zone_stats(z, &stats) != 0) {
            continue;
        }
        kprintf("  Zone %s: total=%llu free=%llu used=%llu pages\n",
                zone_names[z],
                (unsigned long long)stats.total_pages,
                (unsigned long long)stats.free_pages,
                (unsigned long long)stats.used_pages);

        uint32_t total_ranges = 0;
        pmm_get_free_regions(z, NULL, 0, &total_ranges);
        if (total_ranges == 0) {
            kprintf("    (no free ranges)\n");
            continue;
        }
        uint32_t show = (total_ranges > 8) ? 8 : total_ranges;
        pmm_region_t regions[8];
        uint32_t returned = 0;
        if (pmm_get_free_regions(z, regions, show, &returned) != 0) {
            continue;
        }
        for (uint32_t i = 0; i < returned; i++) {
            kprintf("    [%u] base=%llx len=%llx\n",
                    i,
                    (unsigned long long)regions[i].base,
                    (unsigned long long)regions[i].length);
        }
        if (total_ranges > show) {
            kprintf("    ... (%u more)\n", (unsigned)(total_ranges - show));
        }
    }

    kprintf("Regions:\n");
    uint32_t usable_total = 0;
    uint32_t reserved_total = 0;
    pmm_get_usable_regions(NULL, 0, &usable_total);
    pmm_get_reserved_regions(NULL, 0, &reserved_total);
    kprintf("  Usable:   %u\n", (unsigned)usable_total);
    kprintf("  Reserved: %u\n", (unsigned)reserved_total);

    pmm_region_t regions[6];
    uint32_t returned = 0;
    if (usable_total > 0) {
        if (pmm_get_usable_regions(regions, 5, &returned) == 0) {
            kprintf("  Usable (first %u):\n", (unsigned)returned);
            for (uint32_t i = 0; i < returned; i++) {
                kprintf("    [%u] base=%llx len=%llx\n",
                        i,
                        (unsigned long long)regions[i].base,
                        (unsigned long long)regions[i].length);
            }
        }
    }
    returned = 0;
    if (reserved_total > 0) {
        if (pmm_get_reserved_regions(regions, 5, &returned) == 0) {
            kprintf("  Reserved (first %u):\n", (unsigned)returned);
            for (uint32_t i = 0; i < returned; i++) {
                kprintf("    [%u] base=%llx len=%llx\n",
                        i,
                        (unsigned long long)regions[i].base,
                        (unsigned long long)regions[i].length);
            }
        }
    }
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
        if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            const char* path = argv[i + 1];
            extern int vfs_open(const char* path, int flags, vfs_file_t* out_file);
            extern int vfs_write(vfs_file_t* file, const void* buffer, size_t size);
            extern int vfs_close(vfs_file_t* file);

            vfs_file_t file;
            if (vfs_open(path, VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNC, &file) != 0) {
                kputs("echo: open failed\n");
                return -1;
            }
            for (int j = 1; j < i; j++) {
                vfs_write(&file, argv[j], strlen(argv[j]));
                if (j < i - 1) {
                    char space = ' ';
                    vfs_write(&file, &space, 1);
                }
            }
            vfs_close(&file);
            return 0;
        }
    }

    for (int i = 1; i < argc; i++) {
        kputs(argv[i]);
        if (i < argc - 1) {
            kputc(' ');
        }
    }
    kputc('\n');
    return 0;
}

static void shell_ls_cb(const vfs_node_t* node, void* ctx)
{
    (void)ctx;
    if (node->type == VFS_NODE_DIR) {
        kprintf("%s/\n", node->name);
    } else {
        kprintf("%s\n", node->name);
    }
}

static int shell_cmd_ls(int argc, char** argv)
{
    const char* path = "/";
    if (argc > 1) {
        path = argv[1];
    }

    if (!vfs_is_ready()) {
        kputs("VFS not initialized\n");
        return -1;
    }
    if (vfs_list_dir(path, shell_ls_cb, NULL) != 0) {
        kputs("ls: failed to list directory\n");
        return -1;
    }
    return 0;
}

static int shell_cmd_cat(int argc, char** argv)
{
    if (argc < 2) {
        kputs("Usage: cat <file>\n");
        return -1;
    }

    extern int vfs_open(const char* path, int flags, vfs_file_t* out_file);
    extern int vfs_read(vfs_file_t* file, void* buffer, size_t size);
    extern int vfs_close(vfs_file_t* file);

    vfs_file_t file;
    if (vfs_open(argv[1], VFS_OPEN_READ | VFS_OPEN_CREATE, &file) != 0) {
        kputs("cat: open failed\n");
        return -1;
    }

    char buf[128];
    for (;;) {
        int r = vfs_read(&file, buf, sizeof(buf) - 1);
        if (r <= 0) {
            break;
        }
        buf[r] = '\0';
        kputs(buf);
    }
    kputc('\n');
    vfs_close(&file);
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
    {"sched",   shell_cmd_sched,   "Show scheduler statistics"},
    {"clear",   shell_cmd_clear,    "Clear the screen"},
    {"info",    shell_cmd_info,    "Show system information"},
    {"memory",  shell_cmd_memory,  "Show memory statistics"},
    {"mem",     shell_cmd_memory,  "Alias for memory"},
    {"timer",   shell_cmd_timer,   "Show timer information"},
    {"echo",    shell_cmd_echo,    "Echo arguments (supports: echo ... > file)"},
    {"ls",      shell_cmd_ls,      "List directory"},
    {"cat",     shell_cmd_cat,     "Show file contents"},
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
    
    /* Disable log prefixes for interactive shell output */
    extern void console_set_log_prefix_enabled(bool enabled);
    console_set_log_prefix_enabled(false);
    extern void input_flush(void);
    input_flush();

    kputs("\nRodNIX Shell v0.1\n");
    kputs("Type 'help' for available commands.\n\n");
    __asm__ volatile ("" ::: "memory");
    
    while (shell_state.running) {
        interrupts_enable();
        interrupts_disable();
        kputs(SHELL_PROMPT);
        interrupts_enable();
        __asm__ volatile ("" ::: "memory"); /* Ensure prompt is flushed */

        
        /* Read command line */
        size_t len = input_read_line(line, SHELL_MAX_LINE_LENGTH);
        
        if (len == 0) {
            continue; /* Empty line */
        }
        
        /* Parse command */
        int argc = shell_parse_command(line, argv, SHELL_MAX_ARGS);
        
        if (argc > 0) {
            /* Execute command */
            shell_execute_command(argc, argv);
            shell_state.command_count++;
        }
        scheduler_ast_check();
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
