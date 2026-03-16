#ifndef _RODNIX_COMPAT_SYS_SBUF_H
#define _RODNIX_COMPAT_SYS_SBUF_H

struct sbuf { int dummy; };

static inline struct sbuf* sbuf_new_auto(void) { return (struct sbuf*)0; }
static inline int sbuf_printf(struct sbuf* s, const char* fmt, ...) { (void)s; (void)fmt; return 0; }
static inline int sbuf_finish(struct sbuf* s) { (void)s; return 0; }
static inline char* sbuf_data(struct sbuf* s) { (void)s; return (char*)""; }
static inline void sbuf_delete(struct sbuf* s) { (void)s; }

#endif
