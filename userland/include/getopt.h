#ifndef _RODNIX_USERLAND_GETOPT_H
#define _RODNIX_USERLAND_GETOPT_H

extern char *optarg;
extern int   optind;
extern int   opterr;
extern int   optopt;

int getopt(int argc, char * const argv[], const char *optstring);

#endif /* _RODNIX_USERLAND_GETOPT_H */
