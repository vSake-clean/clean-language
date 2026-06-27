#include <stdio.h>
#include <stdlib.h>

typedef struct Node { struct Node *l, *r; } Node;

Node *make(int d) {
    Node *n = (Node*)malloc(sizeof(Node));
    if (d > 0) { n->l = make(d-1); n->r = make(d-1); }
    else { n->l = n->r = 0; }
    return n;
}

int check(Node *n) {
    return n->l ? check(n->l) + check(n->r) + 1 : 1;
}

void free_tree(Node *n) {
    if (n->l) { free_tree(n->l); free_tree(n->r); }
    free(n);
}

int main() {
    int n = 21, sum = 0;
    for (int i = 0; i < 10; i++) {
        Node *t = make(n);
        sum += check(t);
        free_tree(t);
    }
    printf("%d\n", sum);
    return 0;
}
