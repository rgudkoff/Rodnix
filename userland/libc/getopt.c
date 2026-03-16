#include <getopt.h>
#include <string.h>
#include <stdio.h>

char *optarg = NULL;
int   optind = 1;
int   opterr = 1;
int   optopt = 0;

static int sp = 1; /* position within current argv element */

int getopt(int argc, char * const argv[], const char *optstring)
{
    if (sp == 1) {
        if (optind >= argc || !argv[optind] ||
            argv[optind][0] != '-' || argv[optind][1] == '\0') {
            return -1;
        }
        if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
            optind++;
            return -1;
        }
    }

    optopt = (unsigned char)argv[optind][sp];
    const char *p = strchr(optstring, optopt);

    if (!p || optopt == ':') {
        if (opterr && optstring[0] != ':') {
            fprintf(stderr, "illegal option -- %c\n", optopt);
        }
        if (argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        return '?';
    }

    if (p[1] == ':') {
        /* option requires argument */
        if (argv[optind][sp + 1] != '\0') {
            optarg = &argv[optind][sp + 1];
        } else if (++optind >= argc) {
            if (opterr && optstring[0] != ':') {
                fprintf(stderr, "option requires an argument -- %c\n", optopt);
            }
            sp = 1;
            optarg = NULL;
            return optstring[0] == ':' ? ':' : '?';
        } else {
            optarg = argv[optind];
        }
        sp = 1;
        optind++;
    } else {
        if (argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        optarg = NULL;
    }

    return optopt;
}
