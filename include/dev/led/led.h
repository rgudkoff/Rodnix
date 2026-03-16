#ifndef _RODNIX_COMPAT_DEV_LED_LED_H
#define _RODNIX_COMPAT_DEV_LED_LED_H
static inline void* led_create_state(void (*func)(void*, int), void* arg, const char* name, const char* state)
{ (void)func; (void)arg; (void)name; (void)state; return (void*)0; }
#endif
