#include <stdio.h>

static int write_sample_file(const char* path)
{
    FILE* fp = fopen(path, "w");
    if (!fp) {
        perror("stdio_smoke: fopen write");
        return 1;
    }
    if (fprintf(fp, "alpha=%d\nbeta=%s\ngamma=0x%x\n", 7, "ok", 0x2A) < 0) {
        perror("stdio_smoke: fprintf");
        (void)fclose(fp);
        return 1;
    }
    if (fclose(fp) != 0) {
        perror("stdio_smoke: fclose write");
        return 1;
    }
    return 0;
}

static int read_sample_file(const char* path)
{
    char line[64];
    int count = 0;
    FILE* fp = fopen(path, "r");
    if (!fp) {
        perror("stdio_smoke: fopen read");
        return 1;
    }

    while (fgets(line, (int)sizeof(line), fp)) {
        count++;
        printf("[stdio-smoke] line%d: %s", count, line);
    }
    if (ferror(fp)) {
        perror("stdio_smoke: fgets");
        (void)fclose(fp);
        return 1;
    }
    if (fclose(fp) != 0) {
        perror("stdio_smoke: fclose read");
        return 1;
    }
    if (count != 3) {
        fprintf(stderr, "stdio_smoke: expected 3 lines, got %d\n", count);
        return 1;
    }
    return 0;
}

static int smoke_ungetc(const char* path)
{
    FILE* fp = fopen(path, "r");
    int c0;
    int c1;

    if (!fp) {
        perror("stdio_smoke: fopen ungetc");
        return 1;
    }
    c0 = fgetc(fp);
    if (c0 == EOF) {
        perror("stdio_smoke: fgetc");
        (void)fclose(fp);
        return 1;
    }
    if (ungetc(c0, fp) == EOF) {
        perror("stdio_smoke: ungetc");
        (void)fclose(fp);
        return 1;
    }
    c1 = fgetc(fp);
    if (c1 != c0) {
        fprintf(stderr, "stdio_smoke: ungetc mismatch %d != %d\n", c1, c0);
        (void)fclose(fp);
        return 1;
    }
    if (fclose(fp) != 0) {
        perror("stdio_smoke: fclose ungetc");
        return 1;
    }
    return 0;
}

int main(void)
{
    const char* path = "/tmp/stdio-smoke.txt";

    if (write_sample_file(path) != 0) {
        return 1;
    }
    if (read_sample_file(path) != 0) {
        return 1;
    }
    if (smoke_ungetc(path) != 0) {
        return 1;
    }

    printf("[stdio-smoke] PASS %s\n", path);
    return 0;
}
