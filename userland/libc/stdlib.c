#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int char_to_digit(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return -1;
}

int atoi(const char* nptr)
{
    return (int)strtol(nptr, 0, 10);
}

unsigned long strtoul(const char* nptr, char** endptr, int base)
{
    unsigned long value = 0;
    const char* s = nptr;
    int neg = 0;
    int any = 0;

    if (!s) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    for (;;) {
        int d = char_to_digit((unsigned char)*s);
        if (d < 0 || d >= base) {
            break;
        }
        value = (value * (unsigned long)base) + (unsigned long)d;
        any = 1;
        s++;
    }
    if (endptr) {
        *endptr = (char*)(any ? s : nptr);
    }
    if (neg) {
        return (unsigned long)(-(long)value);
    }
    return value;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base)
{
    return (unsigned long long)strtoul(nptr, endptr, base);
}

long strtol(const char* nptr, char** endptr, int base)
{
    return (long)strtoul(nptr, endptr, base);
}

double strtod(const char* nptr, char** endptr)
{
    const char* s = nptr;
    double result = 0.0;
    double frac = 0.0;
    double frac_div = 1.0;
    int neg = 0;
    int in_frac = 0;
    int any = 0;

    if (!s) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0.0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (double)(*s - '0');
        s++;
        any = 1;
    }
    if (*s == '.') {
        s++;
        in_frac = 1;
        while (*s >= '0' && *s <= '9') {
            frac = frac * 10.0 + (double)(*s - '0');
            frac_div *= 10.0;
            s++;
            any = 1;
        }
    }
    (void)in_frac;
    result += frac / frac_div;
    if (*s == 'e' || *s == 'E') {
        const char* exp_start = s;
        s++;
        int exp_neg = 0;
        int exp = 0;
        if (*s == '+' || *s == '-') {
            exp_neg = (*s == '-');
            s++;
        }
        if (*s >= '0' && *s <= '9') {
            while (*s >= '0' && *s <= '9') {
                exp = exp * 10 + (*s - '0');
                s++;
            }
            double mul = 1.0;
            for (int i = 0; i < exp; i++) {
                mul *= 10.0;
            }
            if (exp_neg) {
                result /= mul;
            } else {
                result *= mul;
            }
        } else {
            s = exp_start; /* no digits after e/E[+-]: roll back */
        }
    }
    if (endptr) {
        *endptr = (char*)(any ? s : nptr);
    }
    return neg ? -result : result;
}

float strtof(const char* nptr, char** endptr)
{
    return (float)strtod(nptr, endptr);
}

long double strtold(const char* nptr, char** endptr)
{
    return (long double)strtod(nptr, endptr);
}

double atof(const char* nptr)
{
    return strtod(nptr, NULL);
}

int abs(int x)
{
    return x < 0 ? -x : x;
}

long labs(long x)
{
    return x < 0 ? -x : x;
}

long long llabs(long long x)
{
    return x < 0 ? -x : x;
}

/* atexit handlers */
#define ATEXIT_MAX 32
static void (*g_atexit_fns[ATEXIT_MAX])(void);
static int g_atexit_count = 0;

int atexit(void (*fn)(void))
{
    if (!fn || g_atexit_count >= ATEXIT_MAX) {
        return -1;
    }
    g_atexit_fns[g_atexit_count++] = fn;
    return 0;
}

void exit(int status)
{
    for (int i = g_atexit_count - 1; i >= 0; i--) {
        if (g_atexit_fns[i]) {
            g_atexit_fns[i]();
        }
    }
    _exit(status);
}

void abort(void)
{
    _exit(134); /* SIGABRT conventional exit code */
}

/* environ support */
char** environ = NULL;

char* getenv(const char* name)
{
    size_t nlen;
    char** ep;

    if (!name || !environ) {
        return NULL;
    }
    nlen = strlen(name);
    if (nlen == 0) {
        return NULL;
    }
    for (ep = environ; *ep != NULL; ep++) {
        if (strncmp(*ep, name, nlen) == 0 && (*ep)[nlen] == '=') {
            return *ep + nlen + 1;
        }
    }
    return NULL;
}

/* ---- environ dynamic management ---- */

static char** g_env_arr   = NULL;
static int     g_env_count = 0;
static int     g_env_cap   = 0;

static int env_ensure_arr(void)
{
    int n, cap;
    char** arr;

    if (g_env_arr) {
        return 0;
    }
    n = 0;
    if (environ) {
        while (environ[n]) {
            n++;
        }
    }
    cap = n + 16;
    arr = (char**)malloc((size_t)(cap + 1) * sizeof(char*));
    if (!arr) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        arr[i] = environ[i];
    }
    arr[n] = NULL;
    g_env_arr   = arr;
    g_env_count = n;
    g_env_cap   = cap;
    environ = g_env_arr;
    return 0;
}

