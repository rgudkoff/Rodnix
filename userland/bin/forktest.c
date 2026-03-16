/*
 * forktest.c
 * Basic fork + COW validation probe.
 */

#include <stdint.h>
#include <stdlib.h>
#include "unistd.h"

#define FD_STDOUT 1

static volatile int g_data = 7;

static long write_buf(const char* s, uint64_t len)
{
    return write(FD_STDOUT, s, (size_t)len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

int main(void)
{
    int stack_val = 11;
    int* heap_val = (int*)malloc(sizeof(int));
    if (!heap_val) {
        (void)write_str("forktest: malloc failed\n");
        return 1;
    }
    *heap_val = 13;

    (void)write_str("forktest: before fork data=");
    write_u64((uint64_t)g_data);
    (void)write_str(" stack=");
    write_u64((uint64_t)stack_val);
    (void)write_str(" heap=");
    write_u64((uint64_t)(*heap_val));
    (void)write_str("\n");

    pid_t pid = fork();
    if (pid < 0) {
        (void)write_str("forktest: fork failed\n");
        free(heap_val);
        return 1;
    }

    if (pid == 0) {
        g_data = 70;
        stack_val = 110;
        *heap_val = 130;
        (void)write_str("forktest: child data=");
        write_u64((uint64_t)g_data);
        (void)write_str(" stack=");
        write_u64((uint64_t)stack_val);
        (void)write_str(" heap=");
        write_u64((uint64_t)(*heap_val));
        (void)write_str("\n");
        free(heap_val);
        _exit(0);
    }

    int status = -1;
    pid_t wr = waitpid(pid, &status, 0);
    if (wr != pid || status != 0) {
        (void)write_str("forktest: waitpid failed\n");
        free(heap_val);
        return 1;
    }

    (void)write_str("forktest: parent data=");
    write_u64((uint64_t)g_data);
    (void)write_str(" stack=");
    write_u64((uint64_t)stack_val);
    (void)write_str(" heap=");
    write_u64((uint64_t)(*heap_val));
    (void)write_str("\n");

    int ok = (g_data == 7) && (stack_val == 11) && (*heap_val == 13);
    free(heap_val);
    if (!ok) {
        (void)write_str("forktest: FAIL (COW broken)\n");
        return 1;
    }
    (void)write_str("forktest: PASS\n");
    return 0;
}
