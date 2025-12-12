/**
 * @file shell.h
 * @brief Shell interface
 */

#ifndef _RODNIX_COMMON_SHELL_H
#define _RODNIX_COMMON_SHELL_H

/* Initialize shell */
int shell_init(void);

/* Run shell */
void shell_run(void);

/* Stop shell */
void shell_stop(void);

#endif /* _RODNIX_COMMON_SHELL_H */

