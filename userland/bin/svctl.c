#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static int run_svstat_filtered(const char* name)
{
    FILE* fp;
    char line[512];
    int found = 0;

    fp = fopen("/run/services.status", "r");
    if (!fp) {
        fprintf(stderr, "svctl: cannot open /run/services.status: %s\n", strerror(errno));
        return 1;
    }

    if (fgets(line, sizeof(line), fp)) {
        fputs(line, stdout);
    }
    while (fgets(line, sizeof(line), fp)) {
        if (!name || name[0] == '\0') {
            fputs(line, stdout);
            found = 1;
            continue;
        }
        if (strncmp(line, name, strlen(name)) == 0 && line[strlen(name)] == '\t') {
            fputs(line, stdout);
            found = 1;
        }
    }
    fclose(fp);

    if (name && name[0] != '\0' && !found) {
        fprintf(stderr, "svctl: service '%s' not found\n", name);
        return 1;
    }
    return 0;
}

static int write_control(const char* cmd, const char* name)
{
    int fd;
    char line[128];
    int len;

    fd = open("/run/services.control", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "svctl: cannot open control file: %s\n", strerror(errno));
        return 1;
    }

    if (name && name[0] != '\0') {
        len = snprintf(line, sizeof(line), "%s %s\n", cmd, name);
    } else {
        len = snprintf(line, sizeof(line), "%s\n", cmd);
    }
    if (len < 0 || len >= (int)sizeof(line) || write(fd, line, (size_t)len) != len) {
        fprintf(stderr, "svctl: write control failed\n");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: svctl status [name] | start <name> | stop <name> | restart <name>\n");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        return run_svstat_filtered(argc >= 3 ? argv[2] : NULL);
    }

    if ((strcmp(argv[1], "start") == 0 ||
         strcmp(argv[1], "stop") == 0 ||
         strcmp(argv[1], "restart") == 0) && argc >= 3) {
        return write_control(argv[1], argv[2]);
    }

    fprintf(stderr, "usage: svctl status [name] | start <name> | stop <name> | restart <name>\n");
    return 1;
}
