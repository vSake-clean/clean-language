#include <stdio.h>
int main() {
    int w = 800, h = 800, max = 200, count = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double cx = (double)x / w * 3.5 - 2.5;
            double cy = (double)y / h * 2.0 - 1.0;
            double zx = 0, zy = 0;
            int i = 0;
            while (i < max && zx * zx + zy * zy < 4.0) {
                double nx = zx * zx - zy * zy + cx;
                zy = 2.0 * zx * zy + cy;
                zx = nx;
                i++;
            }
            if (i >= max) count++;
        }
    printf("%d\n", count);
    return 0;
}
