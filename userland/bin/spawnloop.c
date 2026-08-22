/* Minimal reproducer: spawn an external binary repeatedly and report.
 *
 * CT-029 is the first contract case that loads a *different* ELF from the
 * filesystem, and it is where the suite stops under SMP -- while forktest
 * (which forks but does not exec) and execvetest (which execs itself) both
 * pass. This isolates that one operation. */
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#define FD_STDOUT 1
#define ROUNDS 12

static void put(const char* s)
{
    uint64_t n = 0;
    while (s[n]) { n++; }
    (void)posix_write(FD_STDOUT, s, n);
}

static void put_num(long v)
{
    char b[24];
    int i = 0;
    if (v < 0) { put("-"); v = -v; }
    if (v == 0) { b[i++] = '0'; }
    while (v > 0 && i < 23) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    char o[24];
    int j = 0;
    while (i > 0) { o[j++] = b[--i]; }
    o[j] = '\0';
    put(o);
}

int main(void)
{
    /* Exactly what CT-029 runs: ls does a readdir over ~60 entries and
     * writes the result, which plain fork+exec of /bin/true does not. */
    char* const argv[] = { (char*)"/bin/ls", (char*)"-la1", (char*)"/bin", NULL };

    for (int round = 0; round < ROUNDS; round++) {
        int status = -1;
        long pid = posix_spawn(argv[0], (const char* const*)argv);
        if (pid <= 0) {
            put("spawnloop: FAIL spawn round=");
            put_num(round);
            put("\n");
            return 1;
        }
        if (waitpid((pid_t)pid, &status, 0) != pid) {
            put("spawnloop: FAIL wait round=");
            put_num(round);
            put("\n");
            return 1;
        }
    }

    put("spawnloop: PASS ");
    put_num(ROUNDS);
    put(" spawns\n");
    return 0;
}
