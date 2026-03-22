/*
 * ls — list directory contents
 * Flags: -a (all), -l (long), -1 (one per line), -h (human sizes)
 */
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/ioctl.h>

static int opt_all   = 0; /* -a */
static int opt_long  = 0; /* -l */
static int opt_one   = 0; /* -1 */
static int opt_human = 0; /* -h */
static int use_color = 0;

typedef struct ls_entry {
    char* name;
    uint8_t dtype;
    size_t display_width;
    struct stat st;
    int stat_ok;
} ls_entry_t;

typedef struct ls_long_widths {
    size_t owner;
    size_t group;
    size_t size;
} ls_long_widths_t;

/* ── helpers ─────────────────────────────────────────────── */

static size_t str_width(const char* s)
{
    size_t n = 0;
    while (s && s[n] != '\0') {
        n++;
    }
    return n;
}

static size_t entry_display_width(const char* name, uint8_t dtype)
{
    return str_width(name) + ((dtype == DT_DIR) ? 1u : 0u);
}

static size_t digits_u64(unsigned long long v)
{
    size_t n = 1;
    while (v >= 10ULL) {
        v /= 10ULL;
        n++;
    }
    return n;
}

static int cmp_entries_by_name(const void* lhs, const void* rhs)
{
    const ls_entry_t* a = (const ls_entry_t*)lhs;
    const ls_entry_t* b = (const ls_entry_t*)rhs;
    return strcmp(a->name, b->name);
}

static int terminal_columns(void)
{
    struct winsize ws;
    const char* env;

    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, RDNX_TTY_IOCTL_GETWINSZ, &ws) == 0) {
        if (ws.ws_col >= 20 && ws.ws_col <= 1000) {
            return (int)ws.ws_col;
        }
    }

    env = getenv("COLUMNS");
    if (env && env[0] != '\0') {
        long v = strtol(env, NULL, 10);
        if (v >= 20 && v <= 1000) {
            return (int)v;
        }
    }
    return 80;
}

