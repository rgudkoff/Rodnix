#ifndef _RODNIX_COMPAT_MACHINE_IN_CKSUM_H
#define _RODNIX_COMPAT_MACHINE_IN_CKSUM_H
#include "../../net/bsd_inet.h"
#define in_cksum(_m,_l) bsd_in_cksum((_m), (_l))
#endif
