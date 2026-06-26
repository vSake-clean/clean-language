# Clean Language — Agent Memory

Documentation hub: **README.md** (GitHub overview), **docs/TUTORIAL.md** (in-depth language guide), **docs/SPECIFICATION.md** (formal spec).

## Architecture
- Native x86-64 compilation (no VM, no bytecode, no encryption)
- Pipeline: source → lexer → parser → AST → ownership check → x86-64 asm → `as` + `ld` → ELF binary
- GUI programs: detected via `clgui_*` externs → `cc -o` (no `-nostartfiles`, includes C runtime) + `-lX11`
- `clean run x.cl` = compile + execute; GUI programs detach (double-fork), return immediately
- `clean build x.cl output` = produce ELF
- `clean` and `cl` both work as commands

## Build & Test
- Build: `make` (produces `clean.bin`)
- Install: `make install` (copies to `~/.local/bin/clean`, symlinks `cl`, installs clgui.c)
- Quick test after build: `cp clean.bin /tmp/clean && clean run test.cl`
- After install, run `hash -r` or start a new shell (otherwise PATH cache points to old binary)
- Tests: `clean run examples/features.cl` or test files in `~/Dokumenty/1 - 1mil/Count-to-1-Billion/`
- GUI test: `clean run examples/calc.cl` (launches calculator, returns immediately)
- Clean syntax uses indentation (no braces), `fn`, `let`, `var`, `while`, `for`, `if`/`elif`/`else`/`unless`, `return`, `|>` pipe, `use`, list comprehensions `[expr for var in start..end if cond]`, `effect` in function signatures, `inspect`, `assert`, `extern` (top-level only)

## Key Files
- `README.md` — GitHub-facing readme with quickstart, benchmarks, and links
- `docs/TUTORIAL.md` — comprehensive language tutorial (27 sections, from basics to advanced patterns)
- `docs/SPECIFICATION.md` — formal language specification (EBNF grammar, type system, architecture)
- `AGENTS.md` — this file: agent memory, build notes, critical fixes, known limitations
- `src/main.c` — CLI entry, pipeline orchestration, `get_rt_path()` for runtime file location
- `src/diag.h` / `src/diag.c` — diagnostic system (Rust-style error codes, Python-style output)
- `src/ast.h` / `src/ast.c` — AST node definitions + `node_free`
- `src/parser/lexer.h` / `src/parser/lexer.c` — tokenizer with indentation (INDENT/DEDENT), `tok.len`, escape sequences in strings (`\n`→newline, `\r`, `\t`, `\\`, `\"`)
- `src/parser/parser.h` / `src/parser/parser.c` — recursive descent parser
- `src/check.h` / `src/check.c` — ownership checker (Alive/Moved state, use-after-move E1001)
- `src/codegen/codegen.h` / `src/codegen/codegen.c` — x86-64 asm emitter, GUI detection, `cc -lX11` linking, `find_rt_file()` with embedded source fallback, `print_int` + `sleep` in clgui.c for GUI
- `src/runtime/clgui.c` — Xlib wrapper (blocking poll-based event loop, fill/draw_str/draw_int/color, `print_int`, `sleep`)
- `src/runtime/clgui_embed.h` — embedded clgui.c as C string (fallback when clgui.c not on disk)
- `src/check.c` — ownership checker with Alive/Moved state tracking
- `lib/prelude.cl` — standard library (Option, Result enums, traits, stubs)

## Codegen Opcodes (binary ops)
```
0=+, 1=-, 2=*, 3=/, 4=%, 5==, 6!=, 7=<, 8=<=, 9=>, 10=>=, 11=and, 12=or
13=|, 14=^, 15=&, 16=<<, 17=>>, 18=**
```
Unary opcodes: 0=not, 1=neg
Float ops: SSE `addsd`/`subsd`/`mulsd`/`divsd` when either operand is NODE_FLOAT (ops 0-3 only)

