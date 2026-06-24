# Clean — Specyfikacja języka i architektury kompilatora

**Wersja:** 0.1.0-draft  
**Cel:** język systemowy z wcięciami, statycznym typowaniem, własnym backendem codegen (bez LLVM/GCC) oraz **dwoma trybami wykonania**: natywnym AOT (z obfuskacją) i bytecode VM (szyfrowany .clb, Python-like `run`).

---

## 1. Filozofia projektowa

| Wymiar | Decyzja |
|--------|---------|
| Składnia | Indentation-based (OFFSIDE rule), brak `{}` i `;` |
| Wydajność | Zero-cost abstrakcje, monomorfizacja, brak GC |
| Bezpieczeństwo pamięci | Ownership + Borrowing (uproszczony względem Rust) |
| Typowanie | Hindley-Milner + lokalne ograniczenia (traits) |
| Kompilacja | AOT, bez VM; bezpośredni codegen x86-64 / AArch64 |

**Uproszczenie względem Rust:** brak `lifetime` annotations w kodzie użytkownika — kompilator wnioskuje regiony z CFG + liveness. Brak `async`/makr w v0.1.

---

## 2. Leksyka

### 2.1 Tokeny

```
IDENT       ::= [A-Za-z_][A-Za-z0-9_]*
INT_LIT     ::= [0-9]+ | 0x[0-9A-Fa-f]+ | 0b[01]+
FLOAT_LIT   ::= [0-9]+\.[0-9]+([eE][+-]?[0-9]+)?
STRING_LIT  ::= " ( \\. | [^"\\] )* "
CHAR_LIT    ::= ' ( \\. | [^'\\] ) '
BOOL_LIT    ::= true | false
```

### 2.2 Słowa kluczowe

```
fn let mut if elif else while for in return break continue
match struct enum trait impl use pub unsafe extern
move ref mut_ref as type where Self
```

### 2.3 Operatory (priorytet rosnący)

```
1:  or
2:  and
3:  == !=
4:  < <= > >=
5:  | 
6:  ^
7:  &
8:  << >>
9:  + -
10: * / %
11: ! - (unary)
12: ** (potęgowanie, prawostronne)
13: . ?. (field, try)
14: = += -= *= ...
```

### 2.4 Wcięcia (OFFSIDE)

- Tab = 4 spacje (normalizowane w lexerze).
- `INDENT` / `DEDENT` emitowane przy zmianie poziomu wcięcia po `NEWLINE`.
- Kontynuacja linii: wcięcie > bieżącego bloku → kontynuacja wyrażenia (jak Python).

---

## 3. Gramatyka (EBNF)

