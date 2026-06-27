#include "borrowck.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_LOANS 256

typedef enum { LOAN_SHARED, LOAN_MUT } LoanKind;

typedef struct {
    char var_name[64];
    LoanKind kind;
    size_t scope_depth;
    size_t src_line;
    size_t src_col;
} Loan;

typedef struct {
    Loan loans[MAX_LOANS];
    int loan_count;
    Diag *diag;
    const char *source;
    size_t scope_depth;
} BorrowckCtx;

static void bc_init(BorrowckCtx *bc, Diag *d, const char *src) {
    bc->loan_count = 0;
    bc->diag = d;
    bc->source = src;
    bc->scope_depth = 0;
}

static int has_any_loan(BorrowckCtx *bc, const char *var) {
    for (int i = 0; i < bc->loan_count; i++)
        if (strcmp(bc->loans[i].var_name, var) == 0) return 1;
    return 0;
}

static int has_mut_loan(BorrowckCtx *bc, const char *var) {
    for (int i = 0; i < bc->loan_count; i++)
        if (strcmp(bc->loans[i].var_name, var) == 0 && bc->loans[i].kind == LOAN_MUT) return 1;
    return 0;
}

static int add_loan(BorrowckCtx *bc, const char *var, LoanKind kind, size_t line, size_t col) {
    if (bc->loan_count >= MAX_LOANS) return -1;
    Loan *l = &bc->loans[bc->loan_count++];
    strncpy(l->var_name, var, 63);
    l->var_name[63] = 0;
    l->kind = kind;
    l->scope_depth = bc->scope_depth;
    l->src_line = line;
    l->src_col = col;
    return 0;
}

static void remove_loans_at_or_below(BorrowckCtx *bc, size_t depth) {
    int j = 0;
    for (int i = 0; i < bc->loan_count; i++) {
        if (bc->loans[i].scope_depth >= depth) continue;
        if (i != j) bc->loans[j] = bc->loans[i];
        j++;
    }
    bc->loan_count = j;
}

static void bc_enter_scope(BorrowckCtx *bc) {
    bc->scope_depth++;
}

static void bc_leave_scope(BorrowckCtx *bc) {
    remove_loans_at_or_below(bc, bc->scope_depth);
    bc->scope_depth--;
}

/* forward declarations */
static void bc_expr_full(BorrowckCtx *bc, Node *n);
static void bc_stmt(BorrowckCtx *bc, Node *n);

static void bc_borrow_start(BorrowckCtx *bc, Node *n, int mut) {
    if (!n || !n->borrow.operand) return;
    Node *operand = n->borrow.operand;

    /* resolve through deref chains to find the root variable */
    while (operand && operand->type == NODE_DEREF)
        operand = operand->borrow.operand;
    if (!operand || operand->type != NODE_IDENT) return;

    const char *var = operand->ident;

    if (mut) {
        if (has_any_loan(bc, var)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "cannot borrow `%s` as mutable because it is already borrowed", var);
            diag_add(bc->diag, 2001, SEV_ERROR, n->src_line, n->src_col, 1, buf);
            return;
        }
    } else {
        if (has_mut_loan(bc, var)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "cannot borrow `%s` as immutable because it is mutably borrowed", var);
            diag_add(bc->diag, 2001, SEV_ERROR, n->src_line, n->src_col, 1, buf);
            return;
        }
    }
    add_loan(bc, var, mut ? LOAN_MUT : LOAN_SHARED, n->src_line, n->src_col);
}

static void bc_store(BorrowckCtx *bc, Node *target) {
    if (!target) return;
    Node *root = target;
    while (root->type == NODE_DEREF) root = root->borrow.operand;
    if (root->type != NODE_IDENT) return;

    if (has_any_loan(bc, root->ident)) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "cannot assign to `%s` because it is borrowed", root->ident);
        diag_add(bc->diag, 2003, SEV_ERROR, target->src_line, target->src_col, 1, buf);
    }
}

static void bc_move_check(BorrowckCtx *bc, Node *n) {
    if (!n || n->type != NODE_UNARY || n->unary.op != 3) return;
    Node *operand = n->unary.operand;
    if (!operand || operand->type != NODE_IDENT) return;

    const char *var = operand->ident;
    if (has_any_loan(bc, var)) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "cannot move out of `%s` because it is borrowed", var);
        diag_add(bc->diag, 2002, SEV_ERROR, n->src_line, n->src_col, 1, buf);
    }
}