## Built-in Functions
- `print_int(i64)` — prints integer (non-GUI: assembly write syscall; GUI: C function in clgui.c)
- `sleep(i64)` — sleeps N seconds (non-GUI: nanosleep syscall; GUI: C function in clgui.c)
- `read_int()` — reads integer from stdin (byte-by-byte, supports negative numbers; uses `r8`/`r9` to avoid `syscall` clobbering `rcx`)
- `print_str(str, len)` — prints string without newline (write syscall)
- `input(prompt)` — prints prompt string, reads one line from stdin into static 1024-byte buffer, returns pointer (null-terminated). Static buffer: subsequent calls overwrite previous input. Uses `r12` for loop index (preserved by `syscall`) and `rbx` for buffer address.
- `strlen(str)` — returns length of null-terminated string (uses `repne scasb`)
- `time_ms()` — returns current time in milliseconds (clock_gettime syscall)
- `calc_expr()` — reads arithmetic expression from stdin, evaluates with precedence, returns result
- `inspect(x)` — prints "inspect: N\n" to stdout, returns x (assembly write syscall)
- `assert(x)` — aborts with "assertion failed\n" if x == 0 (assembly write + exit syscall)
- `clear_screen()` — clears terminal (emits `\033[2J\033[H` via write syscall)
- `reset_attr()` — resets terminal attributes (emits `\033[0m`)
- `set_fg(i64)` — sets 256-color foreground (emits `\033[38;5;Nm`, N = 0-255)
- `set_bg(i64)` — sets 256-color background (emits `\033[48;5;Nm`, N = 0-255)
- `hide_cursor()` — hides terminal cursor (emits `\033[?25l`)
- `show_cursor()` — shows terminal cursor (emits `\033[?25h`)
- `get_frame_ptr(data, idx, fsize)` — returns pointer to frame `idx` in frame data buffer: `data + idx * fsize` (assembly mul + add, non-GUI only)

## Critical Fixes (2024-06-15)
1. **Arg swap bug**: Codegen pushed args in reverse order but popped into registers backward (rsi got first arg, rdi got second). Fixed by changing pop loop to forward order.
2. **GUI linking**: Changed from `cc -nostartfiles` to `cc` (no flag) — `-nostartfiles` skips C runtime init, causing SIGSEGV in X11. Also skip emitting `_start`/`print_int`/`sleep` in assembly for GUI (provided by clgui.c).
3. **clean run detach**: GUI programs now use double-fork so `waitpid` returns immediately. 50ms delay before `unlink` to ensure grandchild starts before binary is deleted.
4. **Event loop**: Replaced busy-wait `XPending` poll with efficient `poll()` on ConnectionNumber fd.
5. **Stack frame under-allocation**: `gen_fn` computed stack frame from params only, before local variables were counted. Local variables (`let` inside function body) landed below RSP in the red zone, then function calls overwrote them with return addresses. Fixed by pre-counting all local variables with `count_locals()` before emitting `sub rsp`.
6. **read_int RCX clobber**: Used `rcx` as sign flag, but `syscall` clobbers `rcx` (saves RIP in RCX). Fixed by using `r8`/`r9` for accumulator (preserved by syscall) and removing sign support.
7. **`echo -e` misleading tests**: Shell `echo -e` not supported; `-e` is printed literally. Use `printf` for piping input with newlines.

## Syntax Features (2024-06-17 Addition)
- `for i in start..end` — range-based for loop (desugars to `let mut i = start; while i < end { body; i += 1 }`)
- `break` / `continue` — loop control (works in both `while` and `for` loops)
- `+=`, `-=`, `*=`, `/=` — compound assignment operators (desugar `x += y` to `x = x + y`)

## Syntax Features (2024-06-22 Addition)
- `var` — syntactic sugar for `let mut` (mutable variable binding)
- `effect` — keyword in function signature marking impure functions: `fn name(params) effect -> type`
- `unless cond` — postfix condition on any statement (negates the condition): `stmt unless cond` desugars to `if !cond: stmt`
- Postfix `if cond` — condition after statement: `stmt if cond` desugars to `if cond: stmt`
- `|>` — pipe operator (desugars to function call): `a |> f` → `f(a)`, `a |> f(b)` → `f(a, b)`
- `use x = expr: body` — scoped resource management (desugars to let + block)
- `[expr for var in start..end if cond]` — list comprehension (generates loop with print_int for each element, returns count)
- `inspect(x)` — prints "inspect: N\n" and returns x (builtin assembly, only in non-GUI mode)
- `assert(x)` — aborts with message if x == 0 (builtin assembly, only in non-GUI mode)
- `:` after `fn` signature is optional (consumed if present)
- `struct Name:\n  field1\n  field2\n` — struct declaration with fields
- `Name(arg1, arg2)` — struct literal (when Name is a known struct)
- `obj.field` — field access via `.` operator (searches all struct definitions for matching field name)

