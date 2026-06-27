fn check(board: i64, row: i64, col: i64) -> i64:
    var i = 0
    while i < row:
        let b = get_frame_ptr(board, i, 8)
        if b == col:
            return 0
        if b - i == col - row:
            return 0
        if b + i == col + row:
            return 0
        i += 1
    return 1

fn solve(board: i64, row: i64, n: i64, count: i64) -> i64:
    if row == n:
        return count + 1
    var col = 0
    var c = count
    while col < n:
        if check(board, row, col):
            let ptr = get_frame_ptr(board, row, 8)
            ptr = col
            c = solve(board, row + 1, n, c)
        col += 1
    return c

fn main() effect -> i64:
    let board = 0
    let result = solve(board, 0, 13, 0)
    print_int(result)
    return 0
