<?php
class Node { public $l, $r; }
function make($d) {
    $n = new Node;
    if ($d > 0) { $n->l = make($d-1); $n->r = make($d-1); }
    return $n;
}
function check($n) {
    return $n->l ? check($n->l) + check($n->r) + 1 : 1;
}
$n = 21; $s = 0;
for ($i = 0; $i < 10; $i++) {
    $t = make($n);
    $s += check($t);
}
echo "$s\n";
