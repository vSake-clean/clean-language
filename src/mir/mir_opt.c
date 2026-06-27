#include "mir.h"
#include "../ast.h"
#include "../check.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== AST-level constant folding ==================== */

static int foldable_unary(Node *n) {
    if (!n || n->type != NODE_UNARY || !n->unary.operand) return 0;
    return (n->unary.op == 1) && (n->unary.operand->type == NODE_INT);
}

static int foldable_binary(Node *n) {
    if (!n || n->type != NODE_BINARY || !n->binary.left || !n->binary.right) return 0;
    if (n->binary.left->type != NODE_INT || n->binary.right->type != NODE_INT) return 0;
    int op = n->binary.op;
    /* foldable ops: arithmetic, comparison, bitwise. Not div/mod by zero. */
    if (op == 3 || op == 4) {
        if (n->binary.right->int_val == 0) return 0;
    }
    return 1;
}

static Node *ast_fold(Node *n) {
    if (!n) return NULL;

    switch (n->type) {
    case NODE_BINARY: {
        n->binary.left = ast_fold(n->binary.left);
        n->binary.right = ast_fold(n->binary.right);
        if (foldable_binary(n)) {
            long long l = n->binary.left->int_val;
            long long r = n->binary.right->int_val;
            long long result = 0;
            switch (n->binary.op) {
            case 0: result = l + r; break;
            case 1: result = l - r; break;
            case 2: result = l * r; break;
            case 3: result = l / r; break;
            case 4: result = l % r; break;
            case 5: result = (l == r) ? 1 : 0; break;
            case 6: result = (l != r) ? 1 : 0; break;
            case 7: result = (l < r) ? 1 : 0; break;
            case 8: result = (l <= r) ? 1 : 0; break;
            case 9: result = (l > r) ? 1 : 0; break;
            case 10: result = (l >= r) ? 1 : 0; break;
            case 11: result = (l && r) ? 1 : 0; break;
            case 12: result = (l || r) ? 1 : 0; break;
            case 13: result = l | r; break;
            case 14: result = l ^ r; break;
            case 15: result = l & r; break;
            case 16: result = l << r; break;
            case 17: result = l >> r; break;
            default: return n;
            }
            n->type = NODE_INT;
            n->int_val = result;
            n->char_flag = 0;
        }
        return n;
    }
    case NODE_UNARY: {
        n->unary.operand = ast_fold(n->unary.operand);
        if (foldable_unary(n)) {
            n->type = NODE_INT;
            n->int_val = -n->unary.operand->int_val;
            n->char_flag = 0;
        }
        return n;
    }
    case NODE_IF:
        n->if_stmt.cond = ast_fold(n->if_stmt.cond);
        n->if_stmt.then = ast_fold(n->if_stmt.then);
        if (n->if_stmt.otherwise) n->if_stmt.otherwise = ast_fold(n->if_stmt.otherwise);
        /* fold constant condition: if (1) then-branch, if (0) else-branch */
        if (n->if_stmt.cond && n->if_stmt.cond->type == NODE_INT) {
            if (n->if_stmt.cond->int_val != 0 && n->if_stmt.then) {
                Node *result = n->if_stmt.then;
                n->if_stmt.then = NULL;
                return ast_fold(result);
            } else if (n->if_stmt.cond->int_val == 0 && n->if_stmt.otherwise) {
                Node *result = n->if_stmt.otherwise;
                n->if_stmt.otherwise = NULL;
                return ast_fold(result);
            }
        }
        return n;
    case NODE_WHILE:
        n->while_stmt.cond = ast_fold(n->while_stmt.cond);
        n->while_stmt.body = ast_fold(n->while_stmt.body);
        return n;
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next)
            s = ast_fold(s);
        return n;
    case NODE_LET:
        if (n->let.init) n->let.init = ast_fold(n->let.init);
        if (n->let.type) n->let.type = ast_fold(n->let.type);
        return n;
    case NODE_ASSIGN:
        n->assign.lhs = ast_fold(n->assign.lhs);
        n->assign.rhs = ast_fold(n->assign.rhs);
        return n;
    case NODE_RETURN:
        if (n->ret.val) n->ret.val = ast_fold(n->ret.val);
        return n;
    case NODE_EXPR_STMT:
        if (n->expr_stmt.expr) n->expr_stmt.expr = ast_fold(n->expr_stmt.expr);
        return n;
    case NODE_CALL:
        if (n->call.callee) n->call.callee = ast_fold(n->call.callee);
        for (Node *a = n->call.args; a; a = a->next)
            a = ast_fold(a);
        return n;
    case NODE_MATCH:
        if (n->match.expr) n->match.expr = ast_fold(n->match.expr);
        for (Node *arm = n->match.arms; arm; arm = arm->next) {
            if (arm->match_arm.payload) arm->match_arm.payload = ast_fold(arm->match_arm.payload);
            if (arm->match_arm.guard) arm->match_arm.guard = ast_fold(arm->match_arm.guard);
            if (arm->match_arm.body) arm->match_arm.body = ast_fold(arm->match_arm.body);
        }
        return n;
    case NODE_COMPREHENSION:
        if (n->comp.map) n->comp.map = ast_fold(n->comp.map);
        if (n->comp.iter) n->comp.iter = ast_fold(n->comp.iter);
        if (n->comp.iter_end) n->comp.iter_end = ast_fold(n->comp.iter_end);
        if (n->comp.filter) n->comp.filter = ast_fold(n->comp.filter);
        return n;
    default:
        return n;
    }
}