```ebnf
(* Program *)
program        ::= { top_level } EOF ;

top_level      ::= use_decl
                 | struct_decl
                 | enum_decl
                 | trait_decl
                 | impl_block
                 | function_decl ;

use_decl       ::= "use" path [ "as" IDENT ] NEWLINE ;
path           ::= IDENT { "." IDENT } ;

(* Deklaracje *)
struct_decl    ::= [ "pub" ] "struct" IDENT [ type_params ] NEWLINE INDENT
                       { field_decl }
                   DEDENT ;
field_decl     ::= IDENT ":" type_expr NEWLINE ;

enum_decl      ::= [ "pub" ] "enum" IDENT [ type_params ] NEWLINE INDENT
                       { variant_decl }
                   DEDENT ;
variant_decl   ::= IDENT [ "(" type_list ")" ] NEWLINE ;

trait_decl     ::= [ "pub" ] "trait" IDENT [ type_params ] [ trait_bounds ] NEWLINE INDENT
                       { function_sig }
                   DEDENT ;

impl_block     ::= "impl" [ type_params ] type_expr [ "for" type_expr ] NEWLINE INDENT
                       { function_decl | function_sig "..." NEWLINE }
                   DEDENT ;

function_decl  ::= [ "pub" ] "fn" IDENT [ type_params ] "(" param_list ")" 
                   [ "->" type_expr ] NEWLINE INDENT
                       block
                   DEDENT ;

function_sig   ::= "fn" IDENT [ type_params ] "(" param_list ")" [ "->" type_expr ] NEWLINE ;

param_list     ::= [ param { "," param } ] ;
param          ::= IDENT ":" type_expr [ "=" expr ] ;

type_params    ::= "<" IDENT { "," IDENT } ">" ;
trait_bounds   ::= ":" bound { "+" bound } ;
bound          ::= IDENT [ "<" type_list ">" ] ;

type_expr      ::= IDENT [ "<" type_list ">" ]
                 | "&" [ "mut" ] type_expr
                 | "*" [ "mut" ] type_expr
                 | "(" type_list ")" "->" type_expr
                 | type_expr "[" "]"
                 | "(" type_expr ")" ;

type_list      ::= [ type_expr { "," type_expr } ] ;

(* Bloki i instrukcje *)
block          ::= { statement } ;
statement      ::= let_stmt
                 | assign_stmt
                 | expr_stmt
                 | if_stmt
                 | while_stmt
                 | for_stmt
                 | match_stmt
                 | return_stmt
                 | break_stmt
                 | continue_stmt
                 | NEWLINE ;

let_stmt       ::= "let" [ "mut" ] IDENT [ ":" type_expr ] "=" expr NEWLINE ;
assign_stmt    ::= expr "=" expr NEWLINE ;
expr_stmt      ::= expr NEWLINE ;

if_stmt        ::= "if" expr NEWLINE INDENT block DEDENT { elif_clause } [ else_clause ] ;
elif_clause    ::= "elif" expr NEWLINE INDENT block DEDENT ;
else_clause    ::= "else" NEWLINE INDENT block DEDENT ;

while_stmt     ::= "while" expr NEWLINE INDENT block DEDENT ;

for_stmt       ::= "for" IDENT "in" expr NEWLINE INDENT block DEDENT ;

match_stmt     ::= "match" expr NEWLINE INDENT { match_arm } DEDENT ;
match_arm      ::= pattern [ "if" expr ] "=>" ( expr | block ) NEWLINE ;

return_stmt    ::= "return" [ expr ] NEWLINE ;
break_stmt     ::= "break" NEWLINE ;
continue_stmt  ::= "continue" NEWLINE ;

(* Wyrażenia — poziomy precedencji via climbing / Pratt *)
expr           ::= assign_expr ;
assign_expr    ::= or_expr [ assign_op assign_expr ] ;
assign_op      ::= "=" | "+=" | "-=" | "*=" | "/=" ;

or_expr        ::= and_expr { "or" and_expr } ;
and_expr       ::= cmp_expr { "and" cmp_expr } ;
cmp_expr       ::= bit_or_expr { ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) bit_or_expr } ;
bit_or_expr    ::= bit_xor_expr { "|" bit_xor_expr } ;
bit_xor_expr   ::= bit_and_expr { "^" bit_and_expr } ;
bit_and_expr   ::= shift_expr { "&" shift_expr } ;
shift_expr     ::= add_expr { ( "<<" | ">>" ) add_expr } ;
add_expr       ::= mul_expr { ( "+" | "-" ) mul_expr } ;
mul_expr       ::= unary_expr { ( "*" | "/" | "%" ) unary_expr } ;
unary_expr     ::= ( "!" | "-" | "move" | "ref" | "mut_ref" ) unary_expr
                 | postfix_expr ;
postfix_expr   ::= primary_expr { postfix_op } ;
postfix_op     ::= "." IDENT
                 | "(" arg_list ")"
                 | "[" expr "]"
                 | "?."
                 | "**" unary_expr ;

primary_expr   ::= literal
                 | IDENT
                 | "(" expr ")"
                 | struct_lit
                 | array_lit
                 | lambda ;

struct_lit     ::= IDENT "{" field_init { "," field_init } [ "," ] "}" ;
field_init     ::= IDENT ":" expr ;
array_lit      ::= "[" [ expr { "," expr } ] "]" ;
lambda         ::= "fn" "(" param_list ")" [ "->" type_expr ] block ;

pattern        ::= "_" | IDENT | literal | enum_pat | struct_pat ;
enum_pat       ::= IDENT [ "(" pattern_list ")" ] ;
struct_pat     ::= IDENT "{" field_pat { "," field_pat } "}" ;
field_pat      ::= IDENT [ ":" pattern ] ;
pattern_list   ::= pattern { "," pattern } ;

arg_list       ::= [ expr { "," expr } ] ;
literal        ::= INT_LIT | FLOAT_LIT | STRING_LIT | CHAR_LIT | BOOL_LIT ;
```

---

## 4. System typów

### 4.1 Typy podstawowe

| Typ | Rozmiar | Semantyka |
|-----|---------|-----------|
| `bool` | 1 B | wartość logiczna |
| `i8`…`i128`, `u8`…`u128` | 1…16 B | liczby całkowite |
| `f32`, `f64` | 4/8 B | IEEE-754 |
| `isize`, `usize` | arch-dependent | indeksy, rozmiary |
| `()` | 0 | unit |
| `char` | 4 B | UTF-32 codepoint |
| `str` | fat pointer | `(ptr, len)` — immut slice |
| `String` | owned | heap buffer z ownership |

### 4.2 Konstrukcje złożone

