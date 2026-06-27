Node = Struct.new(:l, :r)

def make(d)
  n = Node.new(nil, nil)
  if d > 0
    n.l = make(d - 1)
    n.r = make(d - 1)
  end
  n
end

def check(n)
  n.l ? check(n.l) + check(n.r) + 1 : 1
end

n = 21; s = 0
10.times do
  t = make(n)
  s += check(t)
end
puts s
