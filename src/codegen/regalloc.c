#include "../lir/lir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VREGS 512
#define MAX_INTERVALS 512

/* physical register names */
static const char *phys_reg_names[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
};

#define GP_REGS 16
#define ALL_REGS 24
#define CALLEE_SAVED_GP 5 /* rbx, r12, r13, r14, r15 */
#define CALLEE_SAVED_XMM 2 /* xmm6, xmm7 */

static int callee_saved_gp[] = {LREG_RBX, LREG_R12, LREG_R13, LREG_R14, LREG_R15};
static int callee_saved_xmm[] = {LREG_XMM6, LREG_XMM7};

typedef struct {
    int vreg;
    int start;      /* first instruction index where vreg is defined */
    int end;        /* last instruction index where vreg is used */
    int phys_reg;   /* assigned phys reg (-1 if spilled) */
    int spill_slot; /* stack offset if spilled */
    int is_float;
    int is_fixed;   /* assigned to fixed reg (e.g., rdi for arg) */
    int fixed_reg;
} LiveInterval;

typedef struct {
    LiveInterval intervals[MAX_INTERVALS];
    int count;
    int max_vreg;
    int stack_used;   /* total stack space used for spills */
} RegAlloc;

static void ra_init(RegAlloc *ra) {
    ra->count = 0;
    ra->max_vreg = 0;
    ra->stack_used = 0;
}

static int ra_find(RegAlloc *ra, int vreg) {
    for (int i = 0; i < ra->count; i++)
        if (ra->intervals[i].vreg == vreg) return i;
    return -1;
}

static void compute_live_intervals(LirFn *lir, RegAlloc *ra) {
    /* find the max vreg */
    int max_vreg = 0;
    for (int i = 0; i < lir->count; i++) {
        LirInst *inst = &lir->insts[i];
        if (inst->dst >= max_vreg) max_vreg = inst->dst + 1;
        if (inst->src1 >= max_vreg) max_vreg = inst->src1 + 1;
        if (inst->src2 >= max_vreg) max_vreg = inst->src2 + 1;
        for (int k = 0; k < 8; k++)
            if (inst->args[k] >= max_vreg) max_vreg = inst->args[k] + 1;
    }
    ra->max_vreg = max_vreg;

    /* initialize intervals */
    for (int v = 0; v < max_vreg; v++) {
        LiveInterval *iv = &ra->intervals[ra->count++];
        iv->vreg = v;
        iv->start = 999999;
        iv->end = -1;
        iv->phys_reg = -1;
        iv->spill_slot = -1;
        iv->is_float = 0;
        iv->is_fixed = 0;

        /* find first def and last use */
        for (int i = 0; i < lir->count; i++) {
            LirInst *inst = &lir->insts[i];
            if (inst->opcode == LIR_LABEL || inst->opcode == LIR_JMP ||
                inst->opcode == LIR_Jcc || inst->opcode == LIR_RET)
                continue;

            /* check if this vreg is defined here */
            int defines = 0;
            if (inst->dst == v) defines = 1;
            if (inst->opcode == LIR_CALL) {
                /* call clobbers caller-saved regs */
                if (inst->dst == v) defines = 1;
            }
            if (defines) {
                if (i < iv->start) iv->start = i;
            }

            /* check if vreg is used here */
            int uses = 0;
            if (inst->src1 == v || inst->src2 == v) uses = 1;
            for (int k = 0; k < 8; k++)
                if (inst->args[k] == v) uses = 1;
            if (inst->opcode == LIR_CMP && inst->dst == v) uses = 1; /* CMP uses dst */
            if (inst->opcode == LIR_STORE && inst->src1 == v) uses = 1;
            if (uses || defines) {
                if (i > iv->end) iv->end = i;
            }
            if (inst->is_float) iv->is_float = 1;
        }

        /* if start > end, this vreg is never used */
        if (iv->start > iv->end) {
            iv->start = iv->end = -1;
        }
    }

    /* mark fixed registers for function args (rdi, rsi, rdx, rcx, r8, r9) */
    /* These are vregs that should be mapped to arg registers on entry */
    for (int v = 0; v < max_vreg && v < 6; v++) {
        ra->intervals[v].is_fixed = 1;
        int arg_regs[] = {LREG_RDI, LREG_RSI, LREG_RDX, LREG_RCX, LREG_R8, LREG_R9};
        ra->intervals[v].fixed_reg = arg_regs[v];
    }
}

