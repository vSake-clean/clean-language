def check(board, row, col):
    for i in range(row):
        if board[i] == col or board[i] - i == col - row or board[i] + i == col + row:
            return False
    return True

def solve(board, row, n):
    global count
    if row == n:
        count += 1
        return
    for col in range(n):
        if check(board, row, col):
            board[row] = col
            solve(board, row + 1, n)

count = 0
solve([0]*14, 0, 13)
print(count)
