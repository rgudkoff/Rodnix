/**
 * @file runtime.c
 * @brief Kernel runtime handoff into userspace or shell
 */

#include "../include/kernel.h"
#include "../kernel/arch/percpu.h"
#include "../include/common.h"
#include "../include/console.h"
#include "../include/debug.h"
#include "../include/version.h"
#include "../trace/bootlog.h"
#include "../trace/bootstrap.h"
#include "../kernel/loader.h"
#include "../shell/shell.h"
#include "../kernel/core/boot.h"
#include "../kernel/posix/posix_syscall.h"
#include "../include/gfx.h"

#define USER_INIT_PATH_MAX 128

static char g_user_init_path[USER_INIT_PATH_MAX] = "/bin/init";

static bool
bootarg_has_token(const char* cmdline, const char* token)
{
    if (!cmdline || !token || token[0] == '\0') {
        return false;
    }

    size_t tok_len = strlen(token);
    const char* p = cmdline;

    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == tok_len && strncmp(start, token, tok_len) == 0) {
            return true;
        }
    }

    return false;
}

static void
bootarg_pick_init_path(char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }

    const char* fallback = "/bin/init";
    size_t i = 0;
    while (fallback[i] && i + 1 < out_len) {
        out[i] = fallback[i];
        i++;
    }
    out[i] = '\0';

    boot_info_t* bi = boot_get_info();
    if (!bi || !bi->cmdline[0]) {
        return;
    }

    const char* cmdline = bi->cmdline;
    const char* key = "rdnx.init=";
    size_t key_len = strlen(key);
    const char* p = cmdline;

    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len > key_len && strncmp(start, key, key_len) == 0) {
            size_t path_len = len - key_len;
            if (path_len >= out_len) {
                path_len = out_len - 1;
            }
            memcpy(out, start + key_len, path_len);
            out[path_len] = '\0';
            return;
        }
    }
}

static void
idle_thread(void* arg)
{
    (void)arg;
    for (;;) {
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
}

static void
kernel_shell_thread(void* arg)
{
    (void)arg;
    if (shell_init() != 0) {
        panic("Shell init failed");
    }
    shell_run();
    for (;;) {
        cpu_idle();
    }
}

static void
user_init_thread(void* arg)
{
    const char* init_path = (const char*)arg;
    if (!init_path || init_path[0] == '\0') {
        init_path = "/bin/init";
    }

    klog("init", "exec %s\n", init_path);
    int ret = loader_exec(init_path);
    if (ret == 0) {
        for (;;) {
            cpu_idle();
        }
    }

    klog("init", "exec failed (%d), starting kernel shell\n", ret);
    if (shell_init() != 0) {
        panic("Shell fallback init failed");
    }
    shell_run();
    for (;;) {
        cpu_idle();
    }
}

static void
attach_gfx_console(void)
{
    extern void gfx_console_attach(gfx_display_t *disp);
    extern void gfx_console_putc(char c);
    extern void gfx_console_blink_tick(void);
    extern void console_set_gfx_putc(void (*fn)(char c));
    extern void console_set_gfx_blink(void (*fn)(void));

    console_set_log_prefix_enabled(false);

    gfx_display_t* gfx_primary = gfx_display_primary();
    if (gfx_primary) {
        gfx_console_attach(gfx_primary);
        console_set_gfx_putc(gfx_console_putc);
        console_set_gfx_blink(gfx_console_blink_tick);
    }
}

void
kernel_enter_runtime(void)
{
    bool force_kernel_shell = false;
    boot_info_t* boot_cfg = boot_get_info();
    if (boot_cfg) {
        force_kernel_shell =
            bootarg_has_token(boot_cfg->cmdline, "rdnx.shell=1") ||
            bootarg_has_token(boot_cfg->cmdline, "shell=1");
    }
    bootarg_pick_init_path(g_user_init_path, sizeof(g_user_init_path));

    bootlog_mark("shell", "enter");
    __asm__ volatile ("" ::: "memory");

    bootlog_mark("threads", "create_enter");
    __asm__ volatile ("" ::: "memory");

    task_t* kernel_task = task_create();
    if (!kernel_task) {
        bootlog_mark("threads", "kernel_task_fail");
        panic("Kernel task create failed");
    }
    kernel_task->state = TASK_STATE_READY;
    task_set_current(kernel_task);

    thread_t* primary = NULL;
    if (force_kernel_shell) {
        klog("init", "kernel shell forced by boot arg\n");
        primary = thread_create(kernel_task, kernel_shell_thread, NULL);
    } else {
        klog("init", "userspace target: %s\n", g_user_init_path);
        task_t* init_task = task_create();
        if (!init_task) {
            bootlog_mark("threads", "kernel_task_fail");
            panic("Init task create failed");
        }
        init_task->state = TASK_STATE_READY;
        if (posix_bind_stdio_to_console(init_task) != 0) {
            bootlog_mark("threads", "thread_create_fail");
            panic("Init stdio bind failed");
        }
        primary = thread_create(init_task, user_init_thread, (void*)g_user_init_path);
    }

    /* This processor's idle thread. Deliberately not added to the scheduler:
     * it belongs to this CPU alone. A shared idle thread in the run queue
     * would be handed to whichever processor asked first, leaving the others
     * with nothing to return to -- which is precisely the hole that stopped
     * application processors from running anything. */
    thread_t* idle = thread_create(kernel_task, idle_thread, NULL);
    if (idle) {
        idle->priority = PRIORITY_MIN;
        scheduler_mark_runnable_unqueued(idle);
        percpu_self()->sched_idle = idle;
    }

    bootstrap_start();

    if (!primary || !idle) {
        bootlog_mark("threads", "thread_create_fail");
        panic("Kernel thread create failed");
    }

    scheduler_add_thread(primary);
    bootlog_mark("threads", "created");

    klog("kernel", "boot complete — starting scheduler\n");
    kputs("\n");

    attach_gfx_console();

    bootlog_mark("scheduler", "start");
    __asm__ volatile ("" ::: "memory");
    scheduler_start();

    for (;;) {
        interrupt_wait();
    }
}
