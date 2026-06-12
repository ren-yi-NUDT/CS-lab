// A.c
#include <stdio.h>
#include <unistd.h>

int main() {
    printf("PID : %d\n", getpid());
    while (1) {
        printf("A: I am running...\n");
        sleep(1);   // 每1秒输出一次
    }
    return 0;
}