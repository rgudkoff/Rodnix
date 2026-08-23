/**
 * @file witness.c
 * @brief Lock-order verifier. See core/witness.h for why.
 */

#include "core/witness.h"
#include "core/boot.h"
#include "arch/percpu.h"
#include "fabric/spin.h"
#include "../include/console.h"
#include "../include/debug.h"
#include <stddef.h>

/* Distinct lock nodes. Identity is the acquire-site name, not the address:
 * a per-connection socket lock would otherwise mint a node per connection and
 * exhaust this in a way that scales with load rather than with code. Keying on
 * the name makes a node a lock *class*, which is also what an order is
 * actually a statement about. */
#define WITNESS_MAX_LOCKS  96
#define WITNESS_WORDS      ((WITNESS_MAX_LOCKS + 63) / 64)

/* Deep enough for real nesting and shallow enough that an overflow means a
 * bug rather than a tuning problem. The deepest chain measured so far is 3. */
#define WITNESS_MAX_HELD   12

#define WITNESS_MAX_CPUS   64

/* Edges kept with their source site, for the report. Only new edges are
 * recorded and the graph saturates within the first seconds of a boot, so
 * this is sized for the graph, not for the acquire rate. */
#define WITNESS_MAX_EDGES  512

struct witness_node {
    const char* name;
    const char* file;   /* where it was first seen */
    int line;
};

struct witness_edge {
    uint16_t from;
    uint16_t to;
    const char* file;   /* where this order was learned */
    int line;
};

struct witness_held {
    const void* addr;
    uint32_t id;
    const char* file;
    int line;
};

struct witness_cpu {
    struct witness_held held[WITNESS_MAX_HELD];
    uint32_t depth;
    /* Nonzero while this processor is inside witness. Everything below
     * re-enters through kprintf, which takes the console spinlock, which
     * comes straight back here. Guarding is cheaper and more honest than
     * teaching the reporter to avoid the console. */
    uint32_t busy;
    /* Nesting was deeper than the held array. Tracking stops rather than
     * overwriting, and unwinding is counted so release stays balanced. */
    uint32_t overflow;
} __attribute__((aligned(64)));

static struct witness_cpu g_cpu[WITNESS_MAX_CPUS];

static struct witness_node g_node[WITNESS_MAX_LOCKS];
static uint32_t g_node_count;

/* order[i] bit j: i was held when j was acquired. */
static uint64_t g_order[WITNESS_MAX_LOCKS][WITNESS_WORDS];
/* Reversals already reported, so a pair on a hot path prints once. */
static uint64_t g_reported[WITNESS_MAX_LOCKS][WITNESS_WORDS];

static struct witness_edge g_edge[WITNESS_MAX_EDGES];
static uint32_t g_edge_count;

static witness_mode_t g_mode = WITNESS_WARN;
static bool g_node_overflow;
static uint32_t g_reversals;
static bool g_selftest_wanted;

/*
 * Witness's own lock, hand-rolled rather than a spinlock_t for the obvious
 * reason: a spinlock_t would be checked by witness, and witness would take
 * this to do the checking.
 */
static volatile uint32_t g_witness_lock;

static uint64_t witness_raw_lock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) :: "memory");
    while (__sync_lock_test_and_set(&g_witness_lock, 1)) {
        __asm__ volatile ("pause");
    }
    return flags;
}

static void witness_raw_unlock(uint64_t flags)
{
    __sync_lock_release(&g_witness_lock);
    __asm__ volatile ("pushq %0\n\tpopfq" :: "r"(flags) : "memory", "cc");
}

static inline void bit_set(uint64_t* w, uint32_t i)
{
    w[i / 64] |= (1ULL << (i % 64));
}

static inline bool bit_test(const uint64_t* w, uint32_t i)
{
    return (w[i / 64] & (1ULL << (i % 64))) != 0;
}

static bool witness_streq(const char* a, const char* b)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static struct witness_cpu* witness_cpu(void)
{
    uint32_t i = percpu_index();
    if (i >= WITNESS_MAX_CPUS) {
        i = 0;
    }
    return &g_cpu[i];
}

