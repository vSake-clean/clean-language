#include "check.h"
#include "diag.h"
#include "ast.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define MAX_VARS 256

typedef enum { VS_ALIVE, VS_MOVED, VS_BORROWED, VS_MUT_BORROWED } VarState;

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
    ValType ret_type;
} CheckCtx;

const char *valtype_str(ValType t) {
    switch (t) {
        case TYPE_I64: return "i64";
        case TYPE_BOOL: return "bool";
        case TYPE_STR: return "str";
        case TYPE_FLOAT: return "float";
        case TYPE_VOID: return "void";
        case TYPE_CHAR: return "char";
        default: return "unknown";
    }
}

ValType infer_node_type(Node *n) {
    if (!n) return TYPE_VOID;
    switch (n->type) {
        case NODE_INT: return n->char_flag ? TYPE_CHAR : TYPE_I64;
        case NODE_FLOAT: return TYPE_FLOAT;
        case NODE_BOOL: return TYPE_BOOL;
        case NODE_STR: return TYPE_STR;
        case NODE_IDENT: return TYPE_UNKNOWN; /* needs context */
        case NODE_UNARY:
            if (n->unary.op == 0) return TYPE_BOOL; /* not */
            return infer_node_type(n->unary.operand);
        case NODE_BINARY: {
            ValType lt = infer_node_type(n->binary.left);
            ValType rt = infer_node_type(n->binary.right);
            if (lt == TYPE_FLOAT || rt == TYPE_FLOAT) return TYPE_FLOAT;
            if (n->binary.op >= 5 && n->binary.op <= 10) return TYPE_BOOL; /* comparisons */
            if (n->binary.op >= 11 && n->binary.op <= 12) return TYPE_BOOL; /* and/or */
            return lt;
        }
        case NODE_CALL:
            if (n->call.callee->type == NODE_IDENT) {
                const char *fn = n->call.callee->ident;
                if (strcmp(fn, "print_int") == 0 || strcmp(fn, "print_float") == 0 ||
                    strcmp(fn, "print_str") == 0 || strcmp(fn, "sleep") == 0 ||
                    strcmp(fn, "assert") == 0 || strcmp(fn, "reset_attr") == 0 ||
                    strcmp(fn, "set_fg") == 0 || strcmp(fn, "set_bg") == 0 ||
                    strcmp(fn, "hide_cursor") == 0 || strcmp(fn, "show_cursor") == 0 ||
                    strcmp(fn, "clear_screen") == 0)
                    return TYPE_VOID;
                if (strcmp(fn, "read_int") == 0 || strcmp(fn, "time_ms") == 0 ||
                    strcmp(fn, "strlen") == 0 || strcmp(fn, "calc_expr") == 0 ||
                    strcmp(fn, "input") == 0) return TYPE_I64;
                if (strcmp(fn, "inspect") == 0) {
                    if (n->call.args) return infer_node_type(n->call.args);
                    return TYPE_I64;
                }
            }
            return TYPE_I64;
        case NODE_DEREF: return TYPE_UNKNOWN;
        case NODE_BORROW:
        case NODE_MUT_BORROW: return TYPE_I64; /* pointer */
        case NODE_STRUCT_LITERAL: return TYPE_UNKNOWN;
        case NODE_ENUM_LITERAL: return TYPE_UNKNOWN;
        case NODE_INDEX:
        case NODE_NULLSAFE: return TYPE_I64;
        default: return TYPE_UNKNOWN;
    }
}

int valtype_size(ValType t) {
    switch (t) {
        case TYPE_BOOL:
        case TYPE_CHAR: return 1;
        default: return 8;
    }
}

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
    if (strcmp(name, "float") == 0) return TYPE_FLOAT;
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
    case NODE_FLOAT:
    case NODE_BOOL:
    case NODE_STR:
        break;
    case NODE_UNARY:
        check_expr(c, n->unary.operand);
        if (n->unary.op == 3 && n->unary.operand->type == NODE_IDENT) {
            int idx = find_var(c, n->unary.operand->ident);
            if (idx >= 0 && c->vars[idx].state == VS_ALIVE)
                c->vars[idx].state = VS_MOVED;
        }
        break;
    case NODE_BINARY:
        check_expr(c, n->binary.left);
        check_expr(c, n->binary.right);
        break;
    case NODE_CALL:
        if (!c->has_effect && n->call.callee->type == NODE_IDENT) {
            static const char *impure[] = {"print_int","read_int","sleep","input","calc_expr","time_ms","print_str","print_float","set_fg","set_bg","clear_screen","reset_attr","hide_cursor","show_cursor",NULL};
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
    case NODE_NULLSAFE:
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

static void check_type_mismatch(CheckCtx *c, Node *n, ValType expected, ValType got) {
    if (expected == TYPE_UNKNOWN || got == TYPE_UNKNOWN || expected == got) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "type mismatch: expected `%s`, got `%s`", valtype_str(expected), valtype_str(got));
    diag_add(c->diag, 1004, SEV_ERROR, n->src_line, n->src_col, 1, buf);
}

