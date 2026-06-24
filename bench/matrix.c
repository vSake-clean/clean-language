#include <stdio.h>

int main(void) {
    int n = 100;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            for (int k = 0; k < n; k++) {
                volatile int sum = i * k + k * j;
                (void)sum;
            }
    printf("%d\n", n);
    return 0;
}