static int alloc_reg(RegAlloc *ra, LiveInterval *iv, int pos) {
    int phys;
    int regs[] = {LREG_RAX, LREG_RCX, LREG_RDX, LREG_RSI, LREG_RDI, LREG_R8, LREG_R9,
                  LREG_R10, LREG_R11, LREG_RBX, LREG_R12, LREG_R13, LREG_R14, LREG_R15};
    int nregs = 14; /* all GP regs */

    /* try to find a free register */
    for (int i = 0; i < nregs; i++) {
        int found = 0;
        for (int j = 0; j < ra->count; j++) {
            if (j == (int)(iv - ra->intervals)) continue;
            if (ra->intervals[j].phys_reg == regs[i] &&
                ra->intervals[j].start <= pos &&
                ra->intervals[j].end >= pos) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return regs[i];
        }
    }

    /* no free register: spill the one with farthest end */
    int farthest = -1;
    int farthest_pos = -1;
    for (int j = 0; j < ra->count; j++) {
        if (j == (int)(iv - ra->intervals)) continue;
        if (ra->intervals[j].phys_reg >= 0 &&
            ra->intervals[j].start <= pos &&
            ra->intervals[j].end >= pos) {
            if (ra->intervals[j].end > farthest_pos) {
                farthest_pos = ra->intervals[j].end;
                farthest = j;
            }
        }
    }

    if (farthest >= 0) {
        /* spill it */
        phys = ra->intervals[farthest].phys_reg;
        ra->intervals[farthest].phys_reg = -1;
        if (ra->intervals[farthest].spill_slot < 0) {
            ra->intervals[farthest].spill_slot = ra->stack_used;
            ra->stack_used += 8;
        }
        return phys;
    }

    return LREG_RAX; /* fallback */
}

static int vreg_has_interval(RegAlloc *ra, int vreg) {
    for (int i = 0; i < ra->count; i++)
        if (ra->intervals[i].vreg == vreg) return 1;
    return 0;
}

void regalloc_run(LirFn *lir) {
    RegAlloc ra;
    ra_init(&ra);

    /* compute live intervals */
    compute_live_intervals(lir, &ra);

    /* linear scan: assign registers */
    /* sort intervals by start position (they're already in vreg order, which is generally increasing) */

    /* simple greedy allocation */
    for (int i = 0; i < ra.count; i++) {
        LiveInterval *iv = &ra.intervals[i];
        if (iv->start < 0) continue;
        if (iv->vreg >= lir->num_vregs) continue;

        if (iv->is_fixed) {
            iv->phys_reg = iv->fixed_reg;
            continue;
        }

        int phys = alloc_reg(&ra, iv, iv->start);
        if (phys >= 0) iv->phys_reg = phys;
    }

    /* apply allocation: replace vregs with phys regs in LIR */
    for (int i = 0; i < lir->count; i++) {
        LirInst *inst = &lir->insts[i];
        if (inst->opcode == LIR_LABEL || inst->opcode == LIR_JMP ||
            inst->opcode == LIR_Jcc || inst->opcode == LIR_RET)
            continue;

        /* replace dst */
        if (inst->dst >= 0 && inst->dst < ra.max_vreg) {
            int idx = ra_find(&ra, inst->dst);
            if (idx >= 0 && ra.intervals[idx].phys_reg >= 0)
                inst->dst = ra.intervals[idx].phys_reg;
        }

        /* replace src1 */
        if (inst->src1 >= 0 && inst->src1 < ra.max_vreg) {
            int idx = ra_find(&ra, inst->src1);
            if (idx >= 0 && ra.intervals[idx].phys_reg >= 0)
                inst->src1 = ra.intervals[idx].phys_reg;
        }

        /* replace src2 */
        if (inst->src2 >= 0 && inst->src2 < ra.max_vreg) {
            int idx = ra_find(&ra, inst->src2);
            if (idx >= 0 && ra.intervals[idx].phys_reg >= 0)
                inst->src2 = ra.intervals[idx].phys_reg;
        }

        /* replace args */
        for (int k = 0; k < 8; k++) {
            if (inst->args[k] >= 0 && inst->args[k] < ra.max_vreg) {
                int idx = ra_find(&ra, inst->args[k]);
                if (idx >= 0 && ra.intervals[idx].phys_reg >= 0)
                    inst->args[k] = ra.intervals[idx].phys_reg;
            }
        }
    }

    /* set stack frame size */
    lir->num_spills = ra.stack_used / 8;
}

void regalloc_print(LirFn *lir) {
    printf("RegAlloc: %d spills\n", lir->num_spills);
    for (int i = 0; i < lir->count; i++) {
        LirInst *inst = &lir->insts[i];
        if (inst->opcode == LIR_LABEL) continue;
        printf("  %3d: ", i);
        if (inst->dst >= 0 && inst->dst < 24) printf("%s", phys_reg_names[inst->dst]);
        else printf("v%d", inst->dst);
        printf(" = ");
        if (inst->src1 >= 0 && inst->src1 < 24) printf("%s", phys_reg_names[inst->src1]);
        else printf("v%d", inst->src1);
        if (inst->src2 >= 0) {
            if (inst->src2 < 24) printf(", %s", phys_reg_names[inst->src2]);
            else printf(", v%d", inst->src2);
        }
        printf(" (opcode=%d)\n", inst->opcode);
    }
}
