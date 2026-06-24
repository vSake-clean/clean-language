#include "check.h"
#include "diag.h"
#include "ast.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_VARS 256

typedef enum { VS_ALIVE, VS_MOVED, VS_BORROWED, VS_MUT_BORROWED } VarState;

typedef struct {
    char name[64];
    VarState state;
    size_t decl_line;
    size_t decl_col;
} VarEntry;

typedef struct {
    VarEntry vars[MAX_VARS];
    int count;
    Diag *diag;
    const char *source;
    int has_effect;
} CheckCtx;

static void check_init(CheckCtx *c, Diag *d, const char *src) {
    c->count = 0;
    c->diag = d;
    c->source = src;
}

static int find_var(CheckCtx *c, const char *name) {
    for (int i = 0; i < c->count; i++)
        if (strcmp(c->vars[i].name, name) == 0) return i;
    return -1;
}

static int add_var(CheckCtx *c, const char *name, size_t line, size_t col) {
    if (c->count >= MAX_VARS) return -1;
    strncpy(c->vars[c->count].name, name, 63);
    c->vars[c->count].name[63] = 0;
    c->vars[c->count].state = VS_ALIVE;
    c->vars[c->count].decl_line = line;
    c->vars[c->count].decl_col = col;
    return c->count++;
}

static void check_expr(CheckCtx *c, Node *n);
static void check_stmt(CheckCtx *c, Node *n);

static void check_expr(CheckCtx *c, Node *n) {
    if (!n) return;

    switch (n->type) {
    case NODE_IDENT: {
        int idx = find_var(c, n->ident);
        if (idx < 0) return;
        if (c->vars[idx].state == VS_MOVED) {
            char buf[256];
            snprintf(buf, sizeof(buf), "use of moved value `%s`", n->ident);
            diag_add(c->diag, 1001, SEV_ERROR, 0, 0, 1, buf);
        }
        break;
    }
    case NODE_INT: case NODE_BOOL: case NODE_STR:
        break;
    case NODE_UNARY:
        check_expr(c, n->unary.operand);
        break;
    case NODE_BINARY:
        check_expr(c, n->binary.left);
        check_expr(c, n->binary.right);
        break;
    case NODE_CALL:
        if (!c->has_effect && n->call.callee->type == NODE_IDENT) {
            static const char *impure[] = {"print_int","read_int","sleep","input","calc_expr","time_ms","print_str",NULL};
            for (int i = 0; impure[i]; i++) {
                if (strcmp(n->call.callee->ident, impure[i]) == 0) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "call to impure function `%s` in pure function (add `effect`)", impure[i]);
                    diag_add(c->diag, 1003, SEV_WARN, 0, 0, 1, buf);
                    break;
                }
            }
        }
        check_expr(c, n->call.callee);
        for (Node *a = n->call.args; a; a = a->next)
            check_expr(c, a);
        break;
    case NODE_INDEX:
        check_expr(c, n->index_expr.obj);
        check_expr(c, n->index_expr.index);
        break;
    case NODE_STRUCT_LITERAL:
        for (Node *a = n->struct_literal.args; a; a = a->next)
            check_expr(c, a);
        break;
    case NODE_COMPREHENSION:
        check_expr(c, n->comp.map);
        check_expr(c, n->comp.iter);
        if (n->comp.iter_end) check_expr(c, n->comp.iter_end);
        if (n->comp.filter) check_expr(c, n->comp.filter);
        break;
    default: break;
    }
}

static void check_stmt(CheckCtx *c, Node *n) {
    if (!n) return;

    switch (n->type) {
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next)
            check_stmt(c, s);
        break;
    case NODE_LET: {
        add_var(c, n->let.name, 0, 0);
        if (n->let.init) {
            if (n->let.init->type == NODE_IDENT) {
                int idx = find_var(c, n->let.init->ident);
                if (idx >= 0 && c->vars[idx].state == VS_ALIVE) {
                    c->vars[idx].state = VS_MOVED;
                }
            }
            check_expr(c, n->let.init);
        }
        break;
    }
    case NODE_ASSIGN: {
        if (n->assign.lhs->type == NODE_IDENT) {
            int idx = find_var(c, n->assign.lhs->ident);
            if (idx >= 0 && c->vars[idx].state == VS_MOVED) {
                char buf[256];
                snprintf(buf, sizeof(buf), "cannot assign to moved variable `%s`", n->assign.lhs->ident);
                diag_add(c->diag, 1002, SEV_ERROR, 0, 0, 1, buf);
            }
        }
        check_expr(c, n->assign.lhs);
        check_expr(c, n->assign.rhs);
        break;
    }
    case NODE_IF:
        check_expr(c, n->if_stmt.cond);
        check_stmt(c, n->if_stmt.then);
        if (n->if_stmt.otherwise) check_stmt(c, n->if_stmt.otherwise);
        break;
    case NODE_WHILE:
        check_expr(c, n->while_stmt.cond);
        check_stmt(c, n->while_stmt.body);
        break;
    case NODE_RETURN:
        if (n->ret.val) check_expr(c, n->ret.val);
        break;
    case NODE_EXPR_STMT:
        check_expr(c, n->expr_stmt.expr);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        break;
    default: break;
    }
}

void check_program(Node *prog, Diag *diag, const char *source) {
    CheckCtx ctx;
    check_init(&ctx, diag, source);

    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_FN_DECL) {
            ctx.has_effect = item->fn.effect;
            for (Node *p = item->fn.params; p; p = p->next) {
                if (p->type == NODE_LET && p->let.name)
                    add_var(&ctx, p->let.name, 0, 0);
            }
            check_stmt(&ctx, item->fn.body);
        }
    }
}
