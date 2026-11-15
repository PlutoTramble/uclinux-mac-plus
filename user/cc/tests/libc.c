#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>

static int sum(int n, ...)
{
    va_list ap;
    int s = 0;
    va_start(ap, n);
    while (n-- > 0)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(int argc, char **argv)
{
    char buf[64];
    char *p = malloc(32);
    int fd = open("/var/tmp/ccx", O_RDWR | O_CREAT, 0644);
    struct stat st;
    uint32_t u = 7;
    strcpy(buf, "abc");
    if (strcmp(buf, "abc") == 0 && fd >= 0) {
        write(fd, buf, 3);
        fstat(fd, &st);
        close(fd);
    }
    printf("%d %lu %d\n", argc, (unsigned long)st.st_size, sum(3, 1, 2, 3));
    free(p);
    printf("%f %f\n", sqrt(2.0), (double)u / 3);
    return 0;
}
