fn main() effect -> i64:
    let n = 1000000
    var count = 0
    var i = 2
    while i < n:
        var j = 2
        var is_prime = 1
        while j * j <= i:
            if i % j == 0:
                is_prime = 0
                break
            j += 1
        if is_prime:
            count += 1
        i += 1
    print_int(count)
    return 0
