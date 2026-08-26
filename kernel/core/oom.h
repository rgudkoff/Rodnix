#ifndef _RODNIX_CORE_OOM_H
#define _RODNIX_CORE_OOM_H

/* Один шаг протокола убийства по полосам: возврат 1 — сделано что-то,
 * стоит повторить выделение; 0 — сделать нечего (или жертва — вызывающий,
 * и его выделению положен отказ). */
int oom_kill_step(void);

#endif /* _RODNIX_CORE_OOM_H */
