w, h, max = 800, 800, 200
count = 0
for y in range(h):
    for x in range(w):
        cx = x / w * 3.5 - 2.5
        cy = y / h * 2.0 - 1.0
        zx = zy = 0.0
        i = 0
        while i < max and zx * zx + zy * zy < 4.0:
            nx = zx * zx - zy * zy + cx
            zy = 2.0 * zx * zy + cy
            zx = nx
            i += 1
        if i >= max:
            count += 1
print(count)
