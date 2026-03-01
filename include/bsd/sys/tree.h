/*
 * BSD-style red-black tree primitives for Rodnix.
 *
 * Compatibility goal: provide the common RB_* API surface used in BSD kernels
 * (RB_HEAD/RB_ENTRY/RB_PROTOTYPE/RB_GENERATE and helpers).
 */

#ifndef _RODNIX_BSD_SYS_TREE_H_
#define _RODNIX_BSD_SYS_TREE_H_

#define RB_RED   0
#define RB_BLACK 1

#define RB_HEAD(name, type) \
struct name { \
    struct type* rbh_root; \
}

#define RB_INITIALIZER(root) { 0 }
#define RB_INIT(root) do { \
    RB_ROOT(root) = 0; \
} while (0)

#define RB_ENTRY(type) \
struct { \
    struct type* rbe_left; \
    struct type* rbe_right; \
    struct type* rbe_parent; \
    int rbe_color; \
}

#define RB_ROOT(head) ((head)->rbh_root)
#define RB_EMPTY(head) (RB_ROOT(head) == 0)

#define RB_LEFT(elm, field) ((elm)->field.rbe_left)
#define RB_RIGHT(elm, field) ((elm)->field.rbe_right)
#define RB_PARENT(elm, field) ((elm)->field.rbe_parent)
#define RB_COLOR(elm, field) ((elm)->field.rbe_color)
#define RB_SET(elm, parent, field) do { \
    RB_PARENT((elm), field) = (parent); \
    RB_LEFT((elm), field) = 0; \
    RB_RIGHT((elm), field) = 0; \
    RB_COLOR((elm), field) = RB_RED; \
} while (0)
#define RB_SET_BLACKRED(black, red, field) do { \
    RB_COLOR((black), field) = RB_BLACK; \
    RB_COLOR((red), field) = RB_RED; \
} while (0)

#define RB_AUGMENT(x) do { } while (0)

#define RB_ROTATE_LEFT(head, elm, tmp, field) do { \
    (tmp) = RB_RIGHT((elm), field); \
    if ((RB_RIGHT((elm), field) = RB_LEFT((tmp), field)) != 0) { \
        RB_PARENT(RB_LEFT((tmp), field), field) = (elm); \
    } \
    RB_AUGMENT(elm); \
    if ((RB_PARENT((tmp), field) = RB_PARENT((elm), field)) != 0) { \
        if ((elm) == RB_LEFT(RB_PARENT((elm), field), field)) { \
            RB_LEFT(RB_PARENT((elm), field), field) = (tmp); \
        } else { \
            RB_RIGHT(RB_PARENT((elm), field), field) = (tmp); \
        } \
    } else { \
        RB_ROOT((head)) = (tmp); \
    } \
    RB_LEFT((tmp), field) = (elm); \
    RB_PARENT((elm), field) = (tmp); \
    RB_AUGMENT(elm); \
    RB_AUGMENT(tmp); \
    if ((RB_PARENT((tmp), field)) != 0) { \
        RB_AUGMENT(RB_PARENT((tmp), field)); \
    } \
} while (0)

#define RB_ROTATE_RIGHT(head, elm, tmp, field) do { \
    (tmp) = RB_LEFT((elm), field); \
    if ((RB_LEFT((elm), field) = RB_RIGHT((tmp), field)) != 0) { \
        RB_PARENT(RB_RIGHT((tmp), field), field) = (elm); \
    } \
    RB_AUGMENT(elm); \
    if ((RB_PARENT((tmp), field) = RB_PARENT((elm), field)) != 0) { \
        if ((elm) == RB_LEFT(RB_PARENT((elm), field), field)) { \
            RB_LEFT(RB_PARENT((elm), field), field) = (tmp); \
        } else { \
            RB_RIGHT(RB_PARENT((elm), field), field) = (tmp); \
        } \
    } else { \
        RB_ROOT((head)) = (tmp); \
    } \
    RB_RIGHT((tmp), field) = (elm); \
    RB_PARENT((elm), field) = (tmp); \
    RB_AUGMENT(elm); \
    RB_AUGMENT(tmp); \
    if ((RB_PARENT((tmp), field)) != 0) { \
        RB_AUGMENT(RB_PARENT((tmp), field)); \
    } \
} while (0)

#define RB_PROTOTYPE(name, type, field, cmp) \
RB_PROTOTYPE_INTERNAL(name, type, field, cmp,)

#define RB_PROTOTYPE_STATIC(name, type, field, cmp) \
RB_PROTOTYPE_INTERNAL(name, type, field, cmp, static __attribute__((unused)))