/* ==================== AST-level dead code elimination ==================== */

typedef struct {
    char name[64];
    int use_count;
} UseEntry;

static void count_uses(Node *n, const char *target, int *count) {
    if (!n) return;
    switch (n->type) {
    case NODE_IDENT:
        if (n->ident && strcmp(n->ident, target) == 0)
            (*count)++;
        break;
    case NODE_BINARY:
        count_uses(n->binary.left, target, count);
        count_uses(n->binary.right, target, count);
        break;
    case NODE_UNARY:
        count_uses(n->unary.operand, target, count);
        break;
    case NODE_CALL:
        count_uses(n->call.callee, target, count);
        for (Node *a = n->call.args; a; a = a->next)
            count_uses(a, target, count);
        break;
    case NODE_IF:
        count_uses(n->if_stmt.cond, target, count);
        count_uses(n->if_stmt.then, target, count);
        if (n->if_stmt.otherwise) count_uses(n->if_stmt.otherwise, target, count);
        break;
    case NODE_WHILE:
        count_uses(n->while_stmt.cond, target, count);
        count_uses(n->while_stmt.body, target, count);
        break;
    case NODE_RETURN:
        if (n->ret.val) count_uses(n->ret.val, target, count);
        break;
    case NODE_EXPR_STMT:
        if (n->expr_stmt.expr) count_uses(n->expr_stmt.expr, target, count);
        break;
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next)
            count_uses(s, target, count);
        break;
    case NODE_LET:
        if (n->let.init) count_uses(n->let.init, target, count);
        break;
    case NODE_ASSIGN:
        count_uses(n->assign.lhs, target, count);
        count_uses(n->assign.rhs, target, count);
        break;
    case NODE_MATCH:
        count_uses(n->match.expr, target, count);
        for (Node *arm = n->match.arms; arm; arm = arm->next) {
            if (arm->match_arm.guard) count_uses(arm->match_arm.guard, target, count);
            if (arm->match_arm.body) count_uses(arm->match_arm.body, target, count);
        }
        break;
    case NODE_INDEX:
    case NODE_NULLSAFE:
        count_uses(n->index_expr.obj, target, count);
        count_uses(n->index_expr.index, target, count);
        break;
    default:
        break;
    }
}

static int stmt_has_side_effects(Node *n) {
    if (!n) return 0;
    switch (n->type) {
    case NODE_EXPR_STMT:
        if (n->expr_stmt.expr && n->expr_stmt.expr->type == NODE_CALL) return 1;
        return 0;
    case NODE_CALL: return 1;
    case NODE_ASSIGN: return 1;
    case NODE_RETURN: return 1;
    case NODE_IF: return 1;
    case NODE_WHILE: return 1;
    case NODE_BREAK:
    case NODE_CONTINUE: return 1;
    default: return 0;
    }
}