static int env_grow(void)
{
    int new_cap;
    char** arr;

    new_cap = g_env_cap + 16;
    arr = (char**)realloc(g_env_arr, (size_t)(new_cap + 1) * sizeof(char*));
    if (!arr) {
        return -1;
    }
    g_env_arr = arr;
    g_env_cap = new_cap;
    environ = g_env_arr;
    return 0;
}

static int env_find(const char* name, size_t nlen)
{
    int i;
    for (i = 0; i < g_env_count; i++) {
        if (strncmp(g_env_arr[i], name, nlen) == 0 && g_env_arr[i][nlen] == '=') {
            return i;
        }
    }
    return -1;
}

int setenv(const char* name, const char* value, int overwrite)
{
    size_t nlen, vlen;
    int idx;
    char* new_str;

    if (!name || name[0] == '\0' || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (env_ensure_arr() != 0) {
        errno = ENOMEM;
        return -1;
    }
    nlen = strlen(name);
    vlen = value ? strlen(value) : 0;
    idx  = env_find(name, nlen);

    if (idx >= 0) {
        if (!overwrite) {
            return 0;
        }
        new_str = (char*)malloc(nlen + 1 + vlen + 1);
        if (!new_str) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(new_str, name, nlen);
        new_str[nlen] = '=';
        if (value) {
            memcpy(new_str + nlen + 1, value, vlen);
        }
        new_str[nlen + 1 + vlen] = '\0';
        g_env_arr[idx] = new_str;
        return 0;
    }

    if (g_env_count >= g_env_cap) {
        if (env_grow() != 0) {
            errno = ENOMEM;
            return -1;
        }
    }
    new_str = (char*)malloc(nlen + 1 + vlen + 1);
    if (!new_str) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(new_str, name, nlen);
    new_str[nlen] = '=';
    if (value) {
        memcpy(new_str + nlen + 1, value, vlen);
    }
    new_str[nlen + 1 + vlen] = '\0';
    g_env_arr[g_env_count++] = new_str;
    g_env_arr[g_env_count]   = NULL;
    return 0;
}

int unsetenv(const char* name)
{
    size_t nlen;
    int idx, i;

    if (!name || name[0] == '\0' || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (env_ensure_arr() != 0) {
        errno = ENOMEM;
        return -1;
    }
    nlen = strlen(name);
    idx  = env_find(name, nlen);
    if (idx < 0) {
        return 0;
    }
    for (i = idx; i < g_env_count - 1; i++) {
        g_env_arr[i] = g_env_arr[i + 1];
    }
    g_env_arr[--g_env_count] = NULL;
    return 0;
}

int putenv(char* str)
{
    const char* eq;
    size_t nlen;
    int idx;

    if (!str) {
        errno = EINVAL;
        return -1;
    }
    eq = strchr(str, '=');
    if (!eq || eq == str) {
        errno = EINVAL;
        return -1;
    }
    if (env_ensure_arr() != 0) {
        errno = ENOMEM;
        return -1;
    }
    nlen = (size_t)(eq - str);
    idx  = env_find(str, nlen);
    if (idx >= 0) {
        g_env_arr[idx] = str;
        return 0;
    }
    if (g_env_count >= g_env_cap) {
        if (env_grow() != 0) {
            errno = ENOMEM;
            return -1;
        }
    }
    g_env_arr[g_env_count++] = str;
    g_env_arr[g_env_count]   = NULL;
    return 0;
}

/* qsort — iterative quicksort (Lomuto) + insertion sort for small ranges */
static void swap_bytes(char* a, char* b, size_t size)
{
    while (size-- > 0) {
        char tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

#define QSORT_ISORT_THRESH 16

static void isort_range(char* arr, long lo, long hi, size_t size,
                        int (*compar)(const void*, const void*))
{
    for (long i = lo + 1; i <= hi; i++) {
        for (long j = i; j > lo && compar(arr + (j-1)*size, arr + j*size) > 0; j--) {
            swap_bytes(arr + (j-1)*size, arr + j*size, size);
        }
    }
}

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*))
{
    char* arr = (char*)base;
    if (!arr || nmemb <= 1 || size == 0 || !compar) {
        return;
    }

    /* Small array: insertion sort directly */
    if (nmemb <= QSORT_ISORT_THRESH) {
        isort_range(arr, 0, (long)nmemb - 1, size, compar);
        return;
    }

    /* Iterative Lomuto quicksort with explicit stack */
    long stack[128];
    int sp = 0;
    stack[sp++] = 0;
    stack[sp++] = (long)nmemb - 1;

    while (sp > 0) {
        long hi = stack[--sp];
        long lo = stack[--sp];

        if (lo >= hi) {
            continue;
        }

        /* Insertion sort for small sub-ranges */
        if (hi - lo < QSORT_ISORT_THRESH) {
            isort_range(arr, lo, hi, size, compar);
            continue;
        }

        /* Median-of-three: sort lo, mid, hi — then move median to hi */
        long mid = lo + (hi - lo) / 2;
        if (compar(arr + lo*size, arr + mid*size) > 0) {
            swap_bytes(arr + lo*size, arr + mid*size, size);
        }
        if (compar(arr + lo*size, arr + hi*size) > 0) {
            swap_bytes(arr + lo*size, arr + hi*size, size);
        }
        if (compar(arr + mid*size, arr + hi*size) > 0) {
            swap_bytes(arr + mid*size, arr + hi*size, size);
        }
        /* median is now at mid; move it to hi to serve as Lomuto pivot */
        swap_bytes(arr + mid*size, arr + hi*size, size);

        /* Lomuto partition: pivot stays at hi throughout the scan */
        char* pivot = arr + hi * size;
        long i = lo;
        for (long j = lo; j < hi; j++) {
            if (compar(arr + j*size, pivot) <= 0) {
                swap_bytes(arr + i*size, arr + j*size, size);
                i++;
            }
        }
        /* place pivot in its final position */
        swap_bytes(arr + i*size, arr + hi*size, size);

        /* push both partitions (smaller first to bound stack depth) */
        if (sp + 4 <= 128) {
            stack[sp++] = lo;
            stack[sp++] = i - 1;
            stack[sp++] = i + 1;
            stack[sp++] = hi;
        }
    }
}
#undef QSORT_ISORT_THRESH

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*))
{
    const char* arr = (const char*)base;
    size_t lo = 0;
    size_t hi = nmemb;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = compar(key, arr + mid * size);
        if (cmp == 0) {
            return (void*)(arr + mid * size);
        } else if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

/* ---- realpath ---- */
/* Lexical path normalization (no symlink resolution — no symlinks yet). */
char* realpath(const char* path, char* resolved)
{
    char work[4096];
    char out[4096];
    char* op;
    char* s;
    char* buf;
    size_t outlen;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    if (path[0] != '/') {
        if (!getcwd(work, sizeof(work))) {
            return NULL;
        }
        size_t cwdlen  = strlen(work);
        size_t pathlen = strlen(path);
        if (cwdlen + 1 + pathlen + 1 > sizeof(work)) {
            errno = ENOMEM;
            return NULL;
        }
        work[cwdlen] = '/';
        memcpy(work + cwdlen + 1, path, pathlen + 1);
    } else {
        size_t pathlen = strlen(path);
        if (pathlen + 1 > sizeof(work)) {
            errno = ENOMEM;
            return NULL;
        }
        memcpy(work, path, pathlen + 1);
    }

    /* Walk path component by component.
     * Each regular component is stat()'d immediately after being appended,
     * so missing/../existing correctly fails on "missing", not on the
     * normalized result.  ".." is safe without re-stat because we already
     * verified the child when we descended into it. */
    op  = out;
    *op = '\0';
    s   = work;

    while (*s) {
        char*       cs;
        size_t      len;
        struct stat _st;

        while (*s == '/') {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        cs = s;
        while (*s && *s != '/') {
            s++;
        }
        len = (size_t)(s - cs);

        if (len == 1 && cs[0] == '.') {
            /* Current dir — no movement, already verified. */
        } else if (len == 2 && cs[0] == '.' && cs[1] == '.') {
            /* Parent dir — strip last component.
             * Parent's existence is guaranteed: we stat'd it when descending. */
            if (op > out) {
                op--;
                while (op > out && *op != '/') {
                    op--;
                }
                *op = '\0';
            }
        } else {
            /* Regular component — append and verify it exists NOW. */
            if ((size_t)(op - out) + 1 + len + 1 > sizeof(out)) {
                errno = ENOMEM;
                return NULL;
            }
            *op++ = '/';
            memcpy(op, cs, len);
            op += len;
            *op = '\0';

            if (stat(out, &_st) != 0) {
                /* errno set by stat() — typically ENOENT */
                return NULL;
            }
        }
    }

    if (op == out) {
        out[0] = '/';
        out[1] = '\0';
        op = out + 1;
    }

    outlen = (size_t)(op - out);
    buf    = resolved;
    if (!buf) {
        buf = (char*)malloc(outlen + 1);
        if (!buf) {
            errno = ENOMEM;
            return NULL;
        }
    }
    memcpy(buf, out, outlen + 1);
    return buf;
}

/* ---- mkstemp ---- */
int mkstemp(char* templ)
{
    static unsigned g_seq = 0;
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t tlen;
    char*  x;
    int    attempt;

    if (!templ) {
        errno = EINVAL;
        return -1;
    }
    tlen = strlen(templ);
    if (tlen < 6) {
        errno = EINVAL;
        return -1;
    }
    x = templ + tlen - 6;
    for (int i = 0; i < 6; i++) {
        if (x[i] != 'X') {
            errno = EINVAL;
            return -1;
        }
    }

    for (attempt = 0; attempt < 64; attempt++) {
        unsigned s = (unsigned)getpid() ^ (g_seq++ * 1664525U) ^ ((unsigned)attempt * 1013904223U);
        for (int i = 0; i < 6; i++) {
            s = s * 1664525U + 1013904223U;
            x[i] = chars[s % 62];
        }
        int fd = open(templ, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            return fd;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    errno = EEXIST;
    return -1;
}
