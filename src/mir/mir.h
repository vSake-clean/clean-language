#ifndef CLEAN_MIR_H
#define CLEAN_MIR_H

#include "../ast.h"
#include "../diag.h"
#include <stddef.h>

#define MIR_MAX_BLOCKS 256
#define MIR_MAX_INSTS 4096
#define MIR_MAX_VREGS 512

/* MIR opcodes */
enum {
    MIR_NOP,
    MIR_CONST,      /* vreg = imm */
    MIR_COPY,       /* vreg = vreg */
    MIR_LOAD,       /* vreg = *vreg */
    MIR_STORE,      /* *vreg = vreg */
    MIR_LEA,        /* vreg = &vreg (address of local) */
    MIR_UNARY,      /* vreg = OP vreg */
    MIR_BINARY,     /* vreg = vreg OP vreg */
    MIR_CALL,       /* vreg = call fn(args...) */
    MIR_RET,        /* return vreg */
    MIR_JUMP,       /* goto block */
    MIR_BRANCH,     /* if vreg goto block else block */
    MIR_PHI,        /* vreg = phi(vreg, vreg) */
};

/* Unary operator codes (matches codegen opcodes) */
enum { UNARY_NOT = 0, UNARY_NEG = 1, UNARY_BITNOT = 2, UNARY_MOVE = 3, UNARY_AS = 4 };

/* Binary operator codes (matches codegen opcodes) */
enum { BIN_ADD = 0, BIN_SUB = 1, BIN_MUL = 2, BIN_DIV = 3, BIN_MOD = 4,
       BIN_EQ = 5, BIN_NE = 6, BIN_LT = 7, BIN_LE = 8, BIN_GT = 9, BIN_GE = 10,
       BIN_AND = 11, BIN_OR = 12,
       BIN_BITOR = 13, BIN_XOR = 14, BIN_BITAND = 15,
       BIN_SHL = 16, BIN_SHR = 17, BIN_POW = 18 };

typedef struct {
    int opcode;         /* MIR_CONST, MIR_COPY, etc. */
    int dst;            /* destination virtual register */
    int src1, src2;     /* source virtual registers */
    long long imm;      /* immediate value */
    int op;             /* operator (for unary/binary ops) */
    /* for calls */
    char callee[64];
    int num_args;
    int args[8];        /* up to 8 args as vregs */
    int float_result;   /* 1 if call returns float */
    int is_unsigned;    /* 1 if unsigned operation */
} MirInst;

typedef struct {
    int id;
    int inst_start;     /* index into MirFn.insts */
    int inst_count;
    int term_type;      /* MIR_JUMP, MIR_BRANCH, MIR_RET, or -1 */
    int term_vreg;      /* vreg for branch condition or return value */
    int term_target;    /* target block (JUMP) */
    int term_true;      /* true branch target (BRANCH) */
    int term_false;     /* false branch target (BRANCH) */
} MirBlock;

typedef struct {
    MirInst insts[MIR_MAX_INSTS];
    int inst_count;
    MirBlock blocks[MIR_MAX_BLOCKS];
    int block_count;
    int num_vregs;      /* next virtual register number */
    int num_params;     /* number of function parameters */
    char name[64];      /* function name */
    Node *ast_fn;       /* original AST function node */
} MirFn;

/* variable name → vreg mapping */
typedef struct {
    char name[64];
    int vreg;
    int type;           /* ValType */
    int is_float;
    int is_heap;
} MirVar;

typedef struct {
    MirVar vars[MIR_MAX_VREGS];
    int count;
} MirSym;

void mir_sym_init(MirSym *s);
int mir_sym_add(MirSym *s, const char *name, int vreg, int type);
int mir_sym_find(MirSym *s, const char *name);

/* builder */
MirFn *mir_build_fn(Node *fn_decl, Diag *diag, const char *source);

/* optimizations */
void mir_opt(MirFn *fn);

/* debug */
void mir_print(MirFn *fn);

/* AST-level optimization entry point */
void ast_optimize(Node *prog);

/* lookup helpers */
int mir_new_vreg(MirFn *fn);
int mir_new_block(MirFn *fn);
void mir_add_inst(MirFn *fn, int block_id, MirInst *inst);
void mir_set_term(MirFn *fn, int block_id, int term_type, int vreg, int target, int ttrue, int tfalse);

#endif
