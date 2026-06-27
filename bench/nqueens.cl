fn check(queens: i64, row: i64, col: i64) -> i64:
    var i = 0
    while i < row:
        let q = (queens >> (i * 4)) & 0xF
        if q == col:
            return 0
        if q - i == col - row:
            return 0
        if q + i == col + row:
            return 0
        i += 1
    return 1

fn solve(queens: i64, row: i64, n: i64, count: i64) -> i64:
    if row == n:
        return count + 1
    var col = 0
    var c = 0
    c = c + count
    while col < n:
        if check(queens, row, col) != 0:
            let nq = queens | (col << (row * 4))
            c = solve(nq, row + 1, n, c)
        col += 1
    return c

fn main() effect -> i64:
    let result = solve(0, 0, 13, 0)
    print_int(result)
    return 0
