class Node:
    def __init__(self, d):
        self.l = None
        self.r = None
        if d > 0:
            self.l = Node(d-1)
            self.r = Node(d-1)

def check(n):
    return check(n.l) + check(n.r) + 1 if n.l else 1

n = 21
s = 0
for _ in range(10):
    t = Node(n)
    s += check(t)
print(s)
