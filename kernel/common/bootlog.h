/**
 * @file bootlog.h
 * @brief Boot phase logging helpers + runtime-filtered kernel log.
 */

#ifndef _RODNIX_COMMON_BOOTLOG_H
#define _RODNIX_COMMON_BOOTLOG_H

#include <stdarg.h>

/* ── Log levels ─────────────────────────────────────────────────────────── */
enum {
    KLOG_DEBUG = 0,
    KLOG_INFO  = 1,
    KLOG_WARN  = 2,
    KLOG_ERROR = 3
};

void bootlog_init(void);
void bootlog_mark(const char* phase, const char* event);
int  bootlog_is_verbose(void);

/* Runtime log-level control (default: KLOG_INFO). */
void klog_set_level(int level);
int  klog_get_level(void);

/**
 * klog_at — core timestamped log function.
 *   [  SS.MMM] LEVEL subsystem  : message
 * Messages below the current log level are suppressed.
 */
void klog_at(int level, const char* subsys, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Convenience wrappers — keep klog() as INFO alias for backward compat. */
#define klog(subsys, fmt, ...)  klog_at(KLOG_INFO,  (subsys), fmt, ##__VA_ARGS__)
#define klog_dbg(subsys, fmt, ...) klog_at(KLOG_DEBUG, (subsys), fmt, ##__VA_ARGS__)
#define klog_warn(subsys, fmt, ...) klog_at(KLOG_WARN,  (subsys), fmt, ##__VA_ARGS__)
#define klog_err(subsys, fmt, ...)  klog_at(KLOG_ERROR, (subsys), fmt, ##__VA_ARGS__)

#endif /* _RODNIX_COMMON_BOOTLOG_H */