## Critical Fixes (2024-06-22 Addition)
1. **Input/strlen builtins**: Added `input(prompt)` and `strlen(str)` as built-in assembly functions. `input` uses static 1024-byte BSS buffer, `r12` for loop index (syscall-clobber-safe), `rbx` for buffer address. `strlen` uses `repne scasb`. Both emitted only in non-GUI mode.
2. **BSS section**: Added `.section .bss` with `input_buf: .space 1024` for input buffer. Placed after `.rodata` strtab.

## Syntax Features (2024-06-22 Addition, part 2)
- `struct` — data structure definition: `struct Point:\n  x\n  y\n` creates a heap-allocated Point with fields x, y
- Struct literals: `Point(10, 20)` allocates via brk syscall, stores fields at 8-byte offsets
- Field access: `p.x` uses `.` operator (parser emits NODE_INDEX with field name). Codegen searches all registered structs for a field named `x` to determine offset
- Allocation: `brk` syscall (12) — get current break, add struct size, set new break. Pointer returned in rax
- Nested structs: `outer.inner.x` works — field loads pointer, second `.` dereferences
- Struct table: parser maintains PStructDef[] for distinguishing struct literals from function calls; codegen maintains CStructDef[] with precomputed field offsets and total size

## Critical Fixes (2024-06-22 Addition, part 2)
1. **Struct field resolution**: Field access `p.x` searches ALL registered structs for a field named `x`. Ambiguous field names across different structs may resolve to the wrong offset. Workaround: use unique field names until type-checking is implemented.
2. **Frame pointer (rbx)**: Struct literal codegen uses `rbx` to hold the allocated pointer between brk calls and field stores. Must save/restore if rbx is used as a general-purpose register in the future.
3. **struct_count uninitialized**: `Codegen cg` in `codegen_compile` is stack-allocated; `cg.struct_count` was not initialized to 0, causing out-of-bounds access in `collect_structs`/`find_cg_struct`. Fixed by adding `cg.struct_count = 0` alongside `cg.str_cnt = 0`.

## Session: Large Strings & Builtins (2024-06-22, part 3)
1. **`get_frame_ptr` builtin**: Added `emit_get_frame_ptr` assembly function — `ptr = base + idx * size` (rdi=base, rsi=idx, rdx=size, rax=ptr). Emitted as a global symbol, callable via `extern fn get_frame_ptr(data, frame, fsize)`.
2. **Dynamic string buffer**: Lexer string-reading loop replaced fixed 4096-byte `char buf[4096]` with `malloc`/`realloc` growth, supporting arbitrarily large string literals.
3. **Chunked `emit_strtab`**: Strings >4096 bytes emitted as multiple `.ascii` chunks (each ≤4096 bytes) + trailing `.byte 0`, avoiding GAS line length limits.

