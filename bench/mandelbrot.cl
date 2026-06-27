fn mandel(w: i64, h: i64, max: i64) -> i64:
    var count = 0
    var y = 0
    while y < h:
        var x = 0
        while x < w:
            let cx = x as i64 / w * 350 - 250
            let cy = y as i64 / h * 200 - 100
            var zx = 0
            var zy = 0
            var i = 0
            while i < max:
                let zx2 = zx * zx
                let zy2 = zy * zy
                if zx2 + zy2 >= 400:
                    break
                let nx = zx2 - zy2 + cx
                zy = 2 * zx * zy + cy
                zx = nx
                i += 1
            if i >= max:
                count += 1
            x += 1
        y += 1
    return count

fn main() effect -> i64:
    print_int(mandel(800, 800, 200))
    return 0
