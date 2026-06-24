#include "codegen.h"
#include "../ast.h"
#include "../diag.h"
#include "../parser/parser.h"
#include "../runtime/clgui_embed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>

#define MAX_VARS 256
#define MAX_STRS 1024

#define MAX_CG_STRUCTS 32
#define MAX_CG_FIELDS 32

typedef struct {
    char name[64];
    char fields[MAX_CG_FIELDS][64];
    int offsets[MAX_CG_FIELDS];
    int field_count;
    int total_size;
} CStructDef;

typedef struct {
    char names[MAX_VARS][64];
    int offsets[MAX_VARS];
    int struct_idx[MAX_VARS];   /* -1 = not a struct, >=0 = struct table index */
    int count;
    int stack_size;
} SymTab;

static void sym_init(SymTab *s) { s->count = 0; s->stack_size = 8; }

static int sym_find(SymTab *s, const char *name) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->names[i], name) == 0) return s->offsets[i];
    return -1;
}

static int sym_add(SymTab *s, const char *name) {
    if (s->count >= MAX_VARS) { fprintf(stderr, "too many variables\n"); exit(1); }
    strncpy(s->names[s->count], name, 63);
    s->names[s->count][63] = 0;
    s->stack_size += 8;
    s->offsets[s->count] = s->stack_size;
    s->struct_idx[s->count] = -1;
    s->count++;
    return s->offsets[s->count-1];
}

static int sym_add_struct(SymTab *s, const char *name, int st_idx) {
    int off = sym_add(s, name);
    s->struct_idx[s->count-1] = st_idx;
    return off;
}

typedef struct {
    FILE *out;
    SymTab sym;
    int label_seq;
    Diag *diag;
    const char *strs[MAX_STRS];
    int str_cnt;
    int loop_start[64];
    int loop_end[64];
    int loop_sp;
    CStructDef structs[MAX_CG_STRUCTS];
    int struct_count;
    int gui_mode;
} Codegen;

static int new_label(Codegen *c) { return c->label_seq++; }

static void emit(Codegen *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(c->out, fmt, ap);
    va_end(ap);
}

static int get_str_id(Codegen *c, const char *s) {
    for (int i = 0; i < c->str_cnt; i++)
        if (strcmp(c->strs[i], s) == 0) return i;
    if (c->str_cnt < MAX_STRS) c->strs[c->str_cnt++] = s;
    return c->str_cnt - 1;
}

static void emit_strtab(Codegen *c) {
    if (c->str_cnt == 0) return;
    emit(c, ".section .rodata\n");
    for (int i = 0; i < c->str_cnt; i++) {
        const char *p = c->strs[i];
        size_t len = strlen(p);
        const size_t CHUNK = 4096;
        if (len <= CHUNK) {
            emit(c, ".L_str_%d: .asciz \"", i);
            while (*p) {
                char ch = *p++;
                if (ch == '"') emit(c, "\\\"");
                else if (ch == '\\') emit(c, "\\\\");
                else if (ch == '\n') emit(c, "\\n");
                else if (ch == '\t') emit(c, "\\t");
                else fputc(ch, c->out);
            }
            emit(c, "\"\n");
        } else {
            emit(c, ".L_str_%d:\n", i);
            while (len > 0) {
                size_t n = len > CHUNK ? CHUNK : len;
                fputs("  .ascii \"", c->out);
                for (size_t j = 0; j < n; j++, p++) {
                    char ch = *p;
                    if (ch == '"') fputs("\\\"", c->out);
                    else if (ch == '\\') fputs("\\\\", c->out);
                    else if (ch == '\n') fputs("\\n", c->out);
                    else if (ch == '\t') fputs("\\t", c->out);
                    else fputc(ch, c->out);
                }
                fputs("\"\n", c->out);
                len -= n;
            }
            emit(c, "  .byte 0\n");
        }
    }
    emit(c, ".section .text\n");
}

