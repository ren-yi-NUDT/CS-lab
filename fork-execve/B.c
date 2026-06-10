// B.c
#include <stdio.h>
#include <unistd.h>

int main() {
    while (1) {
        printf("B: I am running...\n");
        sleep(2);   // 每2秒输出一次
    }
    return 0;
}