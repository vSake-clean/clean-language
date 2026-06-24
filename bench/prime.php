<?php
$n = 1000000;
$count = 0;
for ($i = 2; $i < $n; $i++) {
    $isPrime = true;
    for ($j = 2; $j * $j <= $i; $j++) {
        if ($i % $j == 0) { $isPrime = false; break; }
    }
    if ($isPrime) $count++;
}
echo "$count\n";
