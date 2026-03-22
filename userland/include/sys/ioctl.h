#ifndef _RODNIX_USERLAND_SYS_IOCTL_H
#define _RODNIX_USERLAND_SYS_IOCTL_H

/*
 * Minimal RodNIX TTY ioctl command set.
 */
#define RDNX_TTY_IOCTL_ISATTY  0x7401u
#define RDNX_TTY_IOCTL_GETATTR 0x7402u
#define RDNX_TTY_IOCTL_SETATTR 0x7403u
#define RDNX_TTY_IOCTL_GETWINSZ 0x7404u
#define RDNX_TTY_IOCTL_SETWINSZ 0x7405u

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#endif /* _RODNIX_USERLAND_SYS_IOCTL_H */