- `struct`, `enum` (tagged union, niche optimization dla `Option`-like)
- `Array[T; N]` — stały rozmiar na stosie/seg. danych
- `Slice[T]` — `&[T]` / `&mut [T]`
- `Ptr[T]` / `MutPtr[T]` — raw (tylko w `unsafe`)
- `Option[T]`, `Result[T, E]` — wbudowane enumy

### 4.3 Wnioskowanie typów (Algorithm W + extensions)

1. **Constraint generation** z AST → zestaw równań `C`.
2. **Unifikacja** (Robinson) z występowaniem `let`-polymorphism (generalizacja po zamknięciu bloku).
3. **Trait constraints** jako dodatkowe predykaty: `T: Display` → solver ograniczeń (simplistic: trait resolution po unifikacji).
4. **Monomorfizacja** przed IR lowering — każde wywołanie generyczne dostaje konkretny symbol `foo$i32`.

### 4.4 Coercje

- `&T` → `&U` gdy `T: U` (subtyping tylko przez trait objects w v0.2; v0.1: brak)
- `T` → `Option[T]` (autowrap nie; jawne `Some(x)`)
- Array decay: `Array[T;N]` → `Slice[T]` przy pożyczce

---

## 5. Model pamięci: Ownership & Borrowing (Clean)

### 5.1 Zasady (semantyka operacyjna)

1. **Każda wartość ma dokładnie jednego właściciela** (lokalna zmienna, pole struct, element kolekcji).
2. **Move** jest domyślny przy przypisaniu i przekazaniu by-value — stary binding staje się **invalid** (compile-time error przy użyciu).
3. **Borrow (`&T`)**: dowolna liczba immutable borrows **albo** jeden `&mut T`, nigdy oba jednocześnie (reguła ekskluzywności).
4. **Drop** wstawiany automatycznie na końcu zakresu (RAII). Kolejność: odwrotna do deklaracji.
5. **Brak GC** — destrukcja deterministyczna; alokacja przez `alloc` (libc `malloc` / własny allocator w przyszłości).

### 5.2 Uproszczenia dla użytkownika

| Rust | Clean |
|------|-------|
| `'a` lifetime annotations | Wnioskowane (region inference) |
| `Box<T>` jawny | `Box[T]` opcjonalny; preferuj owned return |
| Pin, Send, Sync | v0.1: tylko `Send` marker trait dla wątków |

### 5.3 Region inference (algorytm)

1. Każde pożyczenie tworzy **region variable** `R`.
2. Ograniczenia:
   - `&'r T` nie może przeżyć `T` (outlives).
   - Wynik funkcji `&'r T` → `'r ⊆ intersection żywotności argumentów.
3. Rozwiązanie: **constraint graph** + propagacja (jak Polonius uproszczony).
4. Przy konflikcie: błąd z **span** wskazującym borrow i definicję ownera.

### 5.4 Layout pamięci

```
struct Foo { a: i32, b: bool }  →  offset 0: i32, offset 4: bool, padding do 8
enum Option[T] { None, Some(T) }  →  tag byte + union payload (niche: None = 0xFF...)
```

**ABI calling convention (System V x86-64):**
- Integer/pointer: `RDI, RSI, RDX, RCX, R8, R9`, reszta stack.
- Zwracanie ≤16 B w `RAX, RDX`.
- Struct return >16 B: ukryty wskaźnik `sret` w `RDI`.

### 5.5 Escape analysis

Przed alokacją na stosie vs heap:
- Jeśli referencja do lokalnej może **uciec** (return `&x`) → błąd kompilacji (bez lifetime syntax).
- W przeciwnym razie: stack allocation (LLVM-less: `sub rsp, size`).

---

## 6. Przykładowy kod

### 6.1 Hello World

```clean
fn main() -> i32
    print("Hello, Clean!")
    return 0

fn print(msg: str)
    extern fn write(fd: i32, buf: *u8, len: usize) -> isize
    let bytes = msg.as_ptr()
    let len = msg.len()
    let _ = write(1, bytes, len)
```

### 6.2 QuickSort (in-place, owned slice)

```clean
fn quicksort(data: &mut [i32])
    if data.len() <= 1
        return
    let pivot_idx = partition(data)
    let (left, right) = data.split_at_mut(pivot_idx)
    quicksort(left)
    quicksort(right)

fn partition(data: &mut [i32]) -> usize
    let len = data.len()
    let pivot = data[len - 1]
    let mut i = 0
    let mut j = 0
    while j < len - 1
        if data[j] <= pivot
            data.swap(i, j)
            i += 1
        j += 1
    data.swap(i, len - 1)
    return i

fn main() -> i32
    let mut arr = [5, 3, 8, 1, 2, 9, 4]
    quicksort(&mut arr)
    for x in arr
        print_int(x)
    return 0