void witness_init(void)
{
    const boot_info_t* bi = boot_get_info();
    if (!bi || !bi->cmdline[0]) {
        return;
    }
    /* Substring match, same as the smp flag is read. */
    for (const char* p = bi->cmdline; *p; p++) {
        if (p[0] != 'r' || p[1] != 'd' || p[2] != 'n' || p[3] != 'x' ||
            p[4] != '.' || p[5] != 'w') {
            continue;
        }
        const char* key = "rdnx.witness=";
        const char* q = p;
        const char* k = key;
        while (*k && *q == *k) {
            q++;
            k++;
        }
        if (*k) {
            continue;
        }
        if (q[0] == 'o' && q[1] == 'f' && q[2] == 'f') {
            g_mode = WITNESS_OFF;
        } else if (q[0] == 'p') {
            g_mode = WITNESS_PANIC;
        } else if (q[0] == 's') {
            g_mode = WITNESS_WARN;
            g_selftest_wanted = true;
        } else {
            g_mode = WITNESS_WARN;
        }
        return;
    }
}

witness_mode_t witness_mode(void)
{
    return g_mode;
}

/* Caller holds the witness lock. */
static uint32_t witness_intern(const char* name, const char* file, int line)
{
    for (uint32_t i = 0; i < g_node_count; i++) {
        if (witness_streq(g_node[i].name, name)) {
            return i + 1u;
        }
    }
    if (g_node_count >= WITNESS_MAX_LOCKS) {
        g_node_overflow = true;
        return 0;
    }
    g_node[g_node_count].name = name;
    g_node[g_node_count].file = file;
    g_node[g_node_count].line = line;
    g_node_count++;
    return g_node_count;
}

/*
 * Is `to` reachable from `from` along recorded orders? If it is, adding
 * from -> to closes a cycle, and a cycle is the deadlock.
 *
 * Reachability rather than the reverse-edge test, because a three-lock cycle
 * -- A before B, B before C, C before A -- has no reversed pair anywhere in
 * it. Those are the ones that survive review.
 *
 * Caller holds the witness lock. parent[] is filled so the report can print
 * the cycle rather than just assert one exists.
 */
static bool witness_reaches(uint32_t from, uint32_t to, uint16_t* parent)
{
    uint64_t seen[WITNESS_WORDS] = {0};
    uint16_t stack[WITNESS_MAX_LOCKS];
    uint32_t sp = 0;

    stack[sp++] = (uint16_t)from;
    bit_set(seen, from);
    parent[from] = (uint16_t)from;

    while (sp > 0) {
        uint32_t n = stack[--sp];
        for (uint32_t j = 0; j < g_node_count; j++) {
            if (!bit_test(g_order[n], j)) {
                continue;
            }
            if (bit_test(seen, j)) {
                continue;
            }
            parent[j] = (uint16_t)n;
            if (j == to) {
                return true;
            }
            bit_set(seen, j);
            stack[sp++] = (uint16_t)j;
        }
    }
    return false;
}

/* Caller holds the witness lock. */
static const struct witness_edge* witness_find_edge(uint32_t from, uint32_t to)
{
    for (uint32_t i = 0; i < g_edge_count; i++) {
        if (g_edge[i].from == from && g_edge[i].to == to) {
            return &g_edge[i];
        }
    }
    return NULL;
}

static void witness_report(uint32_t held, uint32_t want,
                           const char* file, int line,
                           const uint16_t* parent)
{
    kprintf("\n[WITNESS] lock order reversal on cpu%u\n",
            (unsigned)percpu_index());
    kprintf("[WITNESS]   acquiring %s at %s:%d\n",
            g_node[want - 1].name, file, line);
    kprintf("[WITNESS]   while holding %s\n", g_node[held - 1].name);
    kprintf("[WITNESS]   but this order is already known the other way:\n");

    /*
     * parent[] points back towards the lock being acquired, so the chain is
     * collected from `held` and printed in reverse -- the direction the code
     * actually takes them, which is the direction a reader needs.
     */
    uint16_t chain[WITNESS_MAX_LOCKS];
    uint32_t len = 0;
    uint32_t n = held - 1u;
    while (len < WITNESS_MAX_LOCKS) {
        chain[len++] = (uint16_t)n;
        uint32_t p = parent[n];
        if (p == n) {
            break;
        }
        n = p;
    }

    /* One lock is taken by a caller in this kernel that also takes the other
     * lock first; witness's own lock guards the tables while we read them,
     * and kprintf below re-enters witness only as far as the busy check. */
    uint64_t f = witness_raw_lock();
    for (uint32_t i = len; i > 1; i--) {
        uint32_t from = chain[i - 1];
        uint32_t to = chain[i - 2];
        const struct witness_edge* e = witness_find_edge(from, to);
        kprintf("[WITNESS]     %s -> %s", g_node[from].name, g_node[to].name);
        if (e) {
            kprintf("   (learned at %s:%d)", e->file, e->line);
        }
        kprintf("\n");
    }
    witness_raw_unlock(f);
    kprintf("[WITNESS]   two processors on these two paths at once wedge both.\n\n");
}

