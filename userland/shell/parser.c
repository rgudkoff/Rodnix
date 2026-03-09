#include "shell_internal.h"

char shell_cwd[SH_PATH_MAX] = "/";
char shell_ps1[SH_PS1_MAX] = "";

int parse_line(char* line, char** argv, int max_args)
{
    int argc = 0;
    int in_word = 0;

    for (int i = 0; line[i] != '\0' && argc < max_args; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c <= 0x20u) {
            if (in_word) {
                line[i] = '\0';
                in_word = 0;
            }
        } else if (!in_word) {
            argv[argc++] = &line[i];
            in_word = 1;
        }
    }

    argv[argc] = 0;
    return argc;
}

int line_has_meta(const char* s)
{
    if (!s) {
        return 0;
    }
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '|' || s[i] == '<' || s[i] == '>') {
            return 1;
        }
    }
    return 0;
}

static int path_is_abs(const char* p)
{
    return p && p[0] == '/';
}

static void path_normalize(const char* in, char* out, int out_sz)
{
    char segs[32][SH_PATH_MAX];
    int seg_count = 0;
    int i = 0;

    if (!in || !out || out_sz < 2) {
        return;
    }

    while (in[i] == '/') {
        i++;
    }
    while (in[i] != '\0') {
        char seg[SH_PATH_MAX];
        int slen = 0;
        while (in[i] != '\0' && in[i] != '/' && slen + 1 < SH_PATH_MAX) {
            seg[slen++] = in[i++];
        }
        seg[slen] = '\0';
        while (in[i] == '/') {
            i++;
        }

        if (slen == 0 || (slen == 1 && seg[0] == '.')) {
            continue;
        }
        if (slen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (seg_count > 0) {
                seg_count--;
            }
            continue;
        }
        if (seg_count < 32) {
            for (int k = 0; k <= slen; k++) {
                segs[seg_count][k] = seg[k];
            }
            seg_count++;
        }
    }

    int p = 0;
    out[p++] = '/';
    for (int s = 0; s < seg_count; s++) {
        int k = 0;
        while (segs[s][k] != '\0') {
            if (p + 1 < out_sz) {
                out[p++] = segs[s][k];
            }
            k++;
        }
        if (s + 1 < seg_count && p + 1 < out_sz) {
            out[p++] = '/';
        }
    }
    if (p <= 0) {
        p = 1;
        out[0] = '/';
    }
    out[p] = '\0';
}

void resolve_path(const char* in, char* out, int out_sz)
{
    char tmp[SH_PATH_MAX];
    int p = 0;

    if (!in || !out || out_sz < 2) {
        return;
    }

    if (path_is_abs(in)) {
        path_normalize(in, out, out_sz);
        return;
    }

    for (int i = 0; shell_cwd[i] != '\0' && p + 1 < (int)sizeof(tmp); i++) {
        tmp[p++] = shell_cwd[i];
    }
    if (p == 0 || tmp[p - 1] != '/') {
        tmp[p++] = '/';
    }
    for (int i = 0; in[i] != '\0' && p + 1 < (int)sizeof(tmp); i++) {
        tmp[p++] = in[i];
    }
    tmp[p] = '\0';
    path_normalize(tmp, out, out_sz);
}

static void shell_ps1_copy(const char* src)
{
    int i = 0;
    if (!src) {
        shell_ps1[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1 < SH_PS1_MAX) {
        shell_ps1[i] = src[i];
        i++;
    }
    shell_ps1[i] = '\0';
}

int shell_set_ps1_from_args(int argc, char** argv, int start_idx)
{
    char buf[SH_PS1_MAX];
    int p = 0;
    if (!argv || start_idx < 0 || start_idx >= argc) {
        return -1;
    }

    for (int i = start_idx; i < argc && p + 1 < SH_PS1_MAX; i++) {
        if (i > start_idx && p + 1 < SH_PS1_MAX) {
            buf[p++] = ' ';
        }
        for (int j = 0; argv[i][j] != '\0' && p + 1 < SH_PS1_MAX; j++) {
            buf[p++] = argv[i][j];
        }
    }
    buf[p] = '\0';

    if (p >= 2 && ((buf[0] == '"' && buf[p - 1] == '"') ||
                   (buf[0] == '\'' && buf[p - 1] == '\''))) {
        buf[p - 1] = '\0';
        shell_ps1_copy(&buf[1]);
        return 0;
    }

    shell_ps1_copy(buf);
    return 0;
}
