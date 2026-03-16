#ifndef _RODNIX_USERLAND_LIMITS_H
#define _RODNIX_USERLAND_LIMITS_H

/* Integer type limits */
#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX
#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535
#define INT_MIN     (-2147483648)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295U
#define LONG_MIN    (-9223372036854775807L - 1L)
#define LONG_MAX    9223372036854775807L
#define ULONG_MAX   18446744073709551615UL
#define LLONG_MIN   (-9223372036854775807LL - 1LL)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615ULL

/* POSIX / FreeBSD-aligned path/name constants */
#define _POSIX2_LINE_MAX       2048
#define _POSIX_LOGIN_NAME_MAX  9
#define _POSIX_PATH_MAX        256
#define _POSIX_NAME_MAX        14

#define LINE_MAX               2048
#define NAME_MAX               255
#define PATH_MAX               1024

#define MB_LEN_MAX             6

#endif /* _RODNIX_USERLAND_LIMITS_H */
