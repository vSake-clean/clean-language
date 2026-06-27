$count = 0

def check(board, row, col)
  for i in 0...row
    if board[i] == col || board[i] - i == col - row || board[i] + i == col + row
      return false
    end
  end
  true
end

def solve(board, row, n)
  if row == n
    $count += 1
    return
  end
  (0...n).each do |col|
    if check(board, row, col)
      board[row] = col
      solve(board, row + 1, n)
    end
  end
end

board = [0] * 14
solve(board, 0, 13)
puts $count