static void check_stmt(CheckCtx *c, Node *n) {
    if (!n) return;

    switch (n->type) {
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next)
            check_stmt(c, s);
        break;
    case NODE_LET: {
        ValType declared = TYPE_UNKNOWN;
        if (n->let.type && n->let.type->type == NODE_IDENT) {
            declared = get_type_for_name(n->let.type->ident);
        }
        if (n->let.init) {
            /* check move semantics */
            if (n->let.init->type == NODE_IDENT) {
                int idx = find_var(c, n->let.init->ident);
                if (idx >= 0 && c->vars[idx].state == VS_ALIVE && declared == TYPE_UNKNOWN) {
                    c->vars[idx].state = VS_MOVED;
                }
            }
            check_expr(c, n->let.init);
            /* type check: compare declared type with inferred type */
            if (declared != TYPE_UNKNOWN) {
                ValType inferred = infer_node_type(n->let.init);
                if (inferred == TYPE_UNKNOWN) {
                    /* try looking up variable types */
                    if (n->let.init->type == NODE_IDENT) {
                        int idx = find_var(c, n->let.init->ident);
                        if (idx >= 0) inferred = c->vars[idx].type;
                    }
                }
                check_type_mismatch(c, n->let.init, declared, inferred);
            }
        }
        add_var(c, n->let.name, declared, n->src_line, n->src_col);
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
            if (idx >= 0 && c->vars[idx].type != TYPE_UNKNOWN) {
                ValType rhs_type = infer_node_type(n->assign.rhs);
                if (rhs_type == TYPE_UNKNOWN && n->assign.rhs->type == NODE_IDENT) {
                    int ridx = find_var(c, n->assign.rhs->ident);
                    if (ridx >= 0) rhs_type = c->vars[ridx].type;
                }
                check_type_mismatch(c, n->assign.rhs, c->vars[idx].type, rhs_type);
            }
        }
        check_expr(c, n->assign.lhs);
        check_expr(c, n->assign.rhs);
        break;
    }
    case NODE_IF:
        check_expr(c, n->if_stmt.cond);
        if (n->if_stmt.cond) {
            ValType ct = infer_node_type(n->if_stmt.cond);
            if (ct == TYPE_UNKNOWN && n->if_stmt.cond->type == NODE_IDENT) {
                int idx = find_var(c, n->if_stmt.cond->ident);
                if (idx >= 0) ct = c->vars[idx].type;
            }
            if (ct != TYPE_BOOL && ct != TYPE_UNKNOWN)
                check_type_mismatch(c, n->if_stmt.cond, TYPE_BOOL, ct);
        }
        check_stmt(c, n->if_stmt.then);
        if (n->if_stmt.otherwise) check_stmt(c, n->if_stmt.otherwise);
        break;
    case NODE_WHILE:
        check_expr(c, n->while_stmt.cond);
        if (n->while_stmt.cond) {
            ValType ct = infer_node_type(n->while_stmt.cond);
            if (ct == TYPE_UNKNOWN && n->while_stmt.cond->type == NODE_IDENT) {
                int idx = find_var(c, n->while_stmt.cond->ident);
                if (idx >= 0) ct = c->vars[idx].type;
            }
            if (ct != TYPE_BOOL && ct != TYPE_UNKNOWN)
                check_type_mismatch(c, n->while_stmt.cond, TYPE_BOOL, ct);
        }
        check_stmt(c, n->while_stmt.body);
        break;
    case NODE_RETURN:
        if (n->ret.val) {
            check_expr(c, n->ret.val);
            if (c->ret_type != TYPE_UNKNOWN) {
                ValType rt = infer_node_type(n->ret.val);
                if (rt == TYPE_UNKNOWN && n->ret.val->type == NODE_IDENT) {
                    int idx = find_var(c, n->ret.val->ident);
                    if (idx >= 0) rt = c->vars[idx].type;
                }
                check_type_mismatch(c, n->ret.val, c->ret_type, rt);
            }
        } else if (c->ret_type != TYPE_VOID && c->ret_type != TYPE_UNKNOWN) {
            char buf[256];
            snprintf(buf, sizeof(buf), "expected return value of type `%s`", valtype_str(c->ret_type));
            diag_add(c->diag, 1004, SEV_ERROR, n->src_line, n->src_col, 0, buf);
        }
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
            ctx.ret_type = TYPE_UNKNOWN;
            if (item->fn.ret_type && item->fn.ret_type->type == NODE_IDENT) {
                ctx.ret_type = get_type_for_name(item->fn.ret_type->ident);
            }
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
