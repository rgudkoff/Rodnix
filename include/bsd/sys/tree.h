/*
 * BSD-style tree primitives (minimal subset for Rodnix).
 *
 * This file intentionally provides a very small compatibility surface for
 * intrusive binary-tree nodes. It is enough to build indexed containers in
 * kernel subsystems while we incrementally adopt fuller BSD tree APIs.
 */

#ifndef _RODNIX_BSD_SYS_TREE_H_
#define _RODNIX_BSD_SYS_TREE_H_

#define RB_HEAD(name, type) \
struct name { \
    struct type* rbh_root; \
}

#define RB_INITIALIZER(root) { 0 }

#define RB_ENTRY(type) \
struct { \
    struct type* rbe_left; \
    struct type* rbe_right; \
    struct type* rbe_parent; \
}

#define RB_ROOT(head) ((head)->rbh_root)
#define RB_EMPTY(head) (RB_ROOT(head) == 0)

#define RB_LEFT(elm, field) ((elm)->field.rbe_left)
#define RB_RIGHT(elm, field) ((elm)->field.rbe_right)
#define RB_PARENT(elm, field) ((elm)->field.rbe_parent)

#endif /* _RODNIX_BSD_SYS_TREE_H_ */