static void bc_expr_full(BorrowckCtx *bc, Node *n) {
    if (!n) return;

    switch (n->type) {
    case NODE_IDENT:
        if (has_mut_loan(bc, n->ident)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "cannot use `%s` because it is mutably borrowed", n->ident);
            diag_add(bc->diag, 2004, SEV_ERROR, n->src_line, n->src_col, 1, buf);
        }
        break;
    case NODE_INT:
    case NODE_FLOAT:
    case NODE_BOOL:
    case NODE_STR:
        break;
    case NODE_BINARY:
        bc_expr_full(bc, n->binary.left);
        bc_expr_full(bc, n->binary.right);
        break;
    case NODE_UNARY:
        bc_move_check(bc, n);
        bc_expr_full(bc, n->unary.operand);
        break;
    case NODE_CALL:
        bc_expr_full(bc, n->call.callee);
        for (Node *a = n->call.args; a; a = a->next)
            bc_expr_full(bc, a);
        break;
    case NODE_INDEX:
    case NODE_NULLSAFE:
        bc_expr_full(bc, n->index_expr.obj);
        bc_expr_full(bc, n->index_expr.index);
        break;
    case NODE_BORROW:
        bc_borrow_start(bc, n, 0);
        break;
    case NODE_MUT_BORROW:
        bc_borrow_start(bc, n, 1);
        break;
    case NODE_DEREF:
        bc_expr_full(bc, n->borrow.operand);
        break;
    case NODE_STRUCT_LITERAL:
        for (Node *a = n->struct_literal.args; a; a = a->next)
            bc_expr_full(bc, a);
        break;
    case NODE_ENUM_LITERAL:
        if (n->enum_literal.payload)
            bc_expr_full(bc, n->enum_literal.payload);
        break;
    case NODE_COMPREHENSION:
        bc_expr_full(bc, n->comp.map);
        bc_expr_full(bc, n->comp.iter);
        if (n->comp.iter_end) bc_expr_full(bc, n->comp.iter_end);
        if (n->comp.filter) bc_expr_full(bc, n->comp.filter);
        break;
    default:
        break;
    }
}

static void bc_stmt(BorrowckCtx *bc, Node *n) {
    if (!n) return;

    switch (n->type) {
    case NODE_BLOCK:
        bc_enter_scope(bc);
        for (Node *s = n->block.stmts; s; s = s->next)
            bc_stmt(bc, s);
        bc_leave_scope(bc);
        break;
    case NODE_LET:
        if (n->let.init) {
            bc_move_check(bc, n->let.init);
            bc_expr_full(bc, n->let.init);
        }
        break;
    case NODE_ASSIGN:
        bc_store(bc, n->assign.lhs);
        bc_expr_full(bc, n->assign.lhs);
        bc_expr_full(bc, n->assign.rhs);
        break;
    case NODE_IF:
        bc_expr_full(bc, n->if_stmt.cond);
        bc_enter_scope(bc);
        bc_stmt(bc, n->if_stmt.then);
        bc_leave_scope(bc);
        if (n->if_stmt.otherwise) {
            bc_enter_scope(bc);
            bc_stmt(bc, n->if_stmt.otherwise);
            bc_leave_scope(bc);
        }
        break;
    case NODE_WHILE:
        bc_expr_full(bc, n->while_stmt.cond);
        bc_enter_scope(bc);
        bc_stmt(bc, n->while_stmt.body);
        bc_leave_scope(bc);
        break;
    case NODE_RETURN:
        if (n->ret.val) bc_expr_full(bc, n->ret.val);
        break;
    case NODE_EXPR_STMT:
        bc_expr_full(bc, n->expr_stmt.expr);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        break;
    case NODE_MATCH:
        bc_expr_full(bc, n->match.expr);
        for (Node *arm = n->match.arms; arm; arm = arm->next) {
            bc_enter_scope(bc);
            if (arm->match_arm.payload) bc_expr_full(bc, arm->match_arm.payload);
            if (arm->match_arm.guard) bc_expr_full(bc, arm->match_arm.guard);
            bc_stmt(bc, arm->match_arm.body);
            bc_leave_scope(bc);
        }
        break;
    default:
        break;
    }
}

void borrowck_program(Node *prog, Diag *diag, const char *source) {
    BorrowckCtx bc;
    bc_init(&bc, diag, source);

    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_FN_DECL) {
            bc.scope_depth = 0;
            bc.loan_count = 0;
            bc_stmt(&bc, item->fn.body);
        }
    }
}