static Node *ast_dce_block(Node *n, Node *next_sibling) {
    if (!n) return NULL;

    switch (n->type) {
    case NODE_BLOCK: {
        Node *prev = NULL;
        Node *s = n->block.stmts;
        while (s) {
            Node *next = s->next;
            /* check for dead let: variable never used after this point */
            if (s->type == NODE_LET && s->let.name && !s->let.init) {
                int uses = 0;
                count_uses(next, s->let.name, &uses);
                count_uses(next_sibling, s->let.name, &uses);
                if (uses == 0) {
                    /* dead let — remove it */
                    if (prev) prev->next = next;
                    else n->block.stmts = next;
                    s->next = NULL;
                    node_free(s);
                    s = next;
                    continue;
                }
            } else if (s->type == NODE_LET && s->let.name && s->let.init &&
                       s->let.init->type == NODE_IDENT) {
                /* let x = y where y is never used after and x is never used */
                int uses = 0;
                count_uses(next, s->let.name, &uses);
                count_uses(next_sibling, s->let.name, &uses);
                if (uses == 0) {
                    /* pure copy — remove if no side effects */
                    if (prev) prev->next = next;
                    else n->block.stmts = next;
                    s->next = NULL;
                    node_free(s);
                    s = next;
                    continue;
                }
            }
            s = ast_dce_block(s, next);
            prev = s;
            s = next;
        }
        return n;
    }
    case NODE_IF:
        n->if_stmt.cond = (Node*)ast_dce_block((Node*)n->if_stmt.cond, NULL);
        n->if_stmt.then = ast_dce_block(n->if_stmt.then, n->if_stmt.otherwise);
        if (n->if_stmt.otherwise) n->if_stmt.otherwise = ast_dce_block(n->if_stmt.otherwise, NULL);
        /* if then-block is empty and no else, simplify */
        if (n->if_stmt.then && n->if_stmt.then->type == NODE_BLOCK &&
            !n->if_stmt.then->block.stmts && !n->if_stmt.otherwise) {
            return NULL; /* dead if */
        }
        return n;
    case NODE_WHILE:
        n->while_stmt.body = ast_dce_block(n->while_stmt.body, NULL);
        return n;
    default:
        return n;
    }
}

/* ==================== Inlining ==================== */

static int is_small_fn(Node *fn) {
    if (!fn || fn->type != NODE_FN_DECL) return 0;
    /* count nodes in body (rough heuristic) */
    int count = 0;
    Node *body = fn->fn.body;
    if (body && body->type == NODE_BLOCK) {
        for (Node *s = body->block.stmts; s; s = s->next) count++;
    }
    return count < 10;
}

/* ==================== Public API ==================== */

void mir_opt(MirFn *fn) {
    if (!fn) return;
    /* MIR-level constant folding on instructions */
    for (int i = 0; i < fn->inst_count; i++) {
        MirInst *inst = &fn->insts[i];
        if (inst->opcode == MIR_BINARY) {
            /* check if both sources are CONST */
            int s1c = 0, s2c = 0;
            long long cv1 = 0, cv2 = 0;
            /* look backwards for CONST definitions of sources */
            for (int j = 0; j < i; j++) {
                if (fn->insts[j].dst == inst->src1 && fn->insts[j].opcode == MIR_CONST) {
                    s1c = 1; cv1 = fn->insts[j].imm;
                }
                if (fn->insts[j].dst == inst->src2 && fn->insts[j].opcode == MIR_CONST) {
                    s2c = 1; cv2 = fn->insts[j].imm;
                }
            }
            if (s1c && s2c) {
                long long result = 0;
                int foldable = 1;
                switch (inst->op) {
                case 0: result = cv1 + cv2; break;
                case 1: result = cv1 - cv2; break;
                case 2: result = cv1 * cv2; break;
                case 3: if (cv2) result = cv1 / cv2; else foldable = 0; break;
                case 5: result = (cv1 == cv2) ? 1 : 0; break;
                case 6: result = (cv1 != cv2) ? 1 : 0; break;
                case 7: result = (cv1 < cv2) ? 1 : 0; break;
                case 8: result = (cv1 <= cv2) ? 1 : 0; break;
                case 9: result = (cv1 > cv2) ? 1 : 0; break;
                case 10: result = (cv1 >= cv2) ? 1 : 0; break;
                default: foldable = 0; break;
                }
                if (foldable) {
                    inst->opcode = MIR_CONST;
                    inst->imm = result;
                    inst->src1 = inst->src2 = -1;
                    inst->op = 0;
                }
            }
        }
    }
}

/* AST-level optimizations: constant folding, then DCE */
void ast_optimize(Node *prog) {
    if (!prog) return;

    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_FN_DECL && item->fn.body) {
            /* constant fold */
            item->fn.body = ast_fold(item->fn.body);
            /* dead code elimination */
            item->fn.body = ast_dce_block(item->fn.body, NULL);
        } else if (item->type == NODE_IMPL_BLOCK) {
            for (Node *m = item->impl_block.methods; m; m = m->next) {
                if (m->type == NODE_FN_DECL && m->fn.body) {
                    m->fn.body = ast_fold(m->fn.body);
                    m->fn.body = ast_dce_block(m->fn.body, NULL);
                }
            }
        }
    }
}
