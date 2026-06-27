w = 800; h = 800; max = 200; count = 0
(0...h).each do |y|
  (0...w).each do |x|
    cx = x.to_f / w * 3.5 - 2.5
    cy = y.to_f / h * 2.0 - 1.0
    zx = 0.0; zy = 0.0; i = 0
    while i < max && zx * zx + zy * zy < 4.0
      nx = zx * zx - zy * zy + cx
      zy = 2.0 * zx * zy + cy
      zx = nx
      i += 1
    end
    count += 1 if i >= max
  end
end
puts count
