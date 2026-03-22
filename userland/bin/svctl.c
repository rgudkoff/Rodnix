#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

typedef struct {
    char name[64];
    char mode[32];
    char state[32];
    long pid;
    unsigned long restarts;
    int last_exit;
    int last_sig;
    char command[256];
} svc_status_t;

static int parse_status_line(const char* line, svc_status_t* svc)
{
    char copy[512];
    char* fields[8];
    int nf = 0;
    char* p;

    if (!line || !svc) {
        return 0;
    }

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    fields[nf++] = copy;
    for (p = copy; *p && nf < 8; p++) {
        if (*p == '\t') {
            *p = '\0';
            fields[nf++] = p + 1;
        }
    }
    if (nf < 8) {
        return 0;
    }

    strncpy(svc->name, fields[0], sizeof(svc->name) - 1);
    svc->name[sizeof(svc->name) - 1] = '\0';
    strncpy(svc->mode, fields[1], sizeof(svc->mode) - 1);
    svc->mode[sizeof(svc->mode) - 1] = '\0';
    strncpy(svc->state, fields[2], sizeof(svc->state) - 1);
    svc->state[sizeof(svc->state) - 1] = '\0';
    svc->pid = strtol(fields[3], NULL, 10);
    svc->restarts = (unsigned long)strtoul(fields[4], NULL, 10);
    svc->last_exit = (int)strtol(fields[5], NULL, 10);
    svc->last_sig = (int)strtol(fields[6], NULL, 10);
    strncpy(svc->command, fields[7], sizeof(svc->command) - 1);
    svc->command[sizeof(svc->command) - 1] = '\0';
    return 1;
}

static int load_service_status(const char* name, svc_status_t* out)
{
    FILE* fp;
    char line[512];

    fp = fopen("/run/services.status", "r");
    if (!fp) {
        fprintf(stderr, "svctl: cannot open /run/services.status: %s\n", strerror(errno));
        return 0;
    }

    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }
    while (fgets(line, sizeof(line), fp)) {
        svc_status_t svc;
        if (!parse_status_line(line, &svc)) {
            continue;
        }
        if (strcmp(svc.name, name) == 0) {
            *out = svc;
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

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
    svc_status_t svc;

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
        if (!load_service_status(argv[2], &svc)) {
            fprintf(stderr, "svctl: service '%s' not found\n", argv[2]);
            return 1;
        }
        if (strcmp(argv[1], "start") == 0 && svc.pid > 0) {
            return 0;
        }

        if (strcmp(argv[1], "start") == 0) {
            return write_control("start", argv[2]);
        }

        if (svc.pid <= 0) {
            if (strcmp(argv[1], "restart") == 0) {
                return write_control("start", argv[2]);
            }
            if (strcmp(svc.state, "inactive") == 0) {
                return 0;
            }
            fprintf(stderr, "svctl: service '%s' is not running\n", argv[2]);
            return 1;
        }

        if (write_control(argv[1], argv[2]) != 0) {
            return 1;
        }
        if (kill((pid_t)svc.pid, SIGTERM) != 0) {
            fprintf(stderr, "svctl: kill failed: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    fprintf(stderr, "usage: svctl status [name] | start <name> | stop <name> | restart <name>\n");
    return 1;
}
