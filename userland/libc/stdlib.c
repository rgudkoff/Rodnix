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
    for (ep = environ; *ep != NULL; ep++) {
        if (strncmp(*ep, name, nlen) == 0 && (*ep)[nlen] == '=') {
            return *ep + nlen + 1;
        }
    }
    return NULL;
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
