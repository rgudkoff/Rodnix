#include <stdio.h>
#include <unistd.h>
#include <pwd.h>

int main(void)
{
    uid_t uid = getuid();
    struct passwd* pw = getpwuid(uid);

    if (pw && pw->pw_name && pw->pw_name[0] != '\0') {
        puts(pw->pw_name);
        return 0;
    }

    printf("%u\n", (unsigned)uid);
    return 0;
}
