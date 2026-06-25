#include "check.h"
#include "diag.h"
#include "ast.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define MAX_VARS 256

typedef enum { VS_ALIVE, VS_MOVED, VS_BORROWED, VS_MUT_BORROWED } VarState;

typedef enum { TYPE_UNKNOWN, TYPE_I64, TYPE_BOOL, TYPE_STR, TYPE_STRUCT } ValType;

typedef struct {
    char name[64];
    VarState state;
    ValType type;
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

static int add_var(CheckCtx *c, const char *name, ValType type, size_t line, size_t col) {
    if (c->count >= MAX_VARS) return -1;
    strncpy(c->vars[c->count].name, name, 63);
    c->vars[c->count].name[63] = 0;
    c->vars[c->count].state = VS_ALIVE;
    c->vars[c->count].type = type;
    c->vars[c->count].decl_line = line;
    c->vars[c->count].decl_col = col;
    return c->count++;
}

static ValType get_type_for_name(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    if (strcmp(name, "i64") == 0 || strcmp(name, "i32") == 0 || strcmp(name, "i8") == 0) return TYPE_I64;
    if (strcmp(name, "bool") == 0) return TYPE_BOOL;
    if (strcmp(name, "str") == 0 || strcmp(name, "String") == 0) return TYPE_STR;
    return TYPE_UNKNOWN;
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
            diag_add(c->diag, 1001, SEV_ERROR, n->src_line, n->src_col, 1, buf);
        }
        break;
    }
    case NODE_INT:
        break;
    case NODE_FLOAT:
        break;
    case NODE_BOOL:
        break;
    case NODE_STR:
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
                    diag_add(c->diag, 1003, SEV_WARN, n->src_line, n->src_col, 1, buf);
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
    case NODE_BORROW:
    case NODE_MUT_BORROW:
        check_expr(c, n->borrow.operand);
        break;
    case NODE_DEREF:
        check_expr(c, n->borrow.operand);
        break;
    case NODE_ENUM_LITERAL:
        if (n->enum_literal.payload) check_expr(c, n->enum_literal.payload);
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
        ValType vt = TYPE_UNKNOWN;
        if (n->let.type && n->let.type->type == NODE_IDENT) {
            vt = get_type_for_name(n->let.type->ident);
        }
        add_var(c, n->let.name, vt, n->src_line, n->src_col);
        if (n->let.init) {
            if (n->let.init->type == NODE_IDENT) {
                int idx = find_var(c, n->let.init->ident);
                if (idx >= 0 && c->vars[idx].state == VS_ALIVE && vt == TYPE_UNKNOWN) {
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
                diag_add(c->diag, 1002, SEV_ERROR, n->src_line, n->src_col, 1, buf);
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
    case NODE_MATCH:
        check_expr(c, n->match.expr);
        for (Node *arm = n->match.arms; arm; arm = arm->next) {
            if (arm->match_arm.payload) check_expr(c, arm->match_arm.payload);
            if (arm->match_arm.guard) check_expr(c, arm->match_arm.guard);
            check_stmt(c, arm->match_arm.body);
        }
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
                if (p->type == NODE_LET && p->let.name) {
                    ValType vt = TYPE_UNKNOWN;
                    if (p->let.type && p->let.type->type == NODE_IDENT) {
                        vt = get_type_for_name(p->let.type->ident);
                    }
                    add_var(&ctx, p->let.name, vt, p->src_line, p->src_col);
                }
            }
            check_stmt(&ctx, item->fn.body);
        }
    }
}