## Critical Fixes (2024-06-25 Addition — big-pickle session)
1. **and/or/not keywords missing from lexer**: `and`, `or`, `not` were not in the keyword table (`lexer.c:22-31`). The lexer treated them as plain identifiers, so `x and y` was parsed as `x(y)` (call to `x` with arg `y` producing `NODE_CALL`) instead of `x AND y` (binary expression). This caused the ownership checker to incorrectly move `x` in `let z = x and y` (since `let x = func()` moves the callee). Added `"and","or","not"` mapped to `TOK_AND,TOK_OR,TOK_NOT`.
2. **Deref prefix `*` missing from parser**: `*expr` (dereference) was not handled as a prefix operator in `parse_expr_prec`. TOK_STAR was only handled as binary multiplication. Added `case TOK_STAR:` to prefix section creating `NODE_DEREF` node.
3. **Debug prints removed**: All `fprintf(stderr, "DEBUG: ...")` lines removed from `check.c` before final commit.
4. **Float codegen: double→int truncation + -nostartfiles kills printf**: NODE_FLOAT cast `double` to `(long long)`, losing fractional part. `print_float` existed but used `printf` while binary was linked with `-nostartfiles` (no C runtime init → printf's stdout buffer uninitialized → silent no-op). Fixed by: (a) emitting ieee754 double bits into rax via `mov rax, 0xHEX`, (b) `print_float` reads rdi (1st arg) via `movq xmm0, rdi` then calls printf, (c) removed `-nostartfiles` and custom `_start`, letting libc init stdio properly.
5. **Float literal greedily consumed `1.` in `1..5`**: Added `isdigit(next_char)` guard before float parsing in lexer. Also restructured float lexing to correctly return TOK_INT when `.` is not followed by digit/e/E.
6. **Struct `Foo: bar` one-line fields silent drop**: Parser only checked for fields after NEWLINE+INDENT; `struct Foo: bar` parsed with 0 fields. Fixed by adding `else` branch in struct parsing for same-line identifiers.
7. **Prelude loading missing**: `main.c` never read `lib/prelude.cl`. Added prelude loading: prepends prelude source before user source. Tries CWD then binary-relative paths.
8. **Struct/enum name stealing from function calls**: `Foo(x)` always treated as struct/enum literal when `Foo` was a known struct/enum, even if user wanted function call. Fixed by requiring PascalCase (first char uppercase) for struct/enum literal detection.
9. **Float SSE arithmetic**: Added SSE float ops (`addsd`/`subsd`/`mulsd`/`divsd`) when either operand of a binary op is NODE_FLOAT. Float results returned in rax via `movq rax, xmm0`.

## Critical Fixes (2024-06-25 Addition — part 2: match + float + misc)
1. **Match arm index vs variant tag**: Match codegen compared `cmp rax, arm_idx` (0-indexed arm position) instead of looking up the actual enum variant tag. Caused wrong arm to match when arm order differed from variant declaration order (e.g., `Option` with `None` first, match with `Some` first matched `None`). Fixed by adding `find_enum_variant_tag()` which searches all enums for the variant name and returns its true tag index. Returns -1 if not found (triggers diagnostic).
2. **Float variable tracking**: `is_float` only checked `NODE_FLOAT` literals, ignoring variables containing floats. Added `expr_is_float()` type inference function and `is_float[MAX_VARS]` to `SymTab`. `let x = 3.14` marks `x` as float; `x + y` uses SSE ops.
3. **Signed integer division/modulo**: `div` is unsigned — gave huge results for negative numbers. Changed to `cqo` + `idiv` for ops 3 (`/`) and 4 (`%`).
4. **print_int negative numbers**: Used `div` to extract digits, treating negative values as huge unsigned. Added neg check before loop, emits `-` prefix and negates value.
5. **`_` wildcard in match**: Dead code checked `TOK_NOT` then peeking `TOK_NOT` — never matched `_` (tokenized as `TOK_IDENT`). Fixed by handling `_` in the `TOK_IDENT` branch via `strcmp(pt.text, "_")`.
6. **set_fg/set_bg clobber rbx**: Used `rbx` (callee-saved) without save/restore. Added `push rbx` / `pop rbx` in both emit_set_fg and emit_set_bg.
7. **Comprehension end bound unallocated**: Stored end bound at `var_off + 8` without reserving the slot, risking collision with subsequent local variables. Fixed by reserving via `c->sym.stack_size += 8` and using `end_off`.
8. **indent_stack OOB**: No bounds check before incrementing `l->indent_sp` into `indent_stack[64]`. Added `if (l->indent_sp >= 63) return 0;`.
9. **calc_expr 256-byte buffer**: Stack buffer limited to 256 bytes with read bound at 255. Increased to 4096 bytes.
10. **Hardcoded ld path**: `-dynamic-linker /lib64/ld-linux-x86-64.so.2` breaks on Arch (uses `/lib/ld-linux-x86-64.so.2`). Removed — `cc` sets the path automatically.
11. **make install missing prelude**: `lib/prelude.cl` not installed. Added install rule + updated `main.c` search path to check `$PREFIX/share/clean/prelude.cl`.
12. **`~` tokenized as TOK_NOT**: Conflicts with logical NOT (`!`/`not`). Changed to `TOK_BITNOT` with codegen support (unary op 2 = `not rax`).
13. **Enum literal single payload only**: `NODE_ENUM_LITERAL` codegen only stored the first payload arg. Fixed to iterate the linked list and store each at offsets 8, 16, 24...
14. **Float power `**`**: Only integer power loop existed. Added SSE float power (loop `mulsd`) when either operand is float.
15. **Escape sequences**: Added `\0` (null), `\xHH` (hex byte), `\uHHHH` (UTF-8 encoded codepoint) to lexer string parsing.
16. **`imul` for multiplication**: Changed `mul rcx` (unsigned) to `imul rax, rcx` (signed) for integer multiply.
17. **Impl block codegen**: Methods inside `NODE_IMPL_BLOCK` are now emitted as global functions (was missing from codegen main loop). Parser now accepts `impl Type for Trait` syntax (was missing `TOK_FOR` handling).

## Documentation
- **README.md** — quickstart, comparison table, CLI reference, examples overview
- **docs/TUTORIAL.md** — 27-section tutorial: syntax, functions, structs, comprehensions, pipe, ownership, effect, GUI, builtins, terminal, performance, 20 exercises with 10 worked examples
- **docs/SPECIFICATION.md** — formal EBNF grammar, type system, memory model, compiler architecture, VM design, code protection

## Known Limitations
- `:` after `while`/`if`/`fn` is optional (no error if missing)
- Variables stay on stack (no register optimization) — 7.3s vs 3.6s C -O0 for 1B count
- `extern` only at top level (not inside functions)
- No strings (`*u8`, `usize`, etc.), no types beyond `i32`/`i64`/`str`/`bool`/float
- Struct field resolution is by name search across ALL structs (ambiguous field names may resolve incorrectly)
- Some `diag_add` calls pass `0,0,1` as location (needs token position)
- The token under the caret in error output may be misaligned if the line has tabs or Unicode
- List comprehensions always use print_int (no pure iteration yet)
- `assert` uses abort(1) — no custom assertion message
- Comprehensions only support range-based iteration (start..end), not arbitrary iterables
- Enum type parameters (`Option<T>`) parsed but monomorphization partial — payloads still 8-byte aligned (bool size not yet 1 byte in match arms)
- No channels, green threads, or async support yet
- `@memoize`, `@lazy` annotations not yet implemented
- GC: scope-based free for local heap vars, but cross-function heap transfers may still leak
- `calc_expr` builtin has ~256 lines of hand-written assembly (maintainability issue)
- No `\n` after `print_str` output (no newline appended)
- `clean run` exit code is ignored (always returns 0 to shell)

## Recent Changes (2024-06-25)
- **Type checker** (`check.c`): `infer_expr_type()` walks AST to determine ValType of any expression. `check_stmt` compares declared vs inferred types for `let`, `assign`, `return`. Error code E1004 ("type mismatch"). Currently checks annotated types only; untyped variables pass without error.
- **GC**: Replaced `brk` syscalls with `malloc`/`free` for all heap allocations (struct literals, enum literals). SymTab `is_heap[]` tracks heap-allocated variables. Scope-based free at function exit. Assignment to heap var frees old value first.
- **Monomorphization foundations**: `valtype_size()` returns type byte sizes (bool=1, others=8). `infer_node_type()` + `valtype_size()` used in enum literal allocation. Match arm payload loads still at 8 bytes for now (full type tracking needs symbol table access in codegen).
- **Parser fixes**: enum variant shorthand in `parse_expr_prec` for PascalCase names; match arm `:` separator consumed; non-PascalCase call fallthrough restored.

## Recent Changes (2024-06-26 — part 1)
- **`'a'` char literals**: added `TOK_CHAR`, lexer handles `'a'`, `'\n'`, `'\xHH'`, `'\0'` etc. Parsed as `NODE_INT`.
- **Hex/bin literals**: `0xFF` and `0b1010` support in lexer integer parsing. Returns `TOK_INT`.
- **move/ref/mut_ref keywords**: added `TOK_MOVE`, `TOK_REF`, `TOK_MUT_REF` to lexer keyword table. `ref x` → `NODE_BORROW`, `mut_ref x` → `NODE_MUT_BORROW`, `move x` → `NODE_UNARY(op=3)` no-op in codegen (marker for future borrow checker).

## Recent Changes (2024-06-26 — part 2: as, Self, unsafe, unit, lambda, bool enum)
- **`as` keyword**: `x as i64` — type cast, no-op in codegen (same register representation). Added `TOK_AS` to lexer, `PREC_ADD` precedence in parser, `NODE_UNARY(op=4)` in codegen.
- **`Self` keyword**: type reference in type position — `fn foo(x: Self)`. Added `TOK_SELF` to lexer, handled in `parse_type`.
- **`unsafe` block**: `unsafe: body` or `unsafe { }` — wraps statements, parsed as regular block. Added `TOK_UNSAFE` handling in `parse_stmt`.
- **Unit `()`**: empty parentheses as expression — evaluates to `NODE_INT(0)`. Handled in `parse_expr_prec` prefix section checking `(` followed by `)`.
- **Lambda `fn(params) body`**: `let f = fn(x) x + 1` — generates unique `.__lambda_N` function, appended to program after parsing. Single-expression body auto-wrapped in `return`. Call via ident or `(fn(x) x + 1)(41)`.
- **Bool monomorphization**: `enum Opt: Yep(bool) | Nop` — `bool` fields use 1 byte instead of 8. `CEnumDef.payload_sizes[][]` tracks per-field sizes. Enum literal allocation and match arm loading use correct byte size.
