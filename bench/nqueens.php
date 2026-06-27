<?php
$count = 0;
function check($board, $row, $col) {
    for ($i = 0; $i < $row; $i++)
        if ($board[$i] == $col || $board[$i] - $i == $col - $row || $board[$i] + $i == $col + $row)
            return 0;
    return 1;
}
function solve(&$board, $row, $n) {
    global $count;
    if ($row == $n) { $count++; return; }
    for ($col = 0; $col < $n; $col++)
        if (check($board, $row, $col)) {
            $board[$row] = $col;
            solve($board, $row + 1, $n);
        }
}
$board = array_fill(0, 14, 0);
solve($board, 0, 13);
echo "$count\n";
