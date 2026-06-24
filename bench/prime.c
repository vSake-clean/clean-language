#include <stdio.h>

int main(void) {
    int n = 1000000;
    int count = 0;
    for (int i = 2; i < n; i++) {
        int is_prime = 1;
        for (int j = 2; j * j <= i; j++) {
            if (i % j == 0) { is_prime = 0; break; }
        }
        if (is_prime) count++;
    }
    printf("%d\n", count);
    return 0;
}
