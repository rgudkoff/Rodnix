/**
 * @file bootlog.h
 * @brief Boot phase logging helpers.
 */

#ifndef _RODNIX_COMMON_BOOTLOG_H
#define _RODNIX_COMMON_BOOTLOG_H

void bootlog_init(void);
void bootlog_mark(const char* phase, const char* event);
int bootlog_is_verbose(void);

#endif /* _RODNIX_COMMON_BOOTLOG_H */