uint32_t witness_check(uint32_t* cached_id, const char* name,
                       const char* file, int line)
{
    if (g_mode == WITNESS_OFF) {
        return 0;
    }

    struct witness_cpu* c = witness_cpu();
    if (c->busy) {
        return 0;
    }
    c->busy++;

    uint64_t f = witness_raw_lock();

    uint32_t id = cached_id ? *cached_id : 0;
    if (id == 0 || id > g_node_count) {
        id = witness_intern(name, file, line);
        if (cached_id) {
            *cached_id = id;
        }
    }
    if (id == 0) {
        witness_raw_unlock(f);
        c->busy--;
        return 0;
    }

    uint32_t want = id - 1u;
    uint32_t report_held = 0;
    uint16_t parent[WITNESS_MAX_LOCKS];

    for (uint32_t i = 0; i < c->depth; i++) {
        uint32_t hid = c->held[i].id;
        if (hid == 0 || hid == id) {
            continue;   /* recursion is spin.c's panic, not ours */
        }
        uint32_t h = hid - 1u;
        if (bit_test(g_order[h], want)) {
            continue;   /* order already known and agreed with */
        }
        if (witness_reaches(want, h, parent)) {
            if (!bit_test(g_reported[h], want)) {
                bit_set(g_reported[h], want);
                g_reversals++;
                report_held = hid;
            }
            continue;   /* do not record the edge that closes the cycle */
        }
        bit_set(g_order[h], want);
        if (g_edge_count < WITNESS_MAX_EDGES) {
            g_edge[g_edge_count].from = (uint16_t)h;
            g_edge[g_edge_count].to = (uint16_t)want;
            g_edge[g_edge_count].file = file;
            g_edge[g_edge_count].line = line;
            g_edge_count++;
        }
    }

    witness_raw_unlock(f);

    if (report_held) {
        witness_report(report_held, id, file, line, parent);
        if (g_mode == WITNESS_PANIC) {
            c->busy--;
            panicf("witness: lock order reversal (%s under %s)",
                   g_node[id - 1].name, g_node[report_held - 1].name);
        }
    }

    c->busy--;
    return id;
}

void witness_acquired(uint32_t* cached_id, const void* addr,
                      const char* name, const char* file, int line)
{
    if (g_mode == WITNESS_OFF) {
        return;
    }
    struct witness_cpu* c = witness_cpu();
    if (c->busy) {
        return;
    }

    uint32_t id = cached_id ? *cached_id : 0;
    if (id == 0) {
        /* A trylock reaches here without a check having run: it never waits,
         * so it cannot be half of a deadlock and deliberately contributes no
         * ordering. It still has to be recorded as held, because what is
         * taken *under* it does order. */
        c->busy++;
        uint64_t f = witness_raw_lock();
        id = witness_intern(name, file, line);
        witness_raw_unlock(f);
        c->busy--;
        if (cached_id) {
            *cached_id = id;
        }
    }

    if (c->depth >= WITNESS_MAX_HELD) {
        c->overflow++;
        return;
    }
    c->held[c->depth].addr = addr;
    c->held[c->depth].id = id;
    c->held[c->depth].file = file;
    c->held[c->depth].line = line;
    c->depth++;
}

void witness_release(const void* addr)
{
    if (g_mode == WITNESS_OFF) {
        return;
    }
    struct witness_cpu* c = witness_cpu();
    if (c->busy) {
        return;
    }

    /* From the top: release is usually but not always LIFO. */
    for (uint32_t i = c->depth; i > 0; i--) {
        if (c->held[i - 1].addr != addr) {
            continue;
        }
        for (uint32_t j = i - 1; j + 1 < c->depth; j++) {
            c->held[j] = c->held[j + 1];
        }
        c->depth--;
        return;
    }
    if (c->overflow > 0) {
        c->overflow--;
    }
}