```

---

## 7. Architektura kompilatora

### 7.1 Pipeline

```
┌────────┐   ┌────────┐   ┌─────────┐   ┌──────────┐   ┌─────────┐   ┌──────────┐
│ Source │──▶│ Lexer  │──▶│ Parser  │──▶│ Name     │──▶│ Typeck  │──▶│ MIR     │
│ .cl    │   │        │   │ (AST)   │   │ Resolve  │   │ + Borrow│   │ Builder │
└────────┘   └────────┘   └─────────┘   └──────────┘   └─────────┘   └────┬─────┘
                                                                          │
                    ┌──────────┐   ┌──────────┐   ┌──────────┐          │
                    │ Object   │◀──│ Asm Emit │◀──│ RegAlloc │◀─────────┤
                    │ .o       │   │ (x64/A64)│   │ + Opt    │   ┌──────▼─────┐
                    └──────────┘   └──────────┘   └──────────┘   │ MIR → LIR  │
                                                                   │ (lowering) │
                                                                   └────────────┘
```

### 7.2 Fazy szczegółowo

#### Lexer (`lexer.c`)
- DFA dla tokenów + OFFSIDE stack.
- `Span { file_id, start, end }` na każdym tokenie.
- Obsługa UTF-8 (IDENT unicode w v0.2).

#### Parser (`parser.c`)
- **Recursive descent** + **Pratt** dla wyrażeń.
- Error recovery: synchronizacja na `DEDENT` / słowa kluczowe top-level.

#### AST (`ast.h`)
- Arena allocator (`bump`) — całe drzewo na plik.
- Węzły: `Expr`, `Stmt`, `Item`, `Pat` z `NodeId`.

#### Name resolution (`resolve.c`)
- Budowa symbol table (scope stack).
- Import resolution (`use`).

#### Type checking (`typeck.c`)
- Constraint gen + unification.
- Ownership check: **moves** w MIR prepass.
- Borrow check: **Non-Lexical Lifetimes** (NLL) na MIR.

#### MIR (Mid-level IR)
- SSA, basic blocks, terminatory: `Goto`, `Switch`, `Return`, `Drop`.
- Instrukcje: `Assign`, `Call`, `Load`, `Store`, `Borrow`, `Move`.
- Ułatwia borrow check i optymalizacje.

#### LIR (Low-level IR)
- Instrukcje zbliżone do maszyny: `Mov`, `Add`, `Sub`, `Cmp`, `Jcc`, `Call`, `Load`, `Store`.
- Operandy: `Reg`, `StackSlot`, `Imm`, `Label`.
- Explicit frame pointer opcjonalny (debug).

#### Code generation (`codegen/`)
- **Target trait**: `emit_lir(bb) -> AsmInst[]`.
- x86-64: rejestry `rax`–`r15`, System V ABI.
- AArch64: `x0`–`x28`, AAPCS64.

#### Linker
- v0.1: wywołanie systemowego `ld` / `clang` tylko jako **linker** (nie codegen).
- v0.2: własny minimalny linker ELF/Mach-O.

---

## 8. Własny generator kodu (bez LLVM)

### 8.1 Instrukcje LIR → maszyna (x86-64 przykład)

| LIR | x86-64 |
|-----|--------|
| `mov r1, r2` | `mov %r2, %r1` |
| `add r1, imm` | `add $imm, %r1` |
| `cmp r1, r2` | `cmp %r2, %r1` |
| `jcc eq, L` | `je L` |
| `call sym` | `call sym@plt` |
| `load r1, [r2+off]` | `mov off(%r2), %r1` |

**Pełny backend** wymaga:
- Encoder instrukcji (tabele opcode/ModRM/SIB/REX).
- Relokacje (RIP-relative, GOT).
- CFA info dla debug (opcjonalnie).

### 8.2 Register allocation

**Algorytm: Linear Scan** (szybki, wystarczający dla v0.1) z **graph coloring fallback** dla hot paths.

#### Linear Scan (Poletto & Sarkar)

1. Zbierz **live intervals** `[start, end)` per virtual register z LIR (po SSA).
2. Sortuj po `start`.
3. Aktywny zbiór `A` rejestrów fizycznych; dla każdego vreg:
   - Expire intervals kończące się przed `start`.
   - Jeśli wolny rejestr → przypisz.
   - Else **spill** na stack slot (najniższy priorytet: najkrótszy interval / najrzadziej używany).
4. **Splitting** przy długich intervalach (opcjonalnie).

#### Caller-saved vs callee-saved

- x86-64 callee-saved: `RBX, RBP, R12–R15`.
- Generator prologu/epilogu funkcji zachowuje zgodność z ABI.

#### Przypisanie argumentów

- Mapowanie formals → `RDI, RSI, ...` zgodnie z ABI.
- Reszta → `[rbp+16+8*i]`.

### 8.3 Optymalizacje (MIR/LIR)

- Constant folding, DCE, mem2reg (promocja alloca → SSA).
- Inline heurystyka (rozmiar < 50 MIR instr).
- Brak pełnego LICM w v0.1.

---

## 9. Struktura projektu kompilatora

```
clean/
├── README.md
├── docs/
│   └── SPECIFICATION.md
├── Makefile
├── src/
│   ├── main.c              # CLI entry
│   ├── driver.c            # orchestracja faz
│   ├── diag.c              # diagnostyka błędów
│   ├── arena.c             # bump allocator
│   ├── lexer/
│   │   ├── lexer.c
│   │   └── unicode.c
│   ├── parser/
│   │   ├── parser.c
│   │   └── expr.c          # Pratt
│   ├── ast/
│   │   └── ast.c
│   ├── resolve/
│   │   └── resolve.c
│   ├── typeck/
│   │   ├── infer.c
│   │   ├── ownership.c
│   │   └── borrowck.c
│   ├── mir/
│   │   ├── build.c
│   │   └── opt.c
│   ├── lir/
│   │   └── lower.c
│   ├── codegen/
│   │   ├── regalloc.c
│   │   ├── emit_x64.c
│   │   └── emit_a64.c
│   └── link/
│       └── elf.c
├── include/
│   └── clean/*.h
├── runtime/
│   ├── start.c             # _start, argc/argv
│   └── panic.c
├── lib/
│   └── prelude.cl          # Option, Result, builtin traits
├── tests/
│   ├── lexer/
│   ├── parser/
│   └── end2end/
└── tools/
    └── clean-gui/            # osobny subprojekt GUI
        ├── main.c
        ├── ast_view.c
        └── cfg_view.c
```

### 9.1 Język implementacji: **C11**

**Uzasadnienie bootstrappability:**
- Kompilator Clean napisany w C kompiluje się dowolnym kompilatorem C (gcc, tcc, zig cc).
- **Bootstrap stage 0:** `gcc -O2 -o clean src/*.c`
- **Stage 1:** `clean build src/*.c` → porównanie z stage 0 (opcjonalna autoweryfikacja).
- Brak zależności od C++ (prostszy ABI, mniejszy runtime).

Alternatywa: **Zig** jako język hosta (lepsze std, cross-compile) — nadal bootstrappable przez stage-0 C.

### 9.2 CLI

```bash
clean build hello.cl -o hello          # kompilacja + link (AOT)
clean build --obfuscate hello.cl -o hello  # AOT + obfuskacja kodu
clean check hello.cl                   # typecheck only
clean run hello.cl                     # uruchom przez bytecode VM (Python-like)
clean run --rebuild hello.cl           # wymuś rekompilację bytecode
clean cache clear                      # wyczyść ~/.cache/clean/
clean mir hello.cl --emit mir          # dump MIR
clean ast hello.cl --emit ast          # dump AST (JSON)
```

---

## 10. GUI kompilatora (koncepcja)

### 10.1 Cele

- **AST live view** podczas edycji (debounce 300 ms → incremental reparse).
- **CFG overlay** z zaznaczonymi blokami zawierającymi błędy borrow/type.
- Minimalny natywny UI — bez Electron.

### 10.2 Architektura

```
┌─────────────────────────────────────────────────────────┐
│  Editor pane (native text control)                      │
├──────────────────────┬──────────────────────────────────┤
│  AST TreeView        │  CFG Canvas (custom draw)        │
│  (expand/collapse)   │  basic blocks as nodes           │
│                      │  edges: branches                 │
│                      │  error nodes: red border         │
└──────────────────────┴──────────────────────────────────┘
│  Diagnostics panel (list, click → jump to span)         │
└─────────────────────────────────────────────────────────┘
```

### 10.3 Backend komunikacji

- Kompilator jako **biblioteka** (`libclean.so` / `clean.dll`) z API:

```c
typedef struct {
    char *json_ast;      // incremental subtree
    char *json_cfg;      // per-function CFG
    Diagnostic *diags;
    size_t diag_count;
} CleanAnalysis;

CleanAnalysis clean_analyze_buffer(const char *source, CleanOpts opts);
void clean_analysis_free(CleanAnalysis *);
```

- GUI wywołuje `clean_analyze_buffer` w wątku roboczym; UI thread dostaje wynik przez kolejkę.

### 10.4 Implementacja per platforma

| Platforma | API | Widgety |
|-----------|-----|---------|
| Windows | Win32 + Direct2D | `EDIT`, custom `WM_PAINT` dla CFG |
| macOS | Cocoa/AppKit | `NSTextView`, `NSOutlineView`, `CAShapeLayer` |
| Linux | GTK4 lub raw X11 | `GtkSourceView`, `GtkTreeView`, Cairo |

**Linux v0.1:** GTK4 — szybszy development; opcjonalny port Win32/Cocoa.

### 10.5 CFG z błędami

1. Po `borrowck` każdy `Diagnostic` ma `NodeId` / `BasicBlockId`.
2. JSON CFG:

```json
{
  "fn": "quicksort",
  "blocks": [
    { "id": 0, "preds": [], "succs": [1,2], "errors": [] },
    { "id": 1, "preds": [0], "succs": [], "errors": ["E0382: use of moved value"] }
  ]
}
```

3. Canvas rysuje bloki; klik → podświetlenie źródła w edytorze.

### 10.6 Incremental parsing

- **Reparsing:** lexer od zmienionej linii; parser tylko dla dotkniętych `Item` (heurystyka).
- v0.1: pełny reparse pliku <10k linii — akceptowalne przy 300 ms debounce.

---

## 11. Format diagnostyki

```
error[E0382]: use of moved value `data`
  --> quicksort.cl:12:18
   |
 8 |     let (left, right) = data.split_at_mut(pivot_idx)
   |                         ---- value moved here
...
12 |     quicksort(data)
   |               ^^^^ value used after move
   |
   = note: consider borrowing: `&mut data`
```

Kody błędów: `E0xxx` type, `E1xxx` borrow, `E2xxx` syntax.

---

## 12. Roadmap implementacji

| Faza | Zakres | Szacunek |
|------|--------|----------|
| M0 | Lexer + Parser + AST dump | 2–3 tyg. |
| M1 | Type inference + MIR | 4–6 tyg. |
| M2 | Borrow check + ownership | 3–4 tyg. |
| M3 | LIR + x86-64 codegen + regalloc | 6–8 tyg. |
| M4 | Runtime + link + hello world AOT | 2 tyg. |
| **M5** | **Bytecode VM + .clb encrypt + `clean run`** | **✅ DONE** |
| M6 | Obfuscation pass (mangle, const_hide, CFG flat) | 3 tyg. |
| M7 | GUI (GTK) + lib API | 4 tyg. |
| M8 | AArch64 backend | 4 tyg. |

---

## 13. Ograniczenia v0.1 i obejścia

| Problem | Obejście |
|---------|----------|
| Pełny linker ELF | Użyj systemowego `ld` tymczasowo |
| Unicode IDENT | ASCII only w v0.1 |
| Const generics | Tylko literały stałe w typach tablicowych |
| Trait objects (dyn) | Monomorfizacja + enum dispatch (manual vtable) |
| Debug info DWARF | Emit minimalnych `.debug_line` w v0.2 |

---

## 14. Ochrona kodu źródłowego — dwa tryby wykonania

Clean oferuje **dwie ścieżki wykonania**, z których obie chronią kod źródłowy przed odczytem:

| Tryb | Komenda | Format wyjścia | Widoczność kodu | Wydajność |
|------|---------|----------------|-----------------|-----------|
| **Bytecode VM** | `clean run script.cl` | `.clb` (szyfrowany) + interpreter | **niewidoczny** — źródło nie potrzebne po pierwszym uruchomieniu | ~50% native |
| **AOT Native** | `clean build script.cl -o out` | ELF/x86-64 (stripped + obfuscated) | **niewidoczny** — wszystkie symbole zaszyfrowane, brak debug info | 100% |

Użytkownik może pracować jak z Pythonem (`clean run plik.cl`), ale nikt nie podejrzy kodu — ani `.cl`, ani pamięci procesu.

---

### 14.1 Tryb Bytecode VM (`clean run`)

#### 14.1.1 Pipeline run

```
┌──────────┐   ┌──────────┐   ┌───────────┐   ┌──────────────┐   ┌───────────┐
│ source   │──▶│ Parse +  │──▶│ Bytecode  │──▶│ Encrypt .clb │──▶│ VM        │
│ .cl      │   │ Typeck   │   │ Emitter   │   │ + Cache      │   │ Execute   │
└──────────┘   └──────────┘   └───────────┘   └──────────────┘   └───────────┘
                                    │                                       ▲
                                    ▼                                       │
                              ┌──────────┐                          ┌───────────┐
                              │ .clb cache│─────────────────────────│ Decrypt   │
                              │ ~/.cache │  (przy kolejnym run)     │ on load   │
                              └──────────┘                          └───────────┘
```

#### 14.1.2 Format pliku .clb (Clean ByteCode)

```
┌─────────────────────────────────────────────┐
│ MAGIC: 0x434C45414E ("CLEAN") [4 B]         │
│ VERSION: u16                                 │
│ FLAGS: u16  (bit 0: encrypted)              │
│ KEY_MATERIAL: u64 (XOR seed, XOR z master)  │
│ CONST_POOL_OFFSET: u32                      │
│ CODE_OFFSET: u32                            │
│ ENTRY_POINT: u32 (BC offset main)           │
│ SOURCE_HASH: [32 B] (SHA-256 .cl)           │
├─────────────────────────────────────────────┤
│ CONSTANT POOL (sequence of entries):        │
│   TAG: u8 (0=int, 1=float, 2=str, 3=ident) │
│   LEN: uleb128                              │
│   DATA: [LEN B]                             │
├─────────────────────────────────────────────┤
│ INSTRUCTION STREAM (bytecode opcodes):      │
│   ... (patrz 14.1.3)                        │
└─────────────────────────────────────────────┘
```

#### 14.1.3 Zestaw instrukcji VM (stack-based)

| Opcode | Hex | Stack (przed → po) | Opis |
|--------|-----|---------------------|------|
| `NOP` | 0x00 | — | no-op |
| `PUSH_CONST` | 0x01 | → val | push constant z pool (indeks uleb128 za opcode) |
| `PUSH_LOCAL` | 0x02 | → val | push local variable (indeks uleb128) |
| `STORE_LOCAL` | 0x03 | val → | pop to local |
| `PUSH_GLOBAL` | 0x04 | → val | push global/static |
| `STORE_GLOBAL`| 0x05 | val → | pop to global |
| `ADD` | 0x10 | a b → a+b | |
| `SUB` | 0x11 | a b → a-b | |
| `MUL` | 0x12 | a b → a*b | |
| `DIV` | 0x13 | a b → a/b | |
| `MOD` | 0x14 | a b → a%b | |
| `NEG` | 0x15 | a → -a | |
| `NOT` | 0x16 | a → !a | |
| `EQ` | 0x20 | a b → a==b | |
| `NE` | 0x21 | a b → a!=b | |
| `LT` | 0x22 | a b → a<b | |
| `LE` | 0x23 | a b → a<=b | |
| `GT` | 0x24 | a b → a>b | |
| `GE` | 0x25 | a b → a>=b | |
| `AND` | 0x26 | a b → a&&b | |
| `OR` | 0x27 | a b → a\|\|b | |
| `JMP` | 0x30 | → | unconditional jump (offset i32 za opcode, relatywny) |
| `JZ` | 0x31 | cond → | jump if zero |
| `JNZ` | 0x32 | cond → | jump if non-zero |
| `CALL` | 0x40 | args... → result | call function (uleb128: nargs; uleb128: func_idx w const pool) |
| `RET` | 0x41 | val → | return from function |
| `MAKE_STRUCT` | 0x50 | fields... → struct | uleb128 nfields |
| `MAKE_ARRAY` | 0x51 | elems... → array | uleb128 nelems |
| `GET_FIELD` | 0x52 | struct → val | uleb128 field_idx |
| `SET_FIELD` | 0x53 | struct val → struct | uleb128 field_idx |
| `BORROW` | 0x60 | val → &val | create immutable reference |
| `MUT_BORROW` | 0x61 | val → &mut val | create mutable reference |
| `MOVE` | 0x62 | val → val | ownership transfer (invalidate source) |
| `DROP` | 0x63 | val → | destructor call |
| `EXTERN_CALL` | 0x70 | args... → result | uleb128: sym_idx w pool; uleb128: nargs |
| `HALT` | 0xFF | — | stop execution |

#### 14.1.4 Szyfrowanie .clb

**Cel:** uniemożliwienie odczytu bytecode'u z dysku.

**Algorytm:** XOR strumieniowy z 64-bitowym kluczem:

```
master_key = 0xA5B6C7D8E9FA0B1C  (stała w binary kompilatora)
file_key   = KEY_MATERIAL ^ master_key
XOR każdego bajtu .clb od CONST_POOL_OFFSET do końca:
    encrypted[i] = plain[i] ^ (file_key + i) & 0xFF
```

Gdzie `KEY_MATERIAL = SHA-256(source)[0..7] ^ timestamp` — unikalny klucz per plik.

**Dekrypcja:** VM przy starcie odczytuje nagłówek, wyciąga `KEY_MATERIAL`, rekonstruuje `file_key`, deszyfruje resztę w locie przed wykonaniem.

**Cache:** Szyfrowany `.clb` zapisywany jest do `~/.cache/clean/<hash>.clb`. Komenda `clean run` sprawdza najpierw cache — jeśli `.cl` jest nowszy, rekompiluje.

#### 14.1.5 VM Interpreter

**Implementacja:** prosty interpreter w C (w `src/vm/`), główna pętla:

```c
Value vm_execute(BCProgram *prog) {
    Value stack[STACK_SIZE];
    Value *sp = stack;
    u8 *ip = prog->code;
    Value locals[MAX_LOCALS];

    for (;;) {
        u8 op = *ip++;
        switch (op) {
        case OP_PUSH_CONST: {
            u32 idx = read_uleb128(&ip);
            PUSH(prog->const_pool[idx]);
            break;
        }
        case OP_ADD: {
            Value b = POP(); Value a = POP();
            PUSH(INT_VAL(a.as_i64 + b.as_i64));
            break;
        }
        case OP_JZ: {
            i32 offset = read_i32(&ip);
            if (!POP().as_bool) ip += offset;
            break;
        }
        // ... pozostałe opcode'y
        case OP_HALT: return POP();
        }
    }
}
```

**Bezpieczeństwo:** brak `eval`, brak dostępu do REPL, brak `dis` — użytkownik nie ma żadnego narzędzia do podejrzenia bytecode'u.

---

### 14.2 Tryb AOT Native z obfuskacją (`clean build --obfuscate`)

Standardowa kompilacja do natywnego ELF-a z dodatkową fazą ochrony kodu.

#### 14.2.1 Fazy ochrony

```
MIR/LIR ──▶ Obfuscation Pass ──▶ Codegen ──▶ Strip ──▶ ELF
                │
                ├── Symbol Mangling:  fn main → _Z6L3kP9mQ (hash + random seed)
                ├── Constant Hiding:  string "hello" → XOR z key i decode w _start
                ├── Control Flow Flattening (opcjonalne)
                └── Dead Code Insertion: dummy basic blocks z never-taken jumps
```

#### 14.2.2 Symbol mangling (bez demanglera)

- Każdy symbol dostaje nazwę: `_Z<len><random_hash>` np. `main` → `_Z4A1b2C3d4`
- Mapa `original → mangled` przechowywana tylko podczas linkowania, potem usuwana
- Brak `.symtab` / `.strtab` (strip --strip-all)

#### 14.2.3 Constant hiding

Stałe tekstowe (stringi) przed emisją do sekcji `.rodata`:
1. Są XORowane z losowym 1-bajtowym kluczem (per plik)
2. Klucz zapisany na końcu sekcji
3. Przy pierwszym użyciu generowany jest stub dekodujący w _start

Bez debuggera + znajomości algorytmu — stringi są nieczytelne w binary.

#### 14.2.4 Flagi CLI

```bash
clean build script.cl -o out              # normalny build (strip domyślnie)
clean build --obfuscate script.cl -o out  # full obfuscation (M2)
clean build -g script.cl                  # debug mode — symbole widoczne
clean run script.cl                       # bytecode run (szyfrowany .clb)
clean run --rebuild script.cl             # wymuś rekompilację bytecode
clean cache clear                         # wyczyść ~/.cache/clean/
```

#### 14.2.5 Podsumowanie ochrony

| Zagrożenie | AOT (--obfuscate) | Bytecode run |
|------------|-------------------|--------------|
| Odczyt z dysku | ELF stripped + XOR strings | .clb encrypted + cache |
| Inżynieria wsteczna | Symbole zaszyfrowane, CFG splątany | Brak narzędzi disassemble |
| Odczyt pamięci procesu | Runtime ma te same ograniczenia co C | Dodatkowa warstwa XOR na stałych |
| Decompilacja | Trudna (brak debug info, flattened CFG) | Łatwiejsza ale .clb i tak szyfrowany |

---

## 15. Struktura projektu — uzupełnienie o VM i ochronę

```
clean/
├── ...
├── src/
│   ├── ...
│   ├── vm/                     ← NOWE: bytecode VM
│   │   ├── vm.c                ← główna pętla interpretera
│   │   ├── vm_ops.c            ← implementacje opcode'ów
│   │   ├── bc_format.h         ← struktury .clb
│   │   ├── decrypt.c           ← XOR decrypt
│   │   └── loader.c            ← wczytywanie i cache .clb
│   └── obfus/                  ← NOWE: ochrona kodu
│       ├── mangle.c            ← symbol mangling
│       ├── const_hide.c        ← XOR string constants
│       └── cfg_flat.c          ← control flow flattening
├── runtime/
│   ├── start.c
│   ├── panic.c
│   └── decrypt_stub.S         ← NOWE: stub dekodujący stringi (AOT)
└── tools/
    └── clean-gui/
        └── ...
```

---

## 16. Referencje algorytmiczne

- Hindley-Milner: Cardelli, *Basic polymorphic typechecking*
- Linear Scan: Poletto & Sarkar, *Linear Scan Register Allocation*
- Ownership: Rust RFC 1858 (MIR borrow check)
- OFFSIDE rule: Python PEP 8 / lexical analysis literature
