fn main() -> i32
    var i = 0
    while i < 100
        var j = 0
        while j < 100
            var k = 0
            var sum = 0
            while k < 100
                sum += i * k + k * j
                k += 1
            j += 1
        i += 1
    print_int(i)
    return 0
