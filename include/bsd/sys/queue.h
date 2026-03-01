/*
 * BSD-style queue macros (minimal subset for Rodnix scheduler).
 * This header intentionally implements a small TAILQ-compatible API:
 * TAILQ_HEAD, TAILQ_ENTRY, TAILQ_INIT, TAILQ_EMPTY, TAILQ_FIRST,
 * TAILQ_NEXT, TAILQ_INSERT_TAIL, TAILQ_REMOVE, TAILQ_FOREACH.
 */

#ifndef _RODNIX_BSD_SYS_QUEUE_H_
#define _RODNIX_BSD_SYS_QUEUE_H_

#define TAILQ_HEAD(name, type) \
struct name { \
    struct type* tqh_first; \
    struct type** tqh_last; \
}

#define TAILQ_ENTRY(type) \
struct { \
    struct type* tqe_next; \
    struct type** tqe_prev; \
}

#define TAILQ_INIT(head) do { \
    (head)->tqh_first = 0; \
    (head)->tqh_last = &((head)->tqh_first); \
} while (0)

#define TAILQ_EMPTY(head) ((head)->tqh_first == 0)
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define TAILQ_INSERT_TAIL(head, elm, field) do { \
    (elm)->field.tqe_next = 0; \
    (elm)->field.tqe_prev = (head)->tqh_last; \
    *((head)->tqh_last) = (elm); \
    (head)->tqh_last = &((elm)->field.tqe_next); \
} while (0)

#define TAILQ_REMOVE(head, elm, field) do { \
    if (((elm)->field.tqe_next) != 0) { \
        (elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev; \
    } else { \
        (head)->tqh_last = (elm)->field.tqe_prev; \
    } \
    *((elm)->field.tqe_prev) = (elm)->field.tqe_next; \
    (elm)->field.tqe_next = 0; \
    (elm)->field.tqe_prev = 0; \
} while (0)

#define TAILQ_FOREACH(var, head, field) \
    for ((var) = TAILQ_FIRST(head); (var) != 0; (var) = TAILQ_NEXT(var, field))

#endif /* _RODNIX_BSD_SYS_QUEUE_H_ */
