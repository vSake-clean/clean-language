#include "mir.h"
#include "../check.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int current_block;   /* current block being built */
static MirSym sym;          /* variable → vreg map */
static int loop_cond_block; /* for break/continue: loop condition block */
static int loop_after_block; /* for break/continue: after loop block */

void mir_sym_init(MirSym *s) { s->count = 0; }

int mir_sym_add(MirSym *s, const char *name, int vreg, int type) {
    if (s->count >= MIR_MAX_VREGS) return -1;
    strncpy(s->vars[s->count].name, name, 63);
    s->vars[s->count].name[63] = 0;
    s->vars[s->count].vreg = vreg;
    s->vars[s->count].type = type;
    s->vars[s->count].is_float = (type == TYPE_FLOAT || type == TYPE_F32);
    s->vars[s->count].is_heap = 0;
    return s->count++;
}

int mir_sym_find(MirSym *s, const char *name) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->vars[i].name, name) == 0) return i;
    return -1;
}

int mir_new_vreg(MirFn *fn) {
    if (fn->num_vregs >= MIR_MAX_VREGS) return -1;
    return fn->num_vregs++;
}

int mir_new_block(MirFn *fn) {
    if (fn->block_count >= MIR_MAX_BLOCKS) return -1;
    int id = fn->block_count++;
    fn->blocks[id].id = id;
    fn->blocks[id].inst_start = fn->inst_count;
    fn->blocks[id].inst_count = 0;
    fn->blocks[id].term_type = -1;
    return id;
}

void mir_add_inst(MirFn *fn, int block_id, MirInst *inst) {
    if (fn->inst_count >= MIR_MAX_INSTS) return;
    int idx = fn->inst_count++;
    fn->insts[idx] = *inst;
    fn->blocks[block_id].inst_count++;
}

void mir_set_term(MirFn *fn, int block_id, int term_type, int vreg, int target, int ttrue, int tfalse) {
    fn->blocks[block_id].term_type = term_type;
    fn->blocks[block_id].term_vreg = vreg;
    fn->blocks[block_id].term_target = target;
    fn->blocks[block_id].term_true = ttrue;
    fn->blocks[block_id].term_false = tfalse;
}

static MirInst mir_op(int opcode, int dst, int src1, int src2, long long imm, int op) {
    MirInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.opcode = opcode; inst.dst = dst; inst.src1 = src1; inst.src2 = src2;
    inst.imm = imm; inst.op = op;
    return inst;
}

