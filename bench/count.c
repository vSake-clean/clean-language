#include <stdio.h>

int main(void) {
    int n = 1000000000;
    volatile int i = 0;
    while (i < n) i++;
    printf("%d\n", i);
    return 0;
}
