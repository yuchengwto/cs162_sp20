#include <stdio.h>
#include <sys/resource.h>

int main() {
    struct rlimit lim;
    getrlimit(RLIMIT_STACK, &lim);
    long limit_stack = lim.rlim_cur;
    getrlimit(RLIMIT_NOFILE, &lim);
    long limit_nofile = lim.rlim_cur;
    getrlimit(RLIMIT_NPROC, &lim);
    long limit_proc = lim.rlim_cur;
    printf("stack size: %ld\n", limit_stack);
    printf("process limit: %ld\n", limit_proc);
    printf("max file descriptors: %ld\n", limit_nofile);
    return 0;
}
