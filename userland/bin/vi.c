/*
 * vi wrapper - run BusyBox vi as a separate process instead of an in-process
 * applet path from ash.
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    char** new_argv = (char**)calloc((size_t)argc + 2u, sizeof(char*));
    if (!new_argv) {
        return 1;
    }

    new_argv[0] = (char*)"busybox";
    new_argv[1] = (char*)"vi";
    for (int i = 1; i < argc; i++) {
        new_argv[i + 1] = argv[i];
    }
    new_argv[argc + 1] = NULL;

    execve("/bin/busybox", new_argv, environ);
    free(new_argv);
    return (errno != 0) ? errno : 127;
}
