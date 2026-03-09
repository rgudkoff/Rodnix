#ifndef _RODNIX_COMPAT_NET_BPF_H
#define _RODNIX_COMPAT_NET_BPF_H
#define BPF_MTAP(_ifp,_m) do { (void)(_ifp); (void)(_m); } while (0)
#endif
