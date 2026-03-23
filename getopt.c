#include <stdio.h>
#include <string.h>

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

int getopt(int argc, char * const argv[], const char *optstring) {
    static int optpos = 1;
    char *arg;

    if (optind >= argc) return -1;

    arg = argv[optind];

    if (arg[0] != '-' || arg[1] == '\0')
        return -1;

    if (strcmp(arg, "--") == 0) {
        optind++;
        return -1;
    }

    char opt = arg[optpos];
    const char *optdecl = strchr(optstring, opt);

    if (!optdecl) {
        optopt = opt;
        if (opterr)
            fprintf(stderr, "Unknown option: -%c\n", opt);
        if (!arg[++optpos]) {
            optind++;
            optpos = 1;
        }
        return '?';
    }

    if (optdecl[1] == ':') {
        if (arg[optpos + 1]) {
            optarg = &arg[optpos + 1];
            optind++;
        } else if (++optind < argc) {
            optarg = argv[optind++];
        } else {
            if (opterr)
                fprintf(stderr, "Option -%c requires argument\n", opt);
            optpos = 1;
            return '?';
        }
        optpos = 1;
    } else {
        if (!arg[++optpos]) {
            optind++;
            optpos = 1;
        }
        optarg = NULL;
    }

    return opt;
}