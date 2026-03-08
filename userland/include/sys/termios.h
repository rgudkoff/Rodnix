#ifndef _RODNIX_USERLAND_SYS_TERMIOS_H
#define _RODNIX_USERLAND_SYS_TERMIOS_H

#include <sys/types.h>

typedef uint32_t tcflag_t;
typedef uint8_t cc_t;
typedef uint32_t speed_t;

#define NCCS 20

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush queue selectors */
#define TCIFLUSH  1
#define TCOFLUSH  2
#define TCIOFLUSH 3

/* tcflow actions */
#define TCOOFF 1
#define TCOON  2
#define TCIOFF 3
#define TCION  4

/* local modes (subset) */
#define ECHO   0x00000008u
#define ICANON 0x00000100u

#endif /* _RODNIX_USERLAND_SYS_TERMIOS_H */
