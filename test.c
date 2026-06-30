#include <stdio.h>
#include <stdlib.h>
int *stackref(){
    int val;
    int *p = (int *)malloc(123);
    return &val;
}
int main(){
    int *x = (int *)malloc(16 * sizeof(int));
    int cc = x[14];
    printf("%d\n", cc);
    int *p = stackref();
    printf("%ls\n", p);
    int tt[10];
    for (int i = 0; i <= 10; i++){
        tt[i] = i;
    }
    return 0;
}