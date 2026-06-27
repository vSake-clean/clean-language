#include <stdio.h>

int count = 0;

int check(int *board, int row, int col) {
    for (int i = 0; i < row; i++)
        if (board[i] == col || board[i] - i == col - row || board[i] + i == col + row)
            return 0;
    return 1;
}

void solve(int *board, int row, int n) {
    if (row == n) { count++; return; }
    for (int col = 0; col < n; col++)
        if (check(board, row, col)) {
            board[row] = col;
            solve(board, row + 1, n);
        }
}

int main() {
    int board[15];
    solve(board, 0, 13);
    printf("%d\n", count);
    return 0;
}
