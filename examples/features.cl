enum Color:
    Red
    Green
    Blue

fn main() effect -> i32:
    let x = 42
    let y = 10
    print_int(x + y)
    print_int(x | y)
    print_int(x & y)
    print_int(y << 2)
    print_int(y >> 1)
    print_int(2 ** 10)
    print_int(x > y)
    print_int(x < y)
    let a = true
    let b = false
    print_int(a and b)
    print_int(a or b)
    print_int(not a)
    let c = Color(Green)
    match c:
        Red:
            print_int(0)
        Green:
            print_int(1)
        Blue:
            print_int(2)
    let r = &x
    print_int(*r)
    for i in 1..3:
        print_int(i)
    let s = "hello"
    print_str(s, 5)
    return 0
