#ifndef CLEAN_LIR_H
#define CLEAN_LIR_H

#include "../mir/mir.h"
#include <stddef.h>
#include <stdio.h>

/* LIR opcodes — machine-level instructions */
enum {
    LIR_NOP,
    LIR_MOV,        /* dst = src (register to register) */
    LIR_MOV_IMM,    /* dst = imm */
    LIR_LOAD,       /* dst = *src (memory to register) */
    LIR_STORE,      /* *dst = src (register to memory) */
    LIR_LEA,        /* dst = address-of-label */
    LIR_ADD, LIR_SUB, LIR_IMUL,
    LIR_CMP,        /* compare, sets flags */
    LIR_SETcc,      /* set byte on condition (cc = 0-7 for eq/ne/lt/le/gt/ge) */
    LIR_JMP,        /* unconditional jump to label */
    LIR_Jcc,        /* conditional jump to label (cc = same as SETcc) */
    LIR_CALL,       /* call function */
    LIR_RET,        /* return from function */
    LIR_PUSH, LIR_POP,
    LIR_NEG, LIR_NOT,
    LIR_AND, LIR_OR, LIR_XOR,
    LIR_SHL, LIR_SHR, LIR_SAR,
    LIR_CQTO,       /* sign-extend rax to rdx:rax (for idiv) */
    LIR_DIV,        /* unsigned div: rax/divisor → rax=quot, rdx=rem */
    LIR_IDIV,       /* signed div */
    LIR_MOVSX,      /* sign-extend move */
    LIR_MOVZX,      /* zero-extend move */
    LIR_MOVSS,      /* movss (f32) */
    LIR_ADDSD,      /* addsd (f64) */
    LIR_SUBSD,      /* subsd */
    LIR_MULSD,      /* mulsd */
    LIR_DIVSD,      /* divsd */
    LIR_MOVQ,       /* movq rax, xmm0 / movq xmm0, rax */
    LIR_LABEL,      /* label definition */
    LIR_MOV_MEM,    /* mov with memory operand: dst = [base + offset] */
    LIR_MOV_MEM_R,  /* mov [base + offset] = src */
    LIR_NEG_MEM,    /* neg [addr] */
    LIR_NOT_MEM,    /* not [addr] */
    LIR_ADD_MEM,    /* add [addr], imm */
};

/* condition codes for SETcc / Jcc */
enum {
    CC_E  = 0,  /* equal / zero */
    CC_NE = 1,  /* not equal */
    CC_L  = 2,  /* less (signed) */
    CC_LE = 3,  /* less or equal */
    CC_G  = 4,  /* greater (signed) */
    CC_GE = 5,  /* greater or equal */
    CC_B  = 6,  /* below (unsigned less) */
    CC_BE = 7,  /* below or equal */
    CC_A  = 8,  /* above (unsigned greater) */
    CC_AE = 9,  /* above or equal */
};

/* register representation */
#define LREG_RAX  0
#define LREG_RCX  1
#define LREG_RDX  2
#define LREG_RBX  3
#define LREG_RSP  4
#define LREG_RBP  5
#define LREG_RSI  6
#define LREG_RDI  7
#define LREG_R8   8
#define LREG_R9   9
#define LREG_R10  10
#define LREG_R11  11
#define LREG_R12  12
#define LREG_R13  13
#define LREG_R14  14
#define LREG_R15  15
#define LREG_XMM0 16
#define LREG_XMM1 17
#define LREG_XMM2 18
#define LREG_XMM3 19
#define LREG_XMM4 20
#define LREG_XMM5 21
#define LREG_XMM6 22
#define LREG_XMM7 23
#define LREG_VIRT 100  /* virtual register base */

#define MAX_LIR_INSTS 8192
#define MAX_PHYS_REGS 24  /* rax-r15 + xmm0-xmm7 */
#define MAX_CALLEE_SAVED 8  /* rbx, r12, r13, r14, r15, xmm6, xmm7 */

typedef struct {
    int opcode;
    int dst;            /* destination (virtual or phys register) */
    int src1, src2;     /* source operands */
    long long imm;      /* immediate */
    int label_id;       /* label index (for jumps) */
    int cc;             /* condition code */
    int is_float;       /* 1 if this is a float operation */
    int is_unsigned;    /* 1 if unsigned comparison */
    int vreg;           /* for spill: which vreg this stores/loads */
    /* for call instructions */
    char callee[64];
    int num_args;
    int args[8];
} LirInst;

typedef struct {
    LirInst insts[MAX_LIR_INSTS];
    int count;
    int num_vregs;      /* highest virtual register used */
    int num_labels;
    int *vreg_to_label; /* stack slot labels for spilled vregs (-1 if not spilled) */
    int num_spills;
} LirFn;

/* lowering */
LirFn *lir_lower(MirFn *mir);

/* debug */
void lir_print(LirFn *lir);

/* register allocation */
void regalloc_run(LirFn *lir);

/* assembly emission */
void emit_lir_function(FILE *fp, LirFn *lir, const char *fn_name, int num_spills, int is_pub);

#endif