static void mode_str(mode_t m, char out[11])
{
    out[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : '-';
    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    out[3] = (m & S_IXUSR) ? 'x' : '-';
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    out[6] = (m & S_IXGRP) ? 'x' : '-';
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    out[9] = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static void human_size(long long sz, char out[16])
{
    if (sz < 1024LL) {
        snprintf(out, 16, "%lldB", sz);
    } else if (sz < 1024LL * 1024) {
        snprintf(out, 16, "%lldK", sz / 1024);
    } else if (sz < 1024LL * 1024 * 1024) {
        snprintf(out, 16, "%lldM", sz / (1024 * 1024));
    } else {
        snprintf(out, 16, "%lldG", sz / (1024 * 1024 * 1024));
    }
}

static const char* month_name(int month)
{
    static const char* names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (month < 0 || month >= 12) {
        return "???";
    }
    return names[month];
}

static void format_mtime(time_t t, char out[20])
{
    struct tm* tm = localtime(&t);
    if (!tm) {
        snprintf(out, 20, "??? ?? ??:??");
        return;
    }
    snprintf(out, 20, "%s %2d %02d:%02d",
             month_name(tm->tm_mon),
             tm->tm_mday,
             tm->tm_hour,
             tm->tm_min);
}

static void print_name(const char* name, uint8_t dtype)
{
    if (use_color) {
        if (dtype == DT_DIR) {
            printf("\033[1;34m%s\033[0m/", name);
        } else {
            printf("%s", name);
        }
    } else {
        printf("%s%s", name, dtype == DT_DIR ? "/" : "");
    }
}

static const char* owner_name(uid_t uid, char* fallback, size_t fallback_size)
{
    struct passwd* pw = getpwuid(uid);
    if (pw && pw->pw_name && pw->pw_name[0] != '\0') {
        return pw->pw_name;
    }
    snprintf(fallback, fallback_size, "%u", (unsigned)uid);
    return fallback;
}

static const char* group_name(gid_t gid, char* fallback, size_t fallback_size)
{
    struct group* gr = getgrgid(gid);
    if (gr && gr->gr_name && gr->gr_name[0] != '\0') {
        return gr->gr_name;
    }
    snprintf(fallback, fallback_size, "%u", (unsigned)gid);
    return fallback;
}

static void compute_long_widths(const ls_entry_t* entries, size_t count, ls_long_widths_t* widths)
{
    char owner_fallback[32];
    char group_fallback[32];
    char sbuf[16];

    widths->owner = 1;
    widths->group = 1;
    widths->size = 1;

    for (size_t i = 0; i < count; i++) {
        const char* owner;
        const char* group;
        size_t size_width;

        if (!entries[i].stat_ok) {
            continue;
        }
        owner = owner_name(entries[i].st.st_uid, owner_fallback, sizeof(owner_fallback));
        group = group_name(entries[i].st.st_gid, group_fallback, sizeof(group_fallback));
        if (opt_human) {
            human_size((long long)entries[i].st.st_size, sbuf);
            size_width = str_width(sbuf);
        } else {
            size_width = digits_u64((unsigned long long)((entries[i].st.st_size < 0) ? 0 : entries[i].st.st_size));
        }

        if (str_width(owner) > widths->owner) {
            widths->owner = str_width(owner);
        }
        if (str_width(group) > widths->group) {
            widths->group = str_width(group);
        }
        if (size_width > widths->size) {
            widths->size = size_width;
        }
    }
}

static void print_long_entry(const ls_entry_t* entry, const ls_long_widths_t* widths)
{
    char mbuf[11];
    char sbuf[16];
    char tbuf[20];
    char owner_fallback[32];
    char group_fallback[32];
    const char* owner;
    const char* group;

    if (!entry->stat_ok) {
        print_name(entry->name, entry->dtype);
        putchar('\n');
        return;
    }

    mode_str(entry->st.st_mode, mbuf);
    if (opt_human) {
        human_size((long long)entry->st.st_size, sbuf);
    } else {
        snprintf(sbuf, sizeof(sbuf), "%lld", (long long)entry->st.st_size);
    }
    owner = owner_name(entry->st.st_uid, owner_fallback, sizeof(owner_fallback));
    group = group_name(entry->st.st_gid, group_fallback, sizeof(group_fallback));
    format_mtime(entry->st.st_mtime, tbuf);

    if (use_color) {
        if (S_ISDIR(entry->st.st_mode)) {
            printf("%s %-*s %-*s %*s %s \033[1;34m%s\033[0m/\n",
                   mbuf,
                   (int)widths->owner, owner,
                   (int)widths->group, group,
                   (int)widths->size, sbuf,
                   tbuf,
                   entry->name);
        } else if (entry->st.st_mode & S_IXUSR) {
            printf("%s %-*s %-*s %*s %s \033[1;32m%s\033[0m\n",
                   mbuf,
                   (int)widths->owner, owner,
                   (int)widths->group, group,
                   (int)widths->size, sbuf,
                   tbuf,
                   entry->name);
        } else {
            printf("%s %-*s %-*s %*s %s %s\n",
                   mbuf,
                   (int)widths->owner, owner,
                   (int)widths->group, group,
                   (int)widths->size, sbuf,
                   tbuf,
                   entry->name);
        }
    } else {
        const char *suf = (entry->dtype == DT_DIR) ? "/" : "";
        printf("%s %-*s %-*s %*s %s %s%s\n",
               mbuf,
               (int)widths->owner, owner,
               (int)widths->group, group,
               (int)widths->size, sbuf,
               tbuf,
               entry->name, suf);
    }
}

static int collect_entries(DIR* d, ls_entry_t** out_entries, size_t* out_count)
{
    struct dirent* de;
    ls_entry_t* entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    while ((de = readdir(d)) != NULL) {
        ls_entry_t* grown;
        size_t name_len;
        char* copy;

        if (!opt_all && de->d_name[0] == '.') {
            continue;
        }
        if (count == capacity) {
            size_t next_capacity = capacity ? capacity * 2u : 32u;
            grown = (ls_entry_t*)realloc(entries, next_capacity * sizeof(ls_entry_t));
            if (!grown) {
                free(entries);
                return -1;
            }
            entries = grown;
            capacity = next_capacity;
        }
        name_len = str_width(de->d_name);
        copy = (char*)malloc(name_len + 1u);
        if (!copy) {
            for (size_t i = 0; i < count; i++) {
                free(entries[i].name);
            }
            free(entries);
            return -1;
        }
        memcpy(copy, de->d_name, name_len + 1u);
        entries[count].name = copy;
        entries[count].dtype = de->d_type;
        entries[count].display_width = entry_display_width(copy, de->d_type);
        entries[count].stat_ok = 0;
        count++;
    }

    *out_entries = entries;
    *out_count = count;
    return 0;
}

static void free_entries(ls_entry_t* entries, size_t count)
{
    if (!entries) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}

static void print_short_entries(ls_entry_t* entries, size_t count)
{
    size_t max_width = 0;
    int term_cols;
    size_t col_width;
    size_t cols;
    size_t rows;

    if (count == 0) {
        putchar('\n');
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (entries[i].display_width > max_width) {
            max_width = entries[i].display_width;
        }
    }

    if (!isatty(STDOUT_FILENO) || opt_one) {
        for (size_t i = 0; i < count; i++) {
            print_name(entries[i].name, entries[i].dtype);
            putchar('\n');
        }
        return;
    }

    term_cols = terminal_columns();
    col_width = max_width + 2u;
    if (col_width == 0) {
        col_width = 1;
    }
    cols = (size_t)term_cols / col_width;
    if (cols == 0) {
        cols = 1;
    }
    rows = (count + cols - 1u) / cols;

    for (size_t row = 0; row < rows; row++) {
        for (size_t col = 0; col < cols; col++) {
            size_t idx = col * rows + row;
            size_t pad;
            if (idx >= count) {
                continue;
            }
            print_name(entries[idx].name, entries[idx].dtype);
            if (col + 1u < cols) {
                pad = col_width - entries[idx].display_width;
                while (pad-- > 0) {
                    putchar(' ');
                }
            }
        }
        putchar('\n');
    }
}

static void fill_long_stats(const char* dir, ls_entry_t* entries, size_t count)
{
    char path[512];

    for (size_t i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, entries[i].name);
        memset(&entries[i].st, 0, sizeof(entries[i].st));
        entries[i].stat_ok = (lstat(path, &entries[i].st) == 0);
    }
}

static int ls_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    ls_entry_t* entries = NULL;
    size_t count = 0;
    if (collect_entries(d, &entries, &count) != 0) {
        closedir(d);
        fprintf(stderr, "ls: %s: out of memory\n", path);
        return 1;
    }
    closedir(d);
    qsort(entries, count, sizeof(ls_entry_t), cmp_entries_by_name);

    if (opt_long) {
        ls_long_widths_t widths;
        fill_long_stats(path, entries, count);
        compute_long_widths(entries, count, &widths);
        for (size_t i = 0; i < count; i++) {
            print_long_entry(&entries[i], &widths);
        }
    } else {
        print_short_entries(entries, count);
    }
    free_entries(entries, count);
    return 0;
}

int main(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "al1h")) != -1) {
        switch (c) {
        case 'a': opt_all   = 1; break;
        case 'l': opt_long  = 1; break;
        case '1': opt_one   = 1; break;
        case 'h': opt_human = 1; break;
        default:
            fprintf(stderr, "usage: ls [-al1h] [path...]\n");
            return 1;
        }
    }

    use_color = isatty(STDOUT_FILENO);

    if (optind >= argc) {
        return ls_dir(".");
    }

    int ret = 0;
    for (int i = optind; i < argc; i++) {
        if (argc - optind > 1) printf("%s:\n", argv[i]);
        ret |= ls_dir(argv[i]);
    }
    return ret;
}
