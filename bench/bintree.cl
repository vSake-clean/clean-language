fn make(d: i64) -> i64:
    let n = Node()
    if d > 0:
        n.l = make(d - 1)
        n.r = make(d - 1)
    return n

fn check(n: i64) -> i64:
    if n.l == 0:
        return 1
    return check(n.l) + check(n.r) + 1

fn main() effect -> i64:
    let n = 21
    var s = 0
    var i = 0
    while i < 10:
        let t = make(n)
        s += check(t)
        i += 1
    print_int(s)
    return 0
