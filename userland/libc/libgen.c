#include <libgen.h>
#include <string.h>

/*
 * basename(3) — POSIX, modifies path in place.
 * Returns pointer into path (or a static literal for edge cases).
 */
char* basename(char* path)
{
    char* p;
    size_t len;

    if (!path || path[0] == '\0') {
        return ".";
    }
    len = strlen(path);
    /* Strip trailing slashes, but keep at least one character. */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    /* Pure-slash path (e.g. "/") */
    if (len == 1 && path[0] == '/') {
        return path;
    }
    p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/*
 * dirname(3) — POSIX, modifies path in place.
 * Returns pointer into path (or a static literal for edge cases).
 */
char* dirname(char* path)
{
    size_t len;
    char* p;

    if (!path || path[0] == '\0') {
        return ".";
    }
    len = strlen(path);
    /* Strip trailing slashes, but keep at least one character. */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    p = strrchr(path, '/');
    if (!p) {
        return ".";
    }
    if (p == path) {
        /* path is "/something" — directory is root */
        return "/";
    }
    *p = '\0';
    return path;
}