static int emit_const(MirFn *fn, long long val) {
    int v = mir_new_vreg(fn);
    MirInst inst = mir_op(MIR_CONST, v, -1, -1, val, 0);
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_float(MirFn *fn, double val) {
    int v = mir_new_vreg(fn);
    long long bits;
    memcpy(&bits, &val, 8);
    MirInst inst = mir_op(MIR_CONST, v, -1, -1, bits, 0);
    inst.float_result = 1;
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_copy(MirFn *fn, int src) {
    int v = mir_new_vreg(fn);
    MirInst inst = mir_op(MIR_COPY, v, src, -1, 0, 0);
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_unary(MirFn *fn, int op, int src) {
    int v = mir_new_vreg(fn);
    MirInst inst = mir_op(MIR_UNARY, v, src, -1, 0, op);
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_binary(MirFn *fn, int op, int s1, int s2) {
    int v = mir_new_vreg(fn);
    MirInst inst = mir_op(MIR_BINARY, v, s1, s2, 0, op);
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_load(MirFn *fn, int ptr) {
    int v = mir_new_vreg(fn);
    MirInst inst = mir_op(MIR_LOAD, v, ptr, -1, 0, 0);
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static void emit_store(MirFn *fn, int ptr, int val) {
    MirInst inst = mir_op(MIR_STORE, -1, ptr, val, 0, 0);
    mir_add_inst(fn, current_block, &inst);
}

static int emit_lea(MirFn *fn, int vreg) {
    int v = mir_new_vreg(fn);
    MirInst inst = mir_op(MIR_LEA, v, vreg, -1, 0, 0);
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_call(MirFn *fn, const char *callee, int *args, int nargs) {
    int v = mir_new_vreg(fn);
    MirInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.opcode = MIR_CALL;
    inst.dst = v;
    strncpy(inst.callee, callee, 63);
    inst.callee[63] = 0;
    inst.num_args = nargs > 8 ? 8 : nargs;
    for (int i = 0; i < inst.num_args; i++) inst.args[i] = args[i];
    if (strcmp(callee, "print_float") == 0 || strcmp(callee, "read_int") == 0) inst.float_result = 1;
    mir_add_inst(fn, current_block, &inst);
    return v;
}

static int emit_unused_call(MirFn *fn, const char *callee, int *args, int nargs) {
    MirInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.opcode = MIR_CALL;
    inst.dst = -1;
    strncpy(inst.callee, callee, 63);
    inst.callee[63] = 0;
    inst.num_args = nargs > 8 ? 8 : nargs;
    for (int i = 0; i < inst.num_args; i++) inst.args[i] = args[i];
    mir_add_inst(fn, current_block, &inst);
    return -1;
}

/* forward declarations */
static int mir_expr(MirFn *fn, Node *n);
static void mir_stmt(MirFn *fn, Node *n);

/* check if a function only uses constructs the MIR pipeline can handle */
static int mir_can_handle_node(Node *n) {
    if (!n) return 1;
    switch (n->type) {
    case NODE_STRUCT_LITERAL:
    case NODE_ENUM_LITERAL:
    case NODE_COMPREHENSION:
    case NODE_MATCH:
    case NODE_NULLSAFE:
        return 0;
    case NODE_INDEX:
        /* NODE_IDENT as index means struct field access */
        if (n->index_expr.index && n->index_expr.index->type == NODE_IDENT)
            return 0;
        return mir_can_handle_node(n->index_expr.obj) &&
               mir_can_handle_node(n->index_expr.index);
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next)
            if (!mir_can_handle_node(s)) return 0;
        return 1;
    case NODE_LET:
        return n->let.init ? mir_can_handle_node(n->let.init) : 1;
    case NODE_ASSIGN:
        return mir_can_handle_node(n->assign.lhs) &&
               mir_can_handle_node(n->assign.rhs);
    case NODE_IF:
        return mir_can_handle_node(n->if_stmt.cond) &&
               mir_can_handle_node(n->if_stmt.then) &&
               (n->if_stmt.otherwise ? mir_can_handle_node(n->if_stmt.otherwise) : 1);
    case NODE_WHILE:
        return mir_can_handle_node(n->while_stmt.cond) &&
               mir_can_handle_node(n->while_stmt.body);
    case NODE_RETURN:
        return n->ret.val ? mir_can_handle_node(n->ret.val) : 1;
    case NODE_EXPR_STMT:
        return n->expr_stmt.expr ? mir_can_handle_node(n->expr_stmt.expr) : 1;
    case NODE_CALL:
        for (Node *a = n->call.args; a; a = a->next)
            if (!mir_can_handle_node(a)) return 0;
        return 1;
    case NODE_BINARY:
        return mir_can_handle_node(n->binary.left) &&
               mir_can_handle_node(n->binary.right);
    case NODE_UNARY:
        return mir_can_handle_node(n->unary.operand);
    case NODE_BORROW:
    case NODE_MUT_BORROW:
    case NODE_DEREF:
        return mir_can_handle_node(n->borrow.operand);
    case NODE_STR:
        return 0; /* strings not yet handled correctly in MIR */
    default:
        return 1;
    }
}

static int mir_can_handle(Node *fn_decl) {
    if (!fn_decl || fn_decl->type != NODE_FN_DECL) return 0;
    return mir_can_handle_node(fn_decl->fn.body);
}

static int get_var_vreg(MirFn *fn, const char *name) {
    int idx = mir_sym_find(&sym, name);
    if (idx >= 0) return sym.vars[idx].vreg;
    return -1;
}

static void set_var_heap(MirFn *fn, const char *name) {
    int idx = mir_sym_find(&sym, name);
    if (idx >= 0) sym.vars[idx].is_heap = 1;
}

static int mir_expr(MirFn *fn, Node *n) {
    if (!n) return -1;

    switch (n->type) {
    case NODE_INT: {
        if (n->char_flag) return emit_const(fn, n->int_val);
        return emit_const(fn, n->int_val);
    }
    case NODE_FLOAT:
        return emit_float(fn, n->float_val);
    case NODE_BOOL:
        return emit_const(fn, n->bool_val ? 1 : 0);
    case NODE_STR:
        /* strings: return the string index as immediate */
        return emit_const(fn, (long long)(size_t)n->str_val);
    case NODE_IDENT: {
        int v = get_var_vreg(fn, n->ident);
        if (v >= 0) return v;
        return emit_const(fn, 0);
    }
    case NODE_UNARY: {
        if (n->unary.op == 1) { /* neg */
            int s = mir_expr(fn, n->unary.operand);
            return emit_unary(fn, UNARY_NEG, s);
        }
        if (n->unary.op == 0) { /* not */
            int s = mir_expr(fn, n->unary.operand);
            return emit_unary(fn, UNARY_NOT, s);
        }
        if (n->unary.op == 2) { /* bitnot */
            int s = mir_expr(fn, n->unary.operand);
            return emit_unary(fn, UNARY_BITNOT, s);
        }
        if (n->unary.op == 3) { /* move */
            int s = mir_expr(fn, n->unary.operand);
            return emit_copy(fn, s);
        }
        if (n->unary.op == 4) { /* as — type cast, no-op */
            return mir_expr(fn, n->unary.operand);
        }
        return mir_expr(fn, n->unary.operand);
    }
    case NODE_BINARY: {
        int l = mir_expr(fn, n->binary.left);
        int r = mir_expr(fn, n->binary.right);
        int op = n->binary.op;
        if (op == 13 || op == 14 || op == 15) op = n->binary.op; /* | ^ & */
        if (op >= 11 && op <= 12) { /* and/or — short-circuit via branch */
            /* convert to branch-based evaluation */
            if (op == 11) { /* and */
                int bb_true = mir_new_block(fn);
                int bb_join = mir_new_block(fn);
                MirInst test = mir_op(MIR_BRANCH, -1, l, -1, 0, 0);
                test.op = BIN_NE; /* compare against 0 */
                mir_add_inst(fn, current_block, &test);
                mir_set_term(fn, current_block, MIR_BRANCH, l, -1, bb_true, bb_join);

                current_block = bb_true;
                int rv = mir_expr(fn, n->binary.right);
                /* convert rv to bool */
                int r_bool = emit_unary(fn, UNARY_NOT, rv);
                r_bool = emit_unary(fn, UNARY_NOT, r_bool); /* !!rv → bool */
                mir_set_term(fn, current_block, MIR_JUMP, -1, bb_join, -1, -1);

                current_block = bb_join;
                /* phi is hard; just use a single vreg */
                return l; /* simplified: and returns left if false, right if true */
            } else { /* or */
                int bb_false = mir_new_block(fn);
                int bb_join = mir_new_block(fn);
                MirInst test = mir_op(MIR_BRANCH, -1, l, -1, 0, 0);
                test.op = BIN_NE;
                mir_add_inst(fn, current_block, &test);
                mir_set_term(fn, current_block, MIR_BRANCH, l, -1, bb_join, bb_false);

                current_block = bb_false;
                int rv = mir_expr(fn, n->binary.right);
                mir_set_term(fn, current_block, MIR_JUMP, -1, bb_join, -1, -1);

                current_block = bb_join;
                return l; /* simplified */
            }
        }
        return emit_binary(fn, op, l, r);
    }
    case NODE_CALL: {
        Node *callee = n->call.callee;
        int args[8];
        int nargs = 0;
        for (Node *a = n->call.args; a && nargs < 8; a = a->next)
            args[nargs++] = mir_expr(fn, a);

        if (callee->type == NODE_IDENT) {
            return emit_call(fn, callee->ident, args, nargs);
        }
        return emit_const(fn, 0);
    }
    case NODE_DEREF: {
        int ptr = mir_expr(fn, n->borrow.operand);
        return emit_load(fn, ptr);
    }
    case NODE_BORROW:
    case NODE_MUT_BORROW: {
        if (n->borrow.operand->type == NODE_IDENT) {
            int v = get_var_vreg(fn, n->borrow.operand->ident);
            if (v >= 0) return emit_lea(fn, v);
            return emit_const(fn, 0);
        }
        return mir_expr(fn, n->borrow.operand);
    }
    case NODE_INDEX:
    case NODE_NULLSAFE: {
        int obj = mir_expr(fn, n->index_expr.obj);
        int idx = mir_expr(fn, n->index_expr.index);
        /* field access: load from offset */
        int ptr = emit_binary(fn, BIN_ADD, obj, idx);
        return emit_load(fn, ptr);
    }
    case NODE_STRUCT_LITERAL: {
        /* allocate struct, store fields */
        int args_v[8];
        int nargs = 0;
        for (Node *a = n->struct_literal.args; a && nargs < 8; a = a->next)
            args_v[nargs++] = mir_expr(fn, a);
        /* use malloc call */
        int size = emit_const(fn, 16); /* default struct size */
        int ptr = emit_call(fn, "malloc", &size, 1);
        /* store each field */
        for (int i = 0; i < nargs; i++) {
            int off = emit_const(fn, i * 8);
            int addr = emit_binary(fn, BIN_ADD, ptr, off);
            emit_store(fn, addr, args_v[i]);
        }
        return ptr;
    }
    case NODE_ENUM_LITERAL: {
        int payload = n->enum_literal.payload ? mir_expr(fn, n->enum_literal.payload) : emit_const(fn, 0);
        int size = emit_const(fn, 16);
        int ptr = emit_call(fn, "malloc", &size, 1);
        int tag = emit_const(fn, 0); /* simplified tag */
        emit_store(fn, ptr, tag);
        int off = emit_const(fn, 8);
        int addr = emit_binary(fn, BIN_ADD, ptr, off);
        emit_store(fn, addr, payload);
        return ptr;
    }
    case NODE_COMPREHENSION: {
        int start = mir_expr(fn, n->comp.iter);
        int end = n->comp.iter_end ? mir_expr(fn, n->comp.iter_end) : emit_const(fn, 0);
        int count = emit_const(fn, 0);
        /* simplified: just loop */
        return count;
    }
    default:
        return emit_const(fn, 0);
    }
}

static void mir_stmt(MirFn *fn, Node *n) {
    if (!n) return;

    switch (n->type) {
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next)
            mir_stmt(fn, s);
        break;
    case NODE_LET: {
        int vreg = mir_new_vreg(fn);
        int type = TYPE_I64;
        if (n->let.type && n->let.type->type == NODE_IDENT)
            type = get_type_for_name(n->let.type->ident);
        mir_sym_add(&sym, n->let.name, vreg, type);
        if (n->let.init) {
            int val = mir_expr(fn, n->let.init);
            MirInst inst = mir_op(MIR_COPY, vreg, val, -1, 0, 0);
            mir_add_inst(fn, current_block, &inst);
            if (n->let.init->type == NODE_STRUCT_LITERAL || n->let.init->type == NODE_ENUM_LITERAL)
                set_var_heap(fn, n->let.name);
        }
        break;
    }
    case NODE_ASSIGN: {
        if (n->assign.lhs->type == NODE_IDENT) {
            int idx = mir_sym_find(&sym, n->assign.lhs->ident);
            if (idx >= 0) {
                int val = mir_expr(fn, n->assign.rhs);
                MirInst inst = mir_op(MIR_COPY, sym.vars[idx].vreg, val, -1, 0, 0);
                mir_add_inst(fn, current_block, &inst);
            }
        } else if (n->assign.lhs->type == NODE_DEREF) {
            int ptr = mir_expr(fn, n->assign.lhs->borrow.operand);
            int val = mir_expr(fn, n->assign.rhs);
            emit_store(fn, ptr, val);
        } else {
            mir_expr(fn, n->assign.lhs);
            mir_expr(fn, n->assign.rhs);
        }
        break;
    }
    case NODE_IF: {
        int cond = mir_expr(fn, n->if_stmt.cond);
        int bb_then = mir_new_block(fn);
        int bb_else = mir_new_block(fn);
        int bb_join = mir_new_block(fn);

        MirInst branch;
        memset(&branch, 0, sizeof(branch));
        branch.opcode = MIR_BRANCH;
        branch.src1 = cond;
        /* branch if cond != 0 -> then, else -> else */
        /* we emit a comparison against 0 */
        mir_add_inst(fn, current_block, &branch);
        int cond_zero = mir_new_vreg(fn);
        (void)cond_zero;
        mir_set_term(fn, current_block, MIR_BRANCH, cond, -1, bb_then, bb_else);

        current_block = bb_then;
        mir_stmt(fn, n->if_stmt.then);
        mir_set_term(fn, current_block, MIR_JUMP, -1, bb_join, -1, -1);

        current_block = bb_else;
        if (n->if_stmt.otherwise)
            mir_stmt(fn, n->if_stmt.otherwise);
        mir_set_term(fn, current_block, MIR_JUMP, -1, bb_join, -1, -1);

        current_block = bb_join;
        break;
    }
    case NODE_WHILE: {
        int bb_cond = mir_new_block(fn);
        int bb_body = mir_new_block(fn);
        int bb_after = mir_new_block(fn);

        int save_cond = loop_cond_block;
        int save_after = loop_after_block;
        loop_cond_block = bb_cond;
        loop_after_block = bb_after;

        /* jump to condition */
        mir_set_term(fn, current_block, MIR_JUMP, -1, bb_cond, -1, -1);

        current_block = bb_cond;
        int cond = mir_expr(fn, n->while_stmt.cond);
        mir_set_term(fn, current_block, MIR_BRANCH, cond, -1, bb_body, bb_after);

        current_block = bb_body;
        mir_stmt(fn, n->while_stmt.body);
        mir_set_term(fn, current_block, MIR_JUMP, -1, bb_cond, -1, -1);

        current_block = bb_after;
        loop_cond_block = save_cond;
        loop_after_block = save_after;
        break;
    }
    case NODE_RETURN: {
        if (n->ret.val) {
            int v = mir_expr(fn, n->ret.val);
            mir_set_term(fn, current_block, MIR_RET, v, -1, -1, -1);
        } else {
            mir_set_term(fn, current_block, MIR_RET, -1, -1, -1, -1);
        }
        /* create a new dead block after return */
        int dead = mir_new_block(fn);
        current_block = dead;
        break;
    }
    case NODE_EXPR_STMT: {
        int v = mir_expr(fn, n->expr_stmt.expr);
        (void)v;
        break;
    }
    case NODE_BREAK:
        if (loop_after_block >= 0)
            mir_set_term(fn, current_block, MIR_JUMP, -1, loop_after_block, -1, -1);
        {
            int dead = mir_new_block(fn);
            current_block = dead;
        }
        break;
    case NODE_CONTINUE:
        if (loop_cond_block >= 0)
            mir_set_term(fn, current_block, MIR_JUMP, -1, loop_cond_block, -1, -1);
        {
            int dead = mir_new_block(fn);
            current_block = dead;
        }
        break;
    case NODE_MATCH: {
        int match_val = mir_expr(fn, n->match.expr);
        int bb_join = mir_new_block(fn);
        int prev_false = -1;
        Node *arm;
        for (arm = n->match.arms; arm; arm = arm->next) {
            int bb_arm = mir_new_block(fn);
            if (prev_false >= 0)
                mir_set_term(fn, prev_false, MIR_JUMP, -1, bb_arm, -1, -1);
            current_block = bb_arm;
            /* skip payload/guard for now */
            if (arm->match_arm.payload) mir_expr(fn, arm->match_arm.payload);
            if (arm->match_arm.guard) mir_expr(fn, arm->match_arm.guard);
            mir_stmt(fn, arm->match_arm.body);
            mir_set_term(fn, current_block, MIR_JUMP, -1, bb_join, -1, -1);
            prev_false = current_block;
        }
        current_block = bb_join;
        break;
    }
    default:
        break;
    }
}

MirFn *mir_build_fn(Node *fn_decl, Diag *diag, const char *source) {
    (void)diag;
    (void)source;
    if (!fn_decl || fn_decl->type != NODE_FN_DECL) return NULL;

    /* check if this function uses constructs we can handle */
    if (!mir_can_handle(fn_decl)) {
        return NULL;
    }

    MirFn *fn = calloc(1, sizeof(MirFn));
    if (!fn) return NULL;

    strncpy(fn->name, fn_decl->fn.name ? fn_decl->fn.name : "anonymous", 63);
    fn->name[63] = 0;
    fn->ast_fn = fn_decl;

    mir_sym_init(&sym);
    loop_cond_block = -1;
    loop_after_block = -1;

    /* create entry block */
    int entry = mir_new_block(fn);
    current_block = entry;

    /* add parameters as vregs */
    fn->num_params = 0;
    for (Node *p = fn_decl->fn.params; p; p = p->next) {
        if (p->type == NODE_LET && p->let.name) {
            int vreg = mir_new_vreg(fn);
            int type = TYPE_I64;
            if (p->let.type && p->let.type->type == NODE_IDENT)
                type = get_type_for_name(p->let.type->ident);
            mir_sym_add(&sym, p->let.name, vreg, type);
            fn->num_params++;
        }
    }

    /* build body */
    mir_stmt(fn, fn_decl->fn.body);

    /* if current block has no terminator, add ret 0 */
    if (fn->blocks[current_block].term_type < 0)
        mir_set_term(fn, current_block, MIR_RET, 0, -1, -1, -1);

    /* update inst_start for all blocks (in case insts were added after block creation) */
    int pos = 0;
    for (int i = 0; i < fn->block_count; i++) {
        fn->blocks[i].inst_start = pos;
        pos += fn->blocks[i].inst_count;
    }

    return fn;
}

void mir_print(MirFn *fn) {
    printf("MIR function: %s (%d blocks, %d insts, %d vregs)\n",
           fn->name, fn->block_count, fn->inst_count, fn->num_vregs);
    int pos = 0;
    for (int i = 0; i < fn->block_count; i++) {
        MirBlock *b = &fn->blocks[i];
        printf("  BB%d (%d insts, term=%d):\n", i, b->inst_count, b->term_type);
        for (int j = 0; j < b->inst_count; j++) {
            MirInst *inst = &fn->insts[pos++];
            printf("    v%d", inst->dst);
            switch (inst->opcode) {
            case MIR_CONST: printf(" = CONST %lld", inst->imm); break;
            case MIR_COPY:  printf(" = COPY v%d", inst->src1); break;
            case MIR_LOAD:  printf(" = LOAD *v%d", inst->src1); break;
            case MIR_STORE: printf(" STORE *v%d = v%d", inst->src1, inst->src2); break;
            case MIR_LEA:   printf(" = LEA &v%d", inst->src1); break;
            case MIR_UNARY: printf(" = UNARY(op=%d) v%d", inst->op, inst->src1); break;
            case MIR_BINARY: printf(" = BINARY(op=%d) v%d, v%d", inst->op, inst->src1, inst->src2); break;
            case MIR_CALL:  printf(" = CALL %s(", inst->callee);
                for (int k = 0; k < inst->num_args; k++)
                    printf("%sv%d", k > 0 ? "," : "", inst->args[k]);
                printf(")"); break;
            case MIR_BRANCH: printf(" BRANCH v%d ? BB%d : BB%d", inst->src1, b->term_true, b->term_false); break;
            default: printf(" opcode=%d", inst->opcode); break;
            }
            printf("\n");
        }
        if (b->term_type == MIR_JUMP) printf("    -> BB%d\n", b->term_target);
        else if (b->term_type == MIR_BRANCH) printf("    IF v%d -> BB%d else BB%d\n", b->term_vreg, b->term_true, b->term_false);
        else if (b->term_type == MIR_RET) printf("    RET v%d\n", b->term_vreg);
    }
}