#define RB_PROTOTYPE_INTERNAL(name, type, field, cmp, attr) \
attr struct type* name##_RB_INSERT(struct name*, struct type*); \
attr struct type* name##_RB_REMOVE(struct name*, struct type*); \
attr struct type* name##_RB_FIND(struct name*, struct type*); \
attr struct type* name##_RB_NFIND(struct name*, struct type*); \
attr struct type* name##_RB_NEXT(struct type*); \
attr struct type* name##_RB_PREV(struct type*); \
attr struct type* name##_RB_MINMAX(struct name*, int)

#define RB_GENERATE(name, type, field, cmp) \
RB_GENERATE_INTERNAL(name, type, field, cmp,)

#define RB_GENERATE_STATIC(name, type, field, cmp) \
RB_GENERATE_INTERNAL(name, type, field, cmp, static __attribute__((unused)))

#define RB_GENERATE_INTERNAL(name, type, field, cmp, attr) \
attr void name##_RB_INSERT_COLOR(struct name* head, struct type* elm) \
{ \
    struct type* parent, *gparent, *tmp; \
    while ((parent = RB_PARENT(elm, field)) != 0 && \
           RB_COLOR(parent, field) == RB_RED) { \
        gparent = RB_PARENT(parent, field); \
        if (parent == RB_LEFT(gparent, field)) { \
            tmp = RB_RIGHT(gparent, field); \
            if (tmp != 0 && RB_COLOR(tmp, field) == RB_RED) { \
                RB_COLOR(tmp, field) = RB_BLACK; \
                RB_SET_BLACKRED(parent, gparent, field); \
                elm = gparent; \
                continue; \
            } \
            if (RB_RIGHT(parent, field) == elm) { \
                RB_ROTATE_LEFT(head, parent, tmp, field); \
                tmp = parent; \
                parent = elm; \
                elm = tmp; \
            } \
            RB_SET_BLACKRED(parent, gparent, field); \
            RB_ROTATE_RIGHT(head, gparent, tmp, field); \
        } else { \
            tmp = RB_LEFT(gparent, field); \
            if (tmp != 0 && RB_COLOR(tmp, field) == RB_RED) { \
                RB_COLOR(tmp, field) = RB_BLACK; \
                RB_SET_BLACKRED(parent, gparent, field); \
                elm = gparent; \
                continue; \
            } \
            if (RB_LEFT(parent, field) == elm) { \
                RB_ROTATE_RIGHT(head, parent, tmp, field); \
                tmp = parent; \
                parent = elm; \
                elm = tmp; \
            } \
            RB_SET_BLACKRED(parent, gparent, field); \
            RB_ROTATE_LEFT(head, gparent, tmp, field); \
        } \
    } \
    RB_COLOR(RB_ROOT(head), field) = RB_BLACK; \
} \
attr void name##_RB_REMOVE_COLOR(struct name* head, struct type* parent, struct type* elm) \
{ \
    struct type* tmp; \
    while ((elm == 0 || RB_COLOR(elm, field) == RB_BLACK) && elm != RB_ROOT(head)) { \
        if (RB_LEFT(parent, field) == elm) { \
            tmp = RB_RIGHT(parent, field); \
            if (tmp != 0 && RB_COLOR(tmp, field) == RB_RED) { \
                RB_SET_BLACKRED(tmp, parent, field); \
                RB_ROTATE_LEFT(head, parent, tmp, field); \
                tmp = RB_RIGHT(parent, field); \
            } \
            if (tmp == 0 || \
                ((RB_LEFT(tmp, field) == 0 || RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) && \
                 (RB_RIGHT(tmp, field) == 0 || RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK))) { \
                if (tmp != 0) { \
                    RB_COLOR(tmp, field) = RB_RED; \
                } \
                elm = parent; \
                parent = RB_PARENT(elm, field); \
            } else { \
                if (RB_RIGHT(tmp, field) == 0 || RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK) { \
                    struct type* oleft; \
                    if ((oleft = RB_LEFT(tmp, field)) != 0) { \
                        RB_COLOR(oleft, field) = RB_BLACK; \
                    } \
                    RB_COLOR(tmp, field) = RB_RED; \
                    RB_ROTATE_RIGHT(head, tmp, oleft, field); \
                    tmp = RB_RIGHT(parent, field); \
                } \
                RB_COLOR(tmp, field) = RB_COLOR(parent, field); \
                RB_COLOR(parent, field) = RB_BLACK; \
                if (RB_RIGHT(tmp, field) != 0) { \
                    RB_COLOR(RB_RIGHT(tmp, field), field) = RB_BLACK; \
                } \
                RB_ROTATE_LEFT(head, parent, tmp, field); \
                elm = RB_ROOT(head); \
            } \
        } else { \
            tmp = RB_LEFT(parent, field); \
            if (tmp != 0 && RB_COLOR(tmp, field) == RB_RED) { \
                RB_SET_BLACKRED(tmp, parent, field); \
                RB_ROTATE_RIGHT(head, parent, tmp, field); \
                tmp = RB_LEFT(parent, field); \
            } \
            if (tmp == 0 || \
                ((RB_LEFT(tmp, field) == 0 || RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) && \
                 (RB_RIGHT(tmp, field) == 0 || RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK))) { \
                if (tmp != 0) { \
                    RB_COLOR(tmp, field) = RB_RED; \
                } \
                elm = parent; \
                parent = RB_PARENT(elm, field); \
            } else { \
                if (RB_LEFT(tmp, field) == 0 || RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) { \
                    struct type* oright; \
                    if ((oright = RB_RIGHT(tmp, field)) != 0) { \
                        RB_COLOR(oright, field) = RB_BLACK; \
                    } \
                    RB_COLOR(tmp, field) = RB_RED; \
                    RB_ROTATE_LEFT(head, tmp, oright, field); \
                    tmp = RB_LEFT(parent, field); \
                } \
                RB_COLOR(tmp, field) = RB_COLOR(parent, field); \
                RB_COLOR(parent, field) = RB_BLACK; \
                if (RB_LEFT(tmp, field) != 0) { \
                    RB_COLOR(RB_LEFT(tmp, field), field) = RB_BLACK; \
                } \
                RB_ROTATE_RIGHT(head, parent, tmp, field); \
                elm = RB_ROOT(head); \
            } \
        } \
    } \
    if (elm != 0) { \
        RB_COLOR(elm, field) = RB_BLACK; \
    } \
} \
attr struct type* name##_RB_REMOVE(struct name* head, struct type* elm) \
{ \
    struct type* child, *parent, *old = elm; \
    int color; \
    if (RB_LEFT(elm, field) == 0) { \
        child = RB_RIGHT(elm, field); \
        parent = RB_PARENT(elm, field); \
        color = RB_COLOR(elm, field); \
        if (child != 0) { \
            RB_PARENT(child, field) = parent; \
        } \
        if (parent != 0) { \
            if (RB_LEFT(parent, field) == elm) { \
                RB_LEFT(parent, field) = child; \
            } else { \
                RB_RIGHT(parent, field) = child; \
            } \
            RB_AUGMENT(parent); \
        } else { \
            RB_ROOT(head) = child; \
        } \
    } else if (RB_RIGHT(elm, field) == 0) { \
        child = RB_LEFT(elm, field); \
        parent = RB_PARENT(elm, field); \
        color = RB_COLOR(elm, field); \
        if (child != 0) { \
            RB_PARENT(child, field) = parent; \
        } \
        if (parent != 0) { \
            if (RB_LEFT(parent, field) == elm) { \
                RB_LEFT(parent, field) = child; \
            } else { \
                RB_RIGHT(parent, field) = child; \
            } \
            RB_AUGMENT(parent); \
        } else { \
            RB_ROOT(head) = child; \
        } \
    } else { \
        struct type* left; \
        elm = RB_RIGHT(elm, field); \
        while ((left = RB_LEFT(elm, field)) != 0) { \
            elm = left; \
        } \
        child = RB_RIGHT(elm, field); \
        parent = RB_PARENT(elm, field); \
        color = RB_COLOR(elm, field); \
        if (child != 0) { \
            RB_PARENT(child, field) = parent; \
        } \
        if (parent != 0) { \
            if (RB_LEFT(parent, field) == elm) { \
                RB_LEFT(parent, field) = child; \
            } else { \
                RB_RIGHT(parent, field) = child; \
            } \
            RB_AUGMENT(parent); \
        } else { \
            RB_ROOT(head) = child; \
        } \
        if (RB_PARENT(elm, field) == old) { \
            parent = elm; \
        } \
        (elm)->field = (old)->field; \
        if (RB_PARENT(old, field) != 0) { \
            if (RB_LEFT(RB_PARENT(old, field), field) == old) { \
                RB_LEFT(RB_PARENT(old, field), field) = elm; \
            } else { \
                RB_RIGHT(RB_PARENT(old, field), field) = elm; \
            } \
            RB_AUGMENT(RB_PARENT(old, field)); \
        } else { \
            RB_ROOT(head) = elm; \
        } \
        RB_PARENT(RB_LEFT(old, field), field) = elm; \
        if (RB_RIGHT(old, field) != 0) { \
            RB_PARENT(RB_RIGHT(old, field), field) = elm; \
        } \
        if (parent != 0) { \
            RB_AUGMENT(parent); \
        } \
    } \
    if (color == RB_BLACK) { \
        name##_RB_REMOVE_COLOR(head, parent, child); \
    } \
    return old; \
} \
attr struct type* name##_RB_INSERT(struct name* head, struct type* elm) \
{ \
    struct type* tmp = RB_ROOT(head); \
    struct type* parent = 0; \
    int comp = 0; \
    while (tmp != 0) { \
        parent = tmp; \
        comp = (cmp)(elm, parent); \
        if (comp < 0) { \
            tmp = RB_LEFT(tmp, field); \
        } else if (comp > 0) { \
            tmp = RB_RIGHT(tmp, field); \
        } else { \
            return tmp; \
        } \
    } \
    RB_SET(elm, parent, field); \
    if (parent != 0) { \
        if (comp < 0) { \
            RB_LEFT(parent, field) = elm; \
        } else { \
            RB_RIGHT(parent, field) = elm; \
        } \
        RB_AUGMENT(parent); \
    } else { \
        RB_ROOT(head) = elm; \
    } \
    name##_RB_INSERT_COLOR(head, elm); \
    return 0; \
} \
attr struct type* name##_RB_FIND(struct name* head, struct type* elm) \
{ \
    struct type* tmp = RB_ROOT(head); \
    int comp; \
    while (tmp != 0) { \
        comp = (cmp)(elm, tmp); \
        if (comp < 0) { \
            tmp = RB_LEFT(tmp, field); \
        } else if (comp > 0) { \
            tmp = RB_RIGHT(tmp, field); \
        } else { \
            return tmp; \
        } \
    } \
    return 0; \
} \
attr struct type* name##_RB_NFIND(struct name* head, struct type* elm) \
{ \
    struct type* tmp = RB_ROOT(head); \
    struct type* res = 0; \
    int comp; \
    while (tmp != 0) { \
        comp = (cmp)(elm, tmp); \
        if (comp < 0) { \
            res = tmp; \
            tmp = RB_LEFT(tmp, field); \
        } else if (comp > 0) { \
            tmp = RB_RIGHT(tmp, field); \
        } else { \
            return tmp; \
        } \
    } \
    return res; \
} \
attr struct type* name##_RB_NEXT(struct type* elm) \
{ \
    if (RB_RIGHT(elm, field) != 0) { \
        elm = RB_RIGHT(elm, field); \
        while (RB_LEFT(elm, field) != 0) { \
            elm = RB_LEFT(elm, field); \
        } \
    } else { \
        if (RB_PARENT(elm, field) && \
            elm == RB_LEFT(RB_PARENT(elm, field), field)) { \
            elm = RB_PARENT(elm, field); \
        } else { \
            while (RB_PARENT(elm, field) && \
                elm == RB_RIGHT(RB_PARENT(elm, field), field)) { \
                elm = RB_PARENT(elm, field); \
            } \
            elm = RB_PARENT(elm, field); \
        } \
    } \
    return elm; \
} \
attr struct type* name##_RB_PREV(struct type* elm) \
{ \
    if (RB_LEFT(elm, field) != 0) { \
        elm = RB_LEFT(elm, field); \
        while (RB_RIGHT(elm, field) != 0) { \
            elm = RB_RIGHT(elm, field); \
        } \
    } else { \
        if (RB_PARENT(elm, field) && \
            elm == RB_RIGHT(RB_PARENT(elm, field), field)) { \
            elm = RB_PARENT(elm, field); \
        } else { \
            while (RB_PARENT(elm, field) && \
                elm == RB_LEFT(RB_PARENT(elm, field), field)) { \
                elm = RB_PARENT(elm, field); \
            } \
            elm = RB_PARENT(elm, field); \
        } \
    } \
    return elm; \
} \
attr struct type* name##_RB_MINMAX(struct name* head, int val) \
{ \
    struct type* tmp = RB_ROOT(head); \
    struct type* parent = 0; \
    while (tmp != 0) { \
        parent = tmp; \
        tmp = (val < 0) ? RB_LEFT(tmp, field) : RB_RIGHT(tmp, field); \
    } \
    return parent; \
}

#define RB_NEGINF -1
#define RB_INF 1

#define RB_INSERT(name, x, y) name##_RB_INSERT((x), (y))
#define RB_REMOVE(name, x, y) name##_RB_REMOVE((x), (y))
#define RB_FIND(name, x, y) name##_RB_FIND((x), (y))
#define RB_NFIND(name, x, y) name##_RB_NFIND((x), (y))
#define RB_NEXT(name, x, y) name##_RB_NEXT((y))
#define RB_PREV(name, x, y) name##_RB_PREV((y))
#define RB_MIN(name, x) name##_RB_MINMAX((x), RB_NEGINF)
#define RB_MAX(name, x) name##_RB_MINMAX((x), RB_INF)

#define RB_FOREACH(x, name, head) \
    for ((x) = RB_MIN(name, (head)); (x) != 0; (x) = RB_NEXT(name, (head), (x)))

#define RB_FOREACH_SAFE(x, name, head, y) \
    for ((x) = RB_MIN(name, (head)); \
         (x) != 0 && (((y) = RB_NEXT(name, (head), (x))), 1); \
         (x) = (y))

#endif /* _RODNIX_BSD_SYS_TREE_H_ */
