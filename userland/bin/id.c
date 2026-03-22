#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define ID_GROUPS_MAX 32

static void print_uid_field(const char* label, uid_t uid)
{
    struct passwd* pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        printf("%s=%u(%s)", label, (unsigned)uid, pw->pw_name);
    } else {
        printf("%s=%u", label, (unsigned)uid);
    }
}

static void print_gid_field(const char* label, gid_t gid)
{
    struct group* gr = getgrgid(gid);
    if (label && label[0] != '\0') {
        if (gr && gr->gr_name) {
            printf("%s=%u(%s)", label, (unsigned)gid, gr->gr_name);
        } else {
            printf("%s=%u", label, (unsigned)gid);
        }
        return;
    }
    if (gr && gr->gr_name) {
        printf("%u(%s)", (unsigned)gid, gr->gr_name);
    } else {
        printf("%u", (unsigned)gid);
    }
}

int main(void)
{
    gid_t gids[ID_GROUPS_MAX];
    int count = getgroups(ID_GROUPS_MAX, gids);
    gid_t primary = getgid();

    if (count < 0) {
        perror("id");
        return 1;
    }

    print_uid_field("uid", getuid());
    putchar(' ');
    print_gid_field("gid", primary);
    putchar(' ');
    print_uid_field("euid", geteuid());
    putchar(' ');
    print_gid_field("egid", getegid());

    printf(" groups=");
    print_gid_field("", primary);
    for (int i = 0; i < count; i++) {
        if (gids[i] == primary) {
            continue;
        }
        putchar(',');
        print_gid_field("", gids[i]);
    }
    putchar('\n');
    return 0;
}
