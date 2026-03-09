#ifndef _RODNIX_COMPAT_SYS_TASKQUEUE_H
#define _RODNIX_COMPAT_SYS_TASKQUEUE_H
struct task { int dummy; };
struct taskqueue { int dummy; };
#define TASK_INIT(_t,_p,_f,_a) do { (void)(_t); } while (0)
#endif
