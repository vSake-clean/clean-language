fn mandel(w: float, h: float, max: float) -> i64:
    var count = 0
    var y = 0.0
    while y < h:
        var x = 0.0
        while x < w:
            let cx = x / w * 3.5 - 2.5
            let cy = y / h * 2.0 - 1.0
            var zx = 0.0
            var zy = 0.0
            var i = 0.0
            while i < max:
                let zx2 = zx * zx
                let zy2 = zy * zy
                if zx2 + zy2 >= 4.0:
                    break
                let nx = zx2 - zy2 + cx
                zy = 2.0 * zx * zy + cy
                zx = nx
                i += 1.0
            if i >= max:
                count += 1
            x += 1.0
        y += 1.0
    return count

fn main() effect -> i64:
    print_int(mandel(800.0, 800.0, 200.0))
    return 0