static void emit_print_int(Codegen *c) {
    int lbl = new_label(c);
    emit(c, "\n# print_int(rdi) -> void\n");
    emit(c, ".globl print_int\n");
    emit(c, ".type print_int, @function\n");
    emit(c, "print_int:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 48\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  lea rsi, [rbp-40]\n  add rsi, 19\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n  mov rcx, 10\n");
    emit(c, "  cmp rax, 0\n");
    emit(c, "  jne .L_print_loop_%d\n", lbl);
    emit(c, "  dec rsi\n  mov byte ptr [rsi], '0'\n");
    emit(c, "  jmp .L_print_done_%d\n", lbl);
    emit(c, ".L_print_loop_%d:\n", lbl);
    emit(c, "  xor rdx, rdx\n  div rcx\n  add dl, '0'\n  dec rsi\n");
    emit(c, "  mov byte ptr [rsi], dl\n  cmp rax, 0\n");
    emit(c, "  jne .L_print_loop_%d\n", lbl);
    emit(c, ".L_print_done_%d:\n", lbl);
    emit(c, "  xor rdx, rdx\n  lea rbx, [rbp-40]\n  add rbx, 19\n");
    emit(c, "  sub rbx, rsi\n  mov rdx, rbx\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov byte ptr [rbp-48], 10\n");
    emit(c, "  mov rdi, 1\n  lea rsi, [rbp-48]\n  mov rdx, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_sleep(Codegen *c) {
    emit(c, "\n# sleep(rdi)\n");
    emit(c, ".globl sleep\n.type sleep, @function\nsleep:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-16], rdi\n  mov qword ptr [rbp-8], 0\n");
    emit(c, "  lea rdi, [rbp-16]\n  xor rsi, rsi\n  mov rax, 35\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_time_ms(Codegen *c) {
    emit(c, "\n# time_ms() -> rax\n");
    emit(c, ".globl time_ms\n.type time_ms, @function\ntime_ms:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n");
    emit(c, "  mov rdi, 1\n");
    emit(c, "  lea rsi, [rbp-16]\n");
    emit(c, "  mov rax, 228\n  syscall\n");
    emit(c, "  mov rax, qword ptr [rbp-16]\n");
    emit(c, "  mov rcx, 1000\n  mul rcx\n");
    emit(c, "  mov r8, rax\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  mov rcx, 1000000\n  mov rdx, 0\n  div rcx\n");
    emit(c, "  add rax, r8\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_print_str(Codegen *c) {
    emit(c, "\n# print_str(rdi=ptr, rsi=len) -> void\n");
    emit(c, ".globl print_str\n");
    emit(c, ".type print_str, @function\n");
    emit(c, "print_str:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rdx, rsi\n  mov rsi, rdi\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_read_int(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# read_int() -> rax (supports negative numbers)\n");
    emit(c, ".globl read_int\n");
    emit(c, ".type read_int, @function\n");
    emit(c, "read_int:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 16\n");
    emit(c, "  xor r8, r8\n");      /* r8 = accumulator */
    emit(c, "  xor r9, r9\n");      /* r9 = negative flag (0=pos, 1=neg) */
    /* read first byte to check for '-' */
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-1]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_rd_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-1]\n");
    emit(c, "  cmp al, '-'\n  jne .L_rd_digit_%d\n", L);
    emit(c, "  mov r9, 1\n");       /* set negative flag */
    /* fall through to digit loop */
    emit(c, ".L_rd_read_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-2]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_rd_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-2]\n");
    emit(c, "  jmp .L_rd_check_%d\n", L);
    /* re-entry point for subsequent digits (read into rbp-2) */
    emit(c, ".L_rd_loop_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-2]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_rd_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-2]\n");
    emit(c, ".L_rd_check_%d:\n", L);
    emit(c, "  cmp al, '0'\n  jb .L_rd_done_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_rd_done_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx rcx, al\n");
    emit(c, "  imul r8, r8, 10\n  add r8, rcx\n");
    emit(c, "  jmp .L_rd_loop_%d\n", L);
    /* non-digit means we already read it; go to digit loop or done */
    emit(c, ".L_rd_digit_%d:\n", L);
    emit(c, "  cmp al, '0'\n  jb .L_rd_done_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_rd_done_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx rcx, al\n");
    emit(c, "  imul r8, r8, 10\n  add r8, rcx\n");
    emit(c, "  jmp .L_rd_loop_%d\n", L);
    emit(c, ".L_rd_done_%d:\n", L);
    emit(c, "  mov rax, r8\n");
    /* if negative flag is set, negate */
    emit(c, "  test r9, r9\n  je .L_rd_pos_%d\n", L);
    emit(c, "  neg rax\n");
    emit(c, ".L_rd_pos_%d:\n", L);
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_input(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# input(rdi=prompt) -> rax=buffer (null-terminated)\n");
    emit(c, ".globl input\n.type input, @function\ninput:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  push rbx\n  push r12\n  sub rsp, 16\n");
    emit(c, "  mov qword ptr [rbp-32], rdi\n");
    /* strlen of prompt (clobbers rcx, rdi) */
    emit(c, "  mov rdi, qword ptr [rbp-32]\n");
    emit(c, "  xor rax, rax\n  mov rcx, -1\n");
    emit(c, "  repne scasb\n  not rcx\n  dec rcx\n");
    emit(c, "  mov rsi, rcx\n");
    /* print_str(prompt, len) */
    emit(c, "  mov rdi, qword ptr [rbp-32]\n");
    emit(c, "  call print_str\n");
    /* read line from stdin into input_buf (use r12 as index — preserved by syscall) */
    emit(c, "  lea rbx, [rip + input_buf]\n");
    emit(c, "  xor r12, r12\n");
    emit(c, ".L_input_loop_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbx + r12]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_input_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbx + r12]\n");
    emit(c, "  cmp al, 10\n  je .L_input_done_%d\n", L);
    emit(c, "  inc r12\n  cmp r12, 1023\n  jb .L_input_loop_%d\n", L);
    emit(c, ".L_input_done_%d:\n", L);
    emit(c, "  mov byte ptr [rbx + r12], 0\n");
    emit(c, "  mov rax, rbx\n");
    emit(c, "  add rsp, 16\n  pop r12\n  pop rbx\n  pop rbp\n  ret\n");
}

static void emit_strlen(Codegen *c) {
    emit(c, "\n# strlen(rdi=str) -> rax=len\n");
    emit(c, ".globl strlen\n.type strlen, @function\nstrlen:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  xor rax, rax\n  mov rcx, -1\n");
    emit(c, "  repne scasb\n  not rcx\n  dec rcx\n");
    emit(c, "  mov rax, rcx\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_inspect(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# inspect(rdi) -> rax (prints \"inspect: N\\n\", returns value)\n");
    emit(c, ".globl inspect\n.type inspect, @function\ninspect:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 16\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  lea rdi, [rip + .Linsp_str_%d]\n", L);
    emit(c, "  mov rsi, 9\n  call print_str\n");
    emit(c, "  mov rdi, qword ptr [rbp-8]\n  call print_int\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
    emit(c, ".Linsp_str_%d: .asciz \"inspect: \"\n", L);
}

static void emit_assert(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# assert(rdi) -> void (aborts if rdi == 0)\n");
    emit(c, ".globl assert\n.type assert, @function\nassert:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  test rdi, rdi\n  jnz .L_assert_ok_%d\n", L);
    emit(c, "  lea rdi, [rip + .Lassert_str_%d]\n", L);
    emit(c, "  mov rsi, 17\n  call print_str\n");
    emit(c, "  mov rdi, 1\n  mov rax, 60\n  syscall\n");
    emit(c, ".L_assert_ok_%d:\n  mov rsp, rbp\n  pop rbp\n  ret\n", L);
    emit(c, ".Lassert_str_%d: .asciz \"assertion failed\\n\"\n", L);
}

static void emit_clear_screen(Codegen *c) {
    emit(c, "\n# clear_screen() -> void\n");
    emit(c, ".globl clear_screen\n.type clear_screen, @function\nclear_screen:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_cls]\n  mov rdx, 7\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_reset_attr(Codegen *c) {
    emit(c, "\n# reset_attr() -> void\n");
    emit(c, ".globl reset_attr\n.type reset_attr, @function\nreset_attr:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_reset]\n  mov rdx, 4\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_hide_cursor(Codegen *c) {
    emit(c, "\n# hide_cursor() -> void\n");
    emit(c, ".globl hide_cursor\n.type hide_cursor, @function\nhide_cursor:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_hide]\n  mov rdx, 6\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_show_cursor(Codegen *c) {
    emit(c, "\n# show_cursor() -> void\n");
    emit(c, ".globl show_cursor\n.type show_cursor, @function\nshow_cursor:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_show]\n  mov rdx, 6\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_get_frame_ptr(Codegen *c) {
    emit(c, "\n# get_frame_ptr(rdi=base, rsi=idx, rdx=fsize) -> rax=ptr\n");
    emit(c, ".globl get_frame_ptr\n.type get_frame_ptr, @function\nget_frame_ptr:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, rsi\n  mul rdx\n  add rax, rdi\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_set_fg(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# set_fg(rdi=color) -> void (256-color foreground)\n");
    emit(c, ".globl set_fg\n.type set_fg, @function\nset_fg:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_fg]\n");
    emit(c, "  mov rdx, 7\n  syscall\n");
    /* convert color index to decimal */
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  lea rsi, [rbp-32]\n  add rsi, 19\n");
    emit(c, "  mov rcx, 10\n  cmp rax, 0\n");
    emit(c, "  jne .L_fg_loop_%d\n", L);
    emit(c, "  dec rsi\n  mov byte ptr [rsi], '0'\n");
    emit(c, "  jmp .L_fg_done_%d\n", L);
    emit(c, ".L_fg_loop_%d:\n", L);
    emit(c, "  xor rdx, rdx\n  div rcx\n");
    emit(c, "  add dl, '0'\n  dec rsi\n  mov byte ptr [rsi], dl\n");
    emit(c, "  cmp rax, 0\n  jne .L_fg_loop_%d\n", L);
    emit(c, ".L_fg_done_%d:\n", L);
    /* write digits */
    emit(c, "  lea rbx, [rbp-32]\n  add rbx, 19\n");
    emit(c, "  sub rbx, rsi\n  mov rdx, rbx\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    /* write 'm' suffix */
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_m]\n");
    emit(c, "  mov rdx, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_set_bg(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# set_bg(rdi=color) -> void (256-color background)\n");
    emit(c, ".globl set_bg\n.type set_bg, @function\nset_bg:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_bg]\n");
    emit(c, "  mov rdx, 7\n  syscall\n");
    /* convert color index to decimal */
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  lea rsi, [rbp-32]\n  add rsi, 19\n");
    emit(c, "  mov rcx, 10\n  cmp rax, 0\n");
    emit(c, "  jne .L_bg_loop_%d\n", L);
    emit(c, "  dec rsi\n  mov byte ptr [rsi], '0'\n");
    emit(c, "  jmp .L_bg_done_%d\n", L);
    emit(c, ".L_bg_loop_%d:\n", L);
    emit(c, "  xor rdx, rdx\n  div rcx\n");
    emit(c, "  add dl, '0'\n  dec rsi\n  mov byte ptr [rsi], dl\n");
    emit(c, "  cmp rax, 0\n  jne .L_bg_loop_%d\n", L);
    emit(c, ".L_bg_done_%d:\n", L);
    /* write digits */
    emit(c, "  lea rbx, [rbp-32]\n  add rbx, 19\n");
    emit(c, "  sub rbx, rsi\n  mov rdx, rbx\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    /* write 'm' suffix */
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_m]\n");
    emit(c, "  mov rdx, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_calc_expr(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# calc_expr() -> rax (reads line, evaluates with precedence)\n");
    emit(c, ".globl calc_expr\n");
    emit(c, ".type calc_expr, @function\n");
    emit(c, "calc_expr:\n");
    emit(c, "  push rbp\n  push rbx\n  mov rbp, rsp\n  sub rsp, 256\n");
    /* read line from stdin into [rbp-256..rbp-1]; rbx = length (not rcx — syscall clobbers rcx) */
    emit(c, "  xor rbx, rbx\n");
    emit(c, ".L_ce_read_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-256]\n  add rsi, rbx\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_ce_eof_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-256+rbx]\n");
    emit(c, "  cmp al, 10\n  je .L_ce_rdone_%d\n", L);
    emit(c, "  inc rbx\n  cmp rbx, 255\n  jb .L_ce_read_%d\n", L);
    emit(c, ".L_ce_eof_%d:\n", L);
    emit(c, "  test rbx, rbx\n  jz .L_ce_exit_%d\n", L);
    emit(c, "  jmp .L_ce_rdone_%d\n", L);
    emit(c, ".L_ce_rdone_%d:\n", L);
    emit(c, "  mov byte ptr [rbp-256+rbx], 0\n");
    emit(c, "  test rbx, rbx\n  jz .L_ce_empty_%d\n", L);
    /* parse first number into r8 (current term) */
    emit(c, "  lea rsi, [rbp-256]\n");
    emit(c, ".L_ce_skip1_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, ' '\n");
    emit(c, "  jne .L_ce_got1_%d\n  inc rsi\n  jmp .L_ce_skip1_%d\n", L, L);
    emit(c, ".L_ce_got1_%d:\n", L);
    emit(c, "  xor r8, r8\n");
    emit(c, ".L_ce_num1_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, '0'\n  jb .L_ce_num1d_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_ce_num1d_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx r9, al\n  imul r8, r8, 10\n  add r8, r9\n");
    emit(c, "  inc rsi\n  jmp .L_ce_num1_%d\n", L);
    emit(c, ".L_ce_num1d_%d:\n", L);
    emit(c, "  xor r9, r9\n");  /* r9 = result sum */
    /* main evaluation loop */
    emit(c, ".L_ce_main_%d:\n", L);
    /* skip whitespace before operator */
    emit(c, ".L_ce_skip2_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, ' '\n");
    emit(c, "  jne .L_ce_gotop_%d\n  inc rsi\n  jmp .L_ce_skip2_%d\n", L, L);
    emit(c, ".L_ce_gotop_%d:\n", L);
    emit(c, "  cmp al, 0\n  je .L_ce_done_%d\n", L);
    emit(c, "  mov r11b, al\n  inc rsi\n");
    /* skip whitespace after operator */
    emit(c, ".L_ce_skip3_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, ' '\n");
    emit(c, "  jne .L_ce_gotnum_%d\n  inc rsi\n  jmp .L_ce_skip3_%d\n", L, L);
    emit(c, ".L_ce_gotnum_%d:\n", L);
    /* parse next number into r10 */
    emit(c, "  xor r10, r10\n");
    emit(c, ".L_ce_num2_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, '0'\n  jb .L_ce_num2d_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_ce_num2d_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx rcx, al\n  imul r10, r10, 10\n  add r10, rcx\n");
    emit(c, "  inc rsi\n  jmp .L_ce_num2_%d\n", L);
    emit(c, ".L_ce_num2d_%d:\n", L);
    /* dispatch on operator in r11b */
    emit(c, "  cmp r11b, '+'\n  jne .L_ce_cm_%d\n", L);
    emit(c, "  add r9, r8\n  mov r8, r10\n  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_cm_%d:\n", L);
    emit(c, "  cmp r11b, '-'\n  jne .L_ce_cx_%d\n", L);
    emit(c, "  add r9, r8\n  neg r10\n  mov r8, r10\n  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_cx_%d:\n", L);
    emit(c, "  cmp r11b, '*'\n  je .L_ce_mul_%d\n", L);
    emit(c, "  cmp r11b, 'x'\n  je .L_ce_mul_%d\n", L);
    emit(c, "  cmp r11b, 'X'\n  je .L_ce_mul_%d\n", L);
    emit(c, "  cmp r11b, ':'\n  je .L_ce_div_%d\n", L);
    emit(c, "  cmp r11b, '/'\n  jne .L_ce_main_%d\n", L);
    emit(c, ".L_ce_div_%d:\n", L);
    emit(c, "  mov rax, r8\n  cqo\n  idiv r10\n  mov r8, rax\n");
    emit(c, "  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_mul_%d:\n", L);
    emit(c, "  imul r8, r10\n  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_done_%d:\n", L);
    emit(c, "  add r9, r8\n  mov rax, r9\n");
    emit(c, "  jmp .L_ce_ret_%d\n", L);
    emit(c, ".L_ce_empty_%d:\n", L);
    emit(c, "  xor rax, rax\n");
    emit(c, ".L_ce_ret_%d:\n", L);
    emit(c, "  mov rsp, rbp\n  pop rbx\n  pop rbp\n  ret\n");
    emit(c, ".L_ce_exit_%d:\n", L);
    emit(c, "  mov rsp, rbp\n  pop rbx\n  pop rbp\n");
    emit(c, "  mov rax, 60\n  xor rdi, rdi\n  syscall\n");
}

static void gen_expr(Codegen *c, Node *n);
static void gen_stmt(Codegen *c, Node *n);
static int find_cg_struct(Codegen *c, const char *name);
static void collect_structs(Codegen *c, Node *prog);

static void gen_expr(Codegen *c, Node *n) {
    if (!n) { emit(c, "  xor rax, rax\n"); return; }
    switch (n->type) {
    case NODE_INT: emit(c, "  mov rax, %lld\n", n->int_val); break;
    case NODE_BOOL: emit(c, "  mov rax, %d\n", n->bool_val ? 1 : 0); break;
    case NODE_STR: {
        int id = get_str_id(c, n->str_val);
        emit(c, "  lea rax, [rip + .L_str_%d]\n", id);
        break;
    }
    case NODE_IDENT: {
        int off = sym_find(&c->sym, n->ident);
        if (off < 0) { break; }
        emit(c, "  mov rax, qword ptr [rbp - %d]\n", off);
        break;
    }
    case NODE_UNARY:
        gen_expr(c, n->unary.operand);
        if (n->unary.op == 0) emit(c, "  test rax, rax\n  setz al\n  movzx rax, al\n");
        else emit(c, "  neg rax\n");
        break;
    case NODE_BINARY: {
        gen_expr(c, n->binary.left);
        emit(c, "  push rax\n");
        gen_expr(c, n->binary.right);
        emit(c, "  mov rcx, rax\n  pop rdx\n");
        int op = n->binary.op;
        if (op <= 4) {
            if (op == 0) emit(c, "  mov rax, rdx\n  add rax, rcx\n");
            else if (op == 1) emit(c, "  mov rax, rdx\n  sub rax, rcx\n");
            else if (op == 2) emit(c, "  mov rax, rdx\n  mul rcx\n");
            else if (op == 3) emit(c, "  mov rax, rdx\n  xor rdx, rdx\n  div rcx\n");
            else if (op == 4) emit(c, "  mov rax, rdx\n  xor rdx, rdx\n  div rcx\n  mov rax, rdx\n");
        } else if (op <= 10) {
            static const char *cc[] = {"sete","setne","setl","setle","setg","setge"};
            emit(c, "  cmp rdx, rcx\n  %s al\n  movzx rax, al\n", cc[op-5]);
        } else if (op == 11) {
            int l = new_label(c);
            emit(c, "  test rdx, rdx\n  je .L_and_false_%d\n", l);
            emit(c, "  test rcx, rcx\n  je .L_and_false_%d\n", l);
            emit(c, "  mov rax, 1\n  jmp .L_and_end_%d\n", l);
            emit(c, ".L_and_false_%d:\n  xor rax, rax\n.L_and_end_%d:\n", l, l);
        } else if (op == 12) {
            int l = new_label(c);
            emit(c, "  test rdx, rdx\n  jne .L_or_true_%d\n", l);
            emit(c, "  test rcx, rcx\n  je .L_or_false_%d\n", l);
            emit(c, ".L_or_true_%d:\n  mov rax, 1\n  jmp .L_or_end_%d\n", l, l);
            emit(c, ".L_or_false_%d:\n  xor rax, rax\n.L_or_end_%d:\n", l, l);
        }
        break;
    }
    case NODE_CALL: {
        int argc = 0;
        for (Node *a = n->call.args; a; a = a->next) argc++;
        if (argc > 0) {
            Node *args[16];
            int i = 0;
            for (Node *a = n->call.args; a && i < 16; a = a->next) args[i++] = a;
            for (int j = i-1; j >= 0; j--) { gen_expr(c, args[j]); emit(c, "  push rax\n"); }
        }
        static const char *arg_regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
        int n_pop = argc < 6 ? argc : 6;
        for (int i = 0; i < n_pop; i++) emit(c, "  pop %s\n", arg_regs[i]);
        if (n->call.callee->type == NODE_IDENT)
            emit(c, "  call %s\n", n->call.callee->ident);
        else { gen_expr(c, n->call.callee); emit(c, "  call rax\n"); }
        break;
    }
    case NODE_COMPREHENSION: {
        int L = new_label(c);
        /* add loop variable to symtab (ensures sym_find works even after count_locals reset) */
        int var_off = -1;
        if (n->comp.var) {
            var_off = sym_find(&c->sym, n->comp.var);
            if (var_off < 0) var_off = sym_add(&c->sym, n->comp.var);
        }
        if (var_off < 0) { emit(c, "  xor rax, rax\n"); break; }
        /* push callee-saved regs we'll use: r12 for counter, rbx for var_off copy */
        emit(c, "  push r12\n  push rbx\n");
        /* compute iter start */
        gen_expr(c, n->comp.iter);
        emit(c, "  mov qword ptr [rbp - %d], rax\n", var_off);
        /* compute iter end */
        Node *end = n->comp.iter_end;
        if (!end) {
            /* if no end, iterate once: set end = start + 1 */
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", var_off);
            emit(c, "  inc rax\n");
            int end_off = var_off + 8; /* use next slot (may overlap, good enough for demo) */
            emit(c, "  mov qword ptr [rbp - %d], rax\n", end_off);
            end = node_new(NODE_INT); end->int_val = 0; /* placeholder, not really used */
        } else {
            gen_expr(c, end);
            /* store end at var_off + 8 (trusting stack layout) */
            emit(c, "  mov qword ptr [rbp - %d], rax\n", var_off + 8);
        }
        /* counter = 0 */
        emit(c, "  xor r12, r12\n");
        /* loop start */
        emit(c, ".L_comp_loop_%d:\n", L);
        /* check var < end */
        emit(c, "  mov rax, qword ptr [rbp - %d]\n", var_off);
        emit(c, "  mov rcx, qword ptr [rbp - %d]\n", var_off + 8);
        emit(c, "  cmp rax, rcx\n  jge .L_comp_done_%d\n", L);
        /* filter check */
        if (n->comp.filter) {
            gen_expr(c, n->comp.filter);
            emit(c, "  test rax, rax\n  je .L_comp_skip_%d\n", L);
        }
        /* map expression */
        gen_expr(c, n->comp.map);
        /* print the result */
        emit(c, "  mov rdi, rax\n  call print_int\n");
        /* increment counter */
        emit(c, "  inc r12\n");
        if (n->comp.filter) emit(c, ".L_comp_skip_%d:\n", L);
        /* increment loop variable */
        emit(c, "  mov rax, qword ptr [rbp - %d]\n", var_off);
        emit(c, "  inc rax\n  mov qword ptr [rbp - %d], rax\n", var_off);
        emit(c, "  jmp .L_comp_loop_%d\n", L);
        emit(c, ".L_comp_done_%d:\n", L);
        /* restore regs, return count */
        emit(c, "  mov rax, r12\n  pop rbx\n  pop r12\n");
        break;
    }
    case NODE_STRUCT_LITERAL: {
        int si = find_cg_struct(c, n->struct_literal.name);
        if (si < 0) { emit(c, "  xor rax, rax\n"); break; }
        CStructDef *s = &c->structs[si];
        int argc = 0;
        for (Node *a = n->struct_literal.args; a; a = a->next) argc++;
        Node *args[16];
        int i = 0;
        for (Node *a = n->struct_literal.args; a && i < 16; a = a->next) args[i++] = a;
        for (int j = i-1; j >= 0; j--) { gen_expr(c, args[j]); emit(c, "  push rax\n"); }
        emit(c, "  mov rax, 12\n  xor rdi, rdi\n  syscall\n");
        emit(c, "  mov rbx, rax\n");
        emit(c, "  add rax, %d\n", s->total_size > 0 ? s->total_size : 8);
        emit(c, "  mov rdi, rax\n  mov rax, 12\n  syscall\n");
        for (int j = 0; j < argc && j < s->field_count; j++) {
            emit(c, "  pop rax\n");
            emit(c, "  mov qword ptr [rbx + %d], rax\n", s->offsets[j]);
        }
        emit(c, "  mov rax, rbx\n");
        break;
    }
    case NODE_INDEX: {
        gen_expr(c, n->index_expr.obj);
        emit(c, "  push rax\n");
        if (n->index_expr.index->type == NODE_IDENT) {
            const char *field = n->index_expr.index->ident;
            int found = 0;
            for (int si = 0; si < c->struct_count && !found; si++) {
                for (int j = 0; j < c->structs[si].field_count; j++) {
                    if (strcmp(c->structs[si].fields[j], field) == 0) {
                        emit(c, "  pop rax\n");
                        emit(c, "  mov rax, qword ptr [rax + %d]\n", c->structs[si].offsets[j]);
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                /* fallback: array indexing */
                emit(c, "  pop rbx\n");
                gen_expr(c, n->index_expr.index);
                emit(c, "  shl rax, 3\n  add rax, rbx\n  mov rax, qword ptr [rax]\n");
            }
        } else {
            emit(c, "  pop rbx\n");
            gen_expr(c, n->index_expr.index);
            emit(c, "  shl rax, 3\n  add rax, rbx\n  mov rax, qword ptr [rax]\n");
        }
        break;
    }
    default: emit(c, "  xor rax, rax\n"); break;
    }
}

static void gen_stmt(Codegen *c, Node *n) {
    if (!n) return;
    switch (n->type) {
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next) gen_stmt(c, s);
        break;
    case NODE_LET: {
        int off;
        if (n->let.init && n->let.init->type == NODE_STRUCT_LITERAL) {
            int si = find_cg_struct(c, n->let.init->struct_literal.name);
            if (si >= 0) off = sym_add_struct(&c->sym, n->let.name, si);
            else off = sym_add(&c->sym, n->let.name);
        } else {
            off = sym_add(&c->sym, n->let.name);
        }
        emit(c, "  mov qword ptr [rbp - %d], 0\n", off);
        if (n->let.init) { gen_expr(c, n->let.init); emit(c, "  mov qword ptr [rbp - %d], rax\n", off); }
        break;
    }
    case NODE_ASSIGN: {
        if (n->assign.lhs->type != NODE_IDENT) break;
        int off = sym_find(&c->sym, n->assign.lhs->ident);
        if (off < 0) break;
        gen_expr(c, n->assign.rhs);
        emit(c, "  mov qword ptr [rbp - %d], rax\n", off);
        break;
    }
    case NODE_IF: {
        int lbl_else = new_label(c), lbl_end = new_label(c);
        gen_expr(c, n->if_stmt.cond);
        emit(c, "  test rax, rax\n  je .L_else_%d\n", lbl_else);
        gen_stmt(c, n->if_stmt.then);
        emit(c, "  jmp .L_endif_%d\n", lbl_end);
        emit(c, ".L_else_%d:\n", lbl_else);
        if (n->if_stmt.otherwise) gen_stmt(c, n->if_stmt.otherwise);
        emit(c, ".L_endif_%d:\n", lbl_end);
        break;
    }
    case NODE_WHILE: {
        int lbl_start = new_label(c), lbl_end = new_label(c);
        c->loop_start[c->loop_sp] = lbl_start;
        c->loop_end[c->loop_sp] = lbl_end;
        c->loop_sp++;
        emit(c, ".L_while_%d:\n", lbl_start);
        gen_expr(c, n->while_stmt.cond);
        emit(c, "  test rax, rax\n  je .L_endwhile_%d\n", lbl_end);
        gen_stmt(c, n->while_stmt.body);
        emit(c, "  jmp .L_while_%d\n", lbl_start);
        emit(c, ".L_endwhile_%d:\n", lbl_end);
        c->loop_sp--;
        break;
    }
    case NODE_RETURN:
        if (n->ret.val) gen_expr(c, n->ret.val);
        else emit(c, "  xor rax, rax\n");
        emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
        break;
    case NODE_BREAK:
        if (c->loop_sp > 0)
            emit(c, "  jmp .L_endwhile_%d\n", c->loop_end[c->loop_sp-1]);
        break;
    case NODE_CONTINUE:
        if (c->loop_sp > 0)
            emit(c, "  jmp .L_while_%d\n", c->loop_start[c->loop_sp-1]);
        break;
    case NODE_EXPR_STMT:
        gen_expr(c, n->expr_stmt.expr);
        break;
    default: break;
    }
}

static void count_locals(SymTab *s, Node *n);

static void count_locals(SymTab *s, Node *n) {
    if (!n) return;
    switch (n->type) {
    case NODE_LET:
        if (n->let.name) sym_add(s, n->let.name);
        if (n->let.init) count_locals(s, n->let.init);
        break;
    case NODE_BLOCK:
        for (Node *c = n->block.stmts; c; c = c->next) count_locals(s, c);
        break;
    case NODE_IF:
        count_locals(s, n->if_stmt.cond);
        count_locals(s, n->if_stmt.then);
        if (n->if_stmt.otherwise) count_locals(s, n->if_stmt.otherwise);
        break;
    case NODE_WHILE:
        count_locals(s, n->while_stmt.cond);
        count_locals(s, n->while_stmt.body);
        break;
    case NODE_ASSIGN:
        if (n->assign.rhs) count_locals(s, n->assign.rhs);
        break;
    case NODE_RETURN:
        if (n->ret.val) count_locals(s, n->ret.val);
        break;
    case NODE_EXPR_STMT:
        if (n->expr_stmt.expr) count_locals(s, n->expr_stmt.expr);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        break;
    case NODE_COMPREHENSION:
        if (n->comp.var) sym_add(s, n->comp.var);
        count_locals(s, n->comp.map);
        count_locals(s, n->comp.iter);
        if (n->comp.iter_end) count_locals(s, n->comp.iter_end);
        if (n->comp.filter) count_locals(s, n->comp.filter);
        break;
    case NODE_CALL:
        for (Node *a = n->call.args; a; a = a->next) count_locals(s, a);
        break;
    case NODE_STRUCT_LITERAL:
        for (Node *a = n->struct_literal.args; a; a = a->next) count_locals(s, a);
        break;
    case NODE_INDEX:
        count_locals(s, n->index_expr.obj);
        count_locals(s, n->index_expr.index);
        break;
    case NODE_BINARY:
        count_locals(s, n->binary.left);
        count_locals(s, n->binary.right);
        break;
    case NODE_UNARY:
        if (n->unary.operand) count_locals(s, n->unary.operand);
        break;
    default: break;
    }
}

static int find_cg_struct(Codegen *c, const char *name) {
    for (int i = 0; i < c->struct_count; i++)
        if (strcmp(c->structs[i].name, name) == 0) return i;
    return -1;
}

static void collect_structs(Codegen *c, Node *prog) {
    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_STRUCT_DECL) {
            if (c->struct_count >= MAX_CG_STRUCTS) continue;
            CStructDef *s = &c->structs[c->struct_count++];
            strncpy(s->name, item->struct_decl.name, 63); s->name[63] = 0;
            s->field_count = 0;
            s->total_size = 0;
            for (Node *f = item->struct_decl.fields; f; f = f->next) {
                if (f->type != NODE_LET || !f->let.name) continue;
                if (s->field_count >= MAX_CG_FIELDS) continue;
                strncpy(s->fields[s->field_count], f->let.name, 63);
                s->fields[s->field_count][63] = 0;
                s->offsets[s->field_count] = s->total_size;
                s->total_size += 8;
                s->field_count++;
            }
        }
    }
}

static void gen_fn(Codegen *c, Node *n) {
    sym_init(&c->sym);
    emit(c, "\n.section .text\n");
    const char *name = n->fn.name;
    if (c->gui_mode && strcmp(name, "main") == 0) name = "clean_main";
    emit(c, ".globl %s\n.type %s, @function\n%s:\n", name, name, name);
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    static const char *arg_regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
    int reg_idx = 0;
    /* count params */
    for (Node *p = n->fn.params; p; p = p->next) {
        if (p->type == NODE_LET && p->let.name) {
            sym_add(&c->sym, p->let.name);
        }
    }
    /* pre-count local variables so frame covers all */
    SymTab tmp;
    memcpy(&tmp, &c->sym, sizeof(tmp));
    count_locals(&c->sym, n->fn.body);
    int frame = c->sym.stack_size;
    if (frame > 0) { frame = (frame + 15) & ~15; emit(c, "  sub rsp, %d\n", frame); }
    /* restore sym table to param-only state, will re-add during gen_stmt */
    memcpy(&c->sym, &tmp, sizeof(tmp));
    /* store params from registers */
    reg_idx = 0;
    for (Node *p = n->fn.params; p; p = p->next) {
        if (p->type == NODE_LET && p->let.name) {
            int off = sym_find(&c->sym, p->let.name);
            if (reg_idx < 6) emit(c, "  mov qword ptr [rbp - %d], %s\n", off, arg_regs[reg_idx]);
            reg_idx++;
        }
    }
    gen_stmt(c, n->fn.body);
    emit(c, "  xor rax, rax\n  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static int has_gui_externs(Node *prog) {
    for (Node *item = prog; item; item = item->next) {
        if (item->type == NODE_EXTERN_DECL && item->ext.name &&
            strncmp(item->ext.name, "clgui_", 6) == 0)
            return 1;
    }
    return 0;
}

static const char *find_rt_file(const char *rt_path, const char *name, pid_t pid) {
    static char path[4096];
    if (rt_path) {
        snprintf(path, sizeof(path), "%s/%s", rt_path, name);
        if (access(path, F_OK) == 0) return path;
    }
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.local/bin/%s", home, name);
        if (access(path, F_OK) == 0) return path;
    }
    snprintf(path, sizeof(path), "./%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "src/runtime/%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "../src/runtime/%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "/tmp/%s", name);
    if (access(path, F_OK) == 0) return path;
    /* last resort: write embedded source to /tmp */
    snprintf(path, sizeof(path), "/tmp/clean_%d_clgui.c", pid);
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(clgui_embedded, f);
    fclose(f);
    return path;
}

int codegen_compile(Node *prog, const char *source_file, const char *output_file, Diag *diag, const char *rt_path) {
    (void)source_file;
    int gui = has_gui_externs(prog);
    char asm_path[1024], obj_path[1024];
    snprintf(asm_path, sizeof(asm_path), "%s.s", output_file);
    snprintf(obj_path, sizeof(obj_path), "%s.o", output_file);

    Codegen cg;
    cg.out = fopen(asm_path, "w");
    if (!cg.out) { perror("asm"); return 1; }
    cg.label_seq = 0;
    cg.diag = diag;
    cg.str_cnt = 0;
    cg.loop_sp = 0;
    cg.struct_count = 0;
    cg.gui_mode = gui;

    emit(&cg, ".intel_syntax noprefix\n");

    if (gui) {
        emit(&cg, ".section .rodata\n");
        emit(&cg, ".section .text\n");
    }

    if (!gui) {
        emit(&cg, ".globl _start\n.type _start, @function\n_start:\n");
        emit(&cg, "  call main\n  mov rdi, rax\n  mov rax, 60\n  syscall\n");
        emit_print_int(&cg);
        emit_sleep(&cg);
        emit_read_int(&cg);
        emit_print_str(&cg);
        emit_calc_expr(&cg);
        emit_time_ms(&cg);
        emit_input(&cg);
        emit_strlen(&cg);
        emit_inspect(&cg);
        emit_assert(&cg);
        emit_clear_screen(&cg);
        emit_reset_attr(&cg);
        emit_set_fg(&cg);
        emit_set_bg(&cg);
        emit_hide_cursor(&cg);
        emit_show_cursor(&cg);
        emit_get_frame_ptr(&cg);
    }

    collect_structs(&cg, prog);
    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_FN_DECL) gen_fn(&cg, item);
        else if (item->type == NODE_EXTERN_DECL) emit(&cg, ".globl %s\n", item->ext.name);
    }

    emit_strtab(&cg);
    /* ANSI escape code data for terminal colors */
    emit(&cg, ".L_ansi_cls: .byte 0x1b, '[', '2', 'J', 0x1b, '[', 'H'\n");
    emit(&cg, ".L_ansi_reset: .byte 0x1b, '[', '0', 'm'\n");
    emit(&cg, ".L_ansi_fg: .byte 0x1b, '[', '3', '8', ';', '5', ';'\n");
    emit(&cg, ".L_ansi_bg: .byte 0x1b, '[', '4', '8', ';', '5', ';'\n");
    emit(&cg, ".L_ansi_m: .byte 'm'\n");
    emit(&cg, ".L_ansi_hide: .byte 0x1b, '[', '?', '2', '5', 'l'\n");
    emit(&cg, ".L_ansi_show: .byte 0x1b, '[', '?', '2', '5', 'h'\n");
    emit(&cg, "\n.section .bss\n.align 8\ninput_buf:\n  .space 1024\n");

    fclose(cg.out);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "as -o %s %s", obj_path, asm_path);
    if (system(cmd) != 0) {
        diag_add(diag, 3000, SEV_ERROR, 0, 0, 0, "assembly failed");
        unlink(asm_path); return 1;
    }

    if (gui) {
        const char *clgui_path = find_rt_file(rt_path, "clgui.c", getpid());
        if (!clgui_path) {
            diag_add(diag, 3002, SEV_ERROR, 0, 0, 0, "clgui.c not found — cannot build GUI program");
            unlink(asm_path); unlink(obj_path); return 1;
        }
        char gui_obj[1024];
        snprintf(gui_obj, sizeof(gui_obj), "%s_clgui.o", output_file);
        snprintf(cmd, sizeof(cmd), "cc -c -o %s %s", gui_obj, clgui_path);
        if (system(cmd) != 0) {
            diag_add(diag, 3003, SEV_ERROR, 0, 0, 0, "clgui.c compilation failed");
            unlink(asm_path); unlink(obj_path); unlink(gui_obj); return 1;
        }
        snprintf(cmd, sizeof(cmd), "cc -o %s %s %s -lX11 -lm", output_file, obj_path, gui_obj);
        if (system(cmd) != 0) {
            diag_add(diag, 3001, SEV_ERROR, 0, 0, 0, "linking failed (X11 required)");
            unlink(asm_path); unlink(obj_path); unlink(gui_obj); return 1;
        }
        unlink(gui_obj);
    } else {
        snprintf(cmd, sizeof(cmd), "ld -o %s %s", output_file, obj_path);
        if (system(cmd) != 0) {
            diag_add(diag, 3001, SEV_ERROR, 0, 0, 0, "linking failed");
            unlink(asm_path); unlink(obj_path); return 1;
        }
    }

    unlink(asm_path);
    unlink(obj_path);
    return 0;
}

int codegen_run(Node *prog, const char *source_file, Diag *diag, const char *rt_path, int prog_argc, char **prog_argv) {
    char output[1024];
    snprintf(output, sizeof(output), "/tmp/clean_%d", getpid());
    if (codegen_compile(prog, source_file, output, diag, rt_path) != 0) return 1;
    chmod(output, 0755);

    /* Build argv for child: [output, ...prog_argv[0..prog_argc-1]..., NULL] */
    int total = 1 + prog_argc + 1;
    char **args = (char **)malloc(total * sizeof(char *));
    if (!args) return 1;
    args[0] = output;
    for (int i = 0; i < prog_argc; i++) args[1 + i] = prog_argv[i];
    args[total - 1] = NULL;

    int gui = has_gui_externs(prog);
    int status;
    pid_t pid = fork();
    if (pid == 0) {
        if (gui) {
            pid_t pid2 = fork();
            if (pid2 == 0) {
                execvp(output, args);
                _exit(127);
            }
            _exit(0);
        } else {
            execvp(output, args);
            _exit(127);
        }
    }
    free(args);
    if (pid > 0) waitpid(pid, &status, 0);
    if (!gui) {
        unlink(output);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    usleep(50000);
    unlink(output);
    return 0;
}
