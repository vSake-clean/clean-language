<?php
$w = 800; $h = 800; $max = 200; $count = 0;
for ($y = 0; $y < $h; $y++)
    for ($x = 0; $x < $w; $x++) {
        $cx = $x / $w * 3.5 - 2.5;
        $cy = $y / $h * 2.0 - 1.0;
        $zx = 0.0; $zy = 0.0;
        $i = 0;
        while ($i < $max && $zx * $zx + $zy * $zy < 4.0) {
            $nx = $zx * $zx - $zy * $zy + $cx;
            $zy = 2.0 * $zx * $zy + $cy;
            $zx = $nx;
            $i++;
        }
        if ($i >= $max) $count++;
    }
echo "$count\n";
