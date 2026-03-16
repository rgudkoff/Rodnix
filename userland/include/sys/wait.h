#ifndef _RODNIX_USERLAND_SYS_WAIT_H
#define _RODNIX_USERLAND_SYS_WAIT_H

#include <sys/types.h>

#define _W_INT(i) ((i))
#define _WSTATUS(x) (_W_INT(x) & 0177)
#define _WSTOPPED 0177

#define WIFSTOPPED(x) (_WSTATUS(x) == _WSTOPPED)
#define WSTOPSIG(x) (_W_INT(x) >> 8)
#define WIFSIGNALED(x) (_WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0 && (x) != 0x13)
#define WTERMSIG(x) (_WSTATUS(x))
#define WIFEXITED(x) (_WSTATUS(x) == 0)
#define WEXITSTATUS(x) (_W_INT(x) >> 8)
#define WIFCONTINUED(x) ((x) == 0x13)

#define WNOHANG   1
#define WUNTRACED 2
#define WSTOPPED  WUNTRACED
#define WCONTINUED 4
#define WNOWAIT   8
#define WEXITED   16
#define WTRAPPED  32

#endif /* _RODNIX_USERLAND_SYS_WAIT_H */
