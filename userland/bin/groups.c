#include <stdio.h>
#include <unistd.h>
#include <grp.h>

#define GROUPS_MAX 32

int main(void)
{
    gid_t gids[GROUPS_MAX];
    int count = getgroups(GROUPS_MAX, gids);
    gid_t primary = getgid();
    int printed = 0;

    if (count < 0) {
        perror("groups");
        return 1;
    }

    {
        struct group* gr = getgrgid(primary);
        if (gr && gr->gr_name) {
            printf("%s", gr->gr_name);
        } else {
            printf("%u", (unsigned)primary);
        }
        printed = 1;
    }

    for (int i = 0; i < count; i++) {
        struct group* gr;
        if (gids[i] == primary) {
            continue;
        }
        if (printed) {
            putchar(' ');
        }
        gr = getgrgid(gids[i]);
        if (gr && gr->gr_name) {
            printf("%s", gr->gr_name);
        } else {
            printf("%u", (unsigned)gids[i]);
        }
        printed = 1;
    }

    putchar('\n');
    return 0;
}
