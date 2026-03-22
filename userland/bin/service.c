#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>

#define RC_DIR "/etc/rc.d"
#define RC_PATH_MAX 256
#define SHELL_PATH "/bin/busybox"

static void usage(void)
{
    fprintf(stderr, "usage: service -l | service <name> <action>\n");
}

static int list_services(void)
{
    DIR* dir = opendir(RC_DIR);
    struct dirent* ent;

    if (!dir) {
        fprintf(stderr, "service: cannot open %s: %s\n", RC_DIR, strerror(errno));
        return 1;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        printf("%s\n", ent->d_name);
    }

    closedir(dir);
    return 0;
}

static int run_service_script(const char* name, char** argv)
{
    char path[RC_PATH_MAX];
    char* child_argv[16];
    int argc = 0;
    pid_t pid;
    int status = 0;

    if (!name || !argv || !argv[0]) {
        usage();
        return 1;
    }

    if (snprintf(path, sizeof(path), "%s/%s", RC_DIR, name) >= (int)sizeof(path)) {
        fprintf(stderr, "service: name too long\n");
        return 1;
    }
    if (access(path, F_OK) != 0) {
        fprintf(stderr, "service: %s not found\n", name);
        return 1;
    }

    child_argv[argc++] = (char*)SHELL_PATH;
    child_argv[argc++] = (char*)"sh";
    child_argv[argc++] = path;
    for (int i = 0; argv[i] != NULL && argc < (int)(sizeof(child_argv) / sizeof(child_argv[0])) - 1; i++) {
        child_argv[argc++] = argv[i];
    }
    child_argv[argc] = NULL;

    pid = spawnv(SHELL_PATH, child_argv);
    if (pid < 0) {
        fprintf(stderr, "service: spawn failed: %s\n", strerror(errno));
        return 1;
    }
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "service: waitpid failed: %s\n", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "-l") == 0) {
        return list_services();
    }
    if (argc >= 3) {
        return run_service_script(argv[1], &argv[2]);
    }

    usage();
    return 1;
}