void witness_dump_held(void)
{
    struct witness_cpu* c = witness_cpu();
    kprintf("[WITNESS] cpu%u holds %u lock(s)%s:\n",
            (unsigned)percpu_index(), (unsigned)c->depth,
            c->overflow ? " (plus untracked, nesting overflowed)" : "");
    for (uint32_t i = c->depth; i > 0; i--) {
        const struct witness_held* h = &c->held[i - 1];
        kprintf("[WITNESS]   %s  taken at %s:%d\n",
                (h->id && h->id <= g_node_count) ? g_node[h->id - 1].name : "?",
                h->file, h->line);
    }
}

uint32_t witness_reversal_count(void)
{
    return g_reversals;
}

/* Locks that exist only to be misused. Static so they get real witness nodes
 * on the real graph -- testing the verifier through a private copy would test
 * the copy. */
static spinlock_t witness_probe_a;
static spinlock_t witness_probe_b;
static spinlock_t witness_probe_c;

void witness_selftest(void)
{
    if (!g_selftest_wanted) {
        return;
    }

    spinlock_init(&witness_probe_a);
    spinlock_init(&witness_probe_b);
    spinlock_init(&witness_probe_c);

    uint32_t before = g_reversals;

    /* A pair. Teach a-before-b, then do b-before-a. */
    spinlock_lock(&witness_probe_a);
    spinlock_lock(&witness_probe_b);
    spinlock_unlock(&witness_probe_b);
    spinlock_unlock(&witness_probe_a);

    spinlock_lock(&witness_probe_b);
    spinlock_lock(&witness_probe_a);
    spinlock_unlock(&witness_probe_a);
    spinlock_unlock(&witness_probe_b);

    uint32_t after_pair = g_reversals;

    /*
     * A three-lock cycle: b-before-c, then c-before-a closes b -> c -> a -> b
     * with no two locks ever taken in opposite order. This is the case a
     * reverse-edge test misses and the reason the check walks the graph.
     */
    spinlock_lock(&witness_probe_b);
    spinlock_lock(&witness_probe_c);
    spinlock_unlock(&witness_probe_c);
    spinlock_unlock(&witness_probe_b);

    spinlock_lock(&witness_probe_c);
    spinlock_lock(&witness_probe_a);
    spinlock_unlock(&witness_probe_a);
    spinlock_unlock(&witness_probe_c);

    uint32_t after_cycle = g_reversals;

    kprintf("[witness] selftest: pair %s, cycle %s (reversals %u -> %u -> %u)\n",
            (after_pair > before) ? "DETECTED" : "MISSED",
            (after_cycle > after_pair) ? "DETECTED" : "MISSED",
            (unsigned)before, (unsigned)after_pair, (unsigned)after_cycle);

    if (after_pair == before || after_cycle == after_pair) {
        panicf("witness selftest failed: the verifier does not verify");
    }
}

void witness_summary(void)
{
    uint64_t f = witness_raw_lock();
    uint32_t nodes = g_node_count;
    uint32_t edges = g_edge_count;
    witness_raw_unlock(f);

    kprintf("[witness] %s: %u locks, %u orders learned%s\n",
            g_mode == WITNESS_OFF ? "off" :
            g_mode == WITNESS_PANIC ? "panic on reversal" : "warn on reversal",
            (unsigned)nodes, (unsigned)edges,
            g_node_overflow ? " (LOCK TABLE FULL)" : "");
}

void witness_dump_graph(void)
{
    uint64_t f = witness_raw_lock();
    uint32_t nodes = g_node_count;
    uint32_t edges = g_edge_count;
    witness_raw_unlock(f);

    kprintf("[WITNESS] mode=%s locks=%u orders=%u%s\n",
            g_mode == WITNESS_OFF ? "off" :
            g_mode == WITNESS_PANIC ? "panic" : "warn",
            (unsigned)nodes, (unsigned)edges,
            g_node_overflow ? " (LOCK TABLE FULL -- some locks untracked)" : "");
    for (uint32_t i = 0; i < edges && i < WITNESS_MAX_EDGES; i++) {
        kprintf("[WITNESS]   %s -> %s   (%s:%d)\n",
                g_node[g_edge[i].from].name,
                g_node[g_edge[i].to].name,
                g_edge[i].file, g_edge[i].line);
    }
}
