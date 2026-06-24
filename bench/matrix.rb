n = 100
for i in 0...n
  for j in 0...n
    for k in 0...n
      s = i * k + k * j
    end
  end
end
puts n
