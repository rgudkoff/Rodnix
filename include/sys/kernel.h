#ifndef _RODNIX_COMPAT_SYS_KERNEL_H
#define _RODNIX_COMPAT_SYS_KERNEL_H

/* Common startup ordering constants used by the compatibility subset. */
#define SI_SUB_DRIVERS 0x3000000
#define SI_SUB_CONFIGURE 0x3800000
#define SI_ORDER_FIRST 0
#define SI_ORDER_MIDDLE 0x1000000
#define SI_ORDER_ANY 0x8000000

#define SYSINIT(_name, _sub, _order, _func, _arg)

#endif /* _RODNIX_COMPAT_SYS_KERNEL_H */
