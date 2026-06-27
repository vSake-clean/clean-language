struct Node:
    l
    r

extern fn free(x: i64)

fn free_tree(n: i64) -> i64:
    if n.l != 0:
        free_tree(n.l)
        free_tree(n.r)
    free(n)
    return 0

fn make(d: i64) -> i64:
    if d > 0:
        return Node(make(d - 1), make(d - 1))
    return Node(0, 0)

fn check(n: i64) -> i64:
    if n.l == 0:
        return 1
    return check(n.l) + check(n.r) + 1

fn main() effect -> i64:
    var s = 0
    var i = 0
    while i < 10:
        let t = make(21)
        s += check(t)
        free_tree(t)
        i += 1
    print_int(s)
    return 0
