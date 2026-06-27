# Clean — Specyfikacja języka i architektury

**Wersja:** 0.2.0  
**Cel:** język systemowy z wcięciami, statycznym typowaniem, własnym backendem codegen (bez LLVM/GCC) — natywna kompilacja x86-64.

---

## 1. Architektura kompilatora

### 1.1 Pipeline

```
Source (.cl) → Lexer → Parser (AST) → Ownership Check → Borrow Check → x86-64 ASM → as + ld → ELF
```

Kompilator nie używa VM, bytecode'u, LLVM ani żadnego pośredniego formatu wykonywalnego. Produktem końcowym jest natywny ELF x86-64.

### 1.2 Fazy

| Faza | Plik | Opis |
|------|------|------|
| Lexer | `lexer.c` | Tokenizacja z OFFSIDE rule (INDENT/DEDENT) |
| Parser | `parser.c` | Recursive descent + Pratt dla wyrażeń |
| Ownership | `check.c` | Sprawdzanie move semantics (Alive/Moved) |
| Borrow | `borrowck.c` | NLL scope-based loan tracking |
| MIR (opcjonalnie) | `mir_build.c`, `mir_opt.c` | Mid-level IR (częściowy, bez PHI) |
| LIR (opcjonalnie) | `lir_lower.c` | Low-level IR (częściowy) |
| Codegen | `codegen.c`, `emit_asm.c`, `regalloc.c` | x86-64 ASM emitter |
| Link | — | Wywołanie systemowego `as` + `ld` przez `cc` |

### 1.3 CLI

```bash
clean run file.cl [args...]    # kompilacja + uruchomienie
clean build file.cl output     # kompilacja do ELF
clean --help                   # pomoc
```

GUI programy (z `clgui_*` extern) linkowane z `-lX11`, uruchamiane w tle (double-fork).

---

## 2. Leksyka

### 2.1 Tokeny

```
IDENT       ::= [A-Za-z_][A-Za-z0-9_]*
INT_LIT     ::= [0-9]+ | 0x[0-9A-Fa-f]+ | 0b[01]+
FLOAT_LIT   ::= [0-9]+\.[0-9]+([eE][+-]?[0-9]+)?
STRING_LIT  ::= " ( escape | [^"\\] )* "
CHAR_LIT    ::= ' ( escape | [^'\\] ) '
escape      ::= "\\" ( "n" | "r" | "t" | "0" | "\\" | "\"" | "x" HEX HEX | "u" HEX HEX HEX HEX )
HEX         ::= [0-9A-Fa-f]
```

Obsługiwane sekwencje: `\n`, `\r`, `\t`, `\\`, `\"`, `\0`, `\xHH`, `\uHHHH`.

### 2.2 Słowa kluczowe

```
fn let mut var if elif else while for in return break continue
match struct enum trait impl use pub unsafe extern
move ref mut_ref as Self
and or not true false unless effect
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
10:  * / %
11:  not (unary), - (unary), ~ (bitwise not)
12:  ** (potęgowanie, prawostronne)
13:  . (field access)
14:  = += -= *= /= (assignment, prawostronne)
```

### 2.4 Wcięcia

- Tab = 4 spacje (normalizowane w lexerze).
- `INDENT` / `DEDENT` emitowane przy zmianie poziomu wcięcia po `NEWLINE`.
- Maksymalny poziom wcięcia: 63.

---

## 3. Gramatyka (EBNF)

```ebnf
(* Program *)
program        ::= { top_level } EOF ;

top_level      ::= struct_decl
                 | enum_decl
                 | trait_decl
                 | impl_block
                 | function_decl
                 | extern_decl ;

extern_decl    ::= [ "pub" ] "extern" "fn" IDENT "(" param_list ")" [ "->" type_expr ] NEWLINE ;

struct_decl    ::= [ "pub" ] "struct" IDENT NEWLINE INDENT { field_decl } DEDENT ;
field_decl     ::= IDENT NEWLINE ;

enum_decl      ::= [ "pub" ] "enum" IDENT [ type_params ] NEWLINE INDENT { variant_decl } DEDENT ;
variant_decl   ::= IDENT [ "(" type_list ")" ] NEWLINE ;

trait_decl     ::= [ "pub" ] "trait" IDENT NEWLINE INDENT { function_sig } DEDENT ;

impl_block     ::= "impl" IDENT [ "for" IDENT ] NEWLINE INDENT { function_decl } DEDENT ;

function_decl  ::= [ "pub" ] "fn" IDENT "(" param_list ")" [ "effect" ] [ "->" type_expr ] NEWLINE INDENT
                       block
                   DEDENT ;

function_sig   ::= "fn" IDENT "(" param_list ")" [ "->" type_expr ] NEWLINE ;

param_list     ::= [ param { "," param } ] ;
param          ::= IDENT [ ":" type_expr ] ;

type_params    ::= "<" IDENT { "," IDENT } ">" ;

type_expr      ::= IDENT [ "<" type_list ">" ]
                 | "(" type_expr ")" ;

type_list      ::= [ type_expr { "," type_expr } ] ;

(* Block and statements *)
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
                 | use_stmt
                 | unsafe_stmt
                 | NEWLINE ;

let_stmt       ::= ( "let" [ "mut" ] | "var" ) IDENT [ ":" type_expr ] "=" expr NEWLINE ;
assign_stmt    ::= expr ( "=" | "+=" | "-=" | "*=" | "/=" ) expr NEWLINE ;
expr_stmt      ::= expr NEWLINE ;

if_stmt        ::= "if" expr [ ":" ] NEWLINE INDENT block DEDENT
                   { "elif" expr [ ":" ] NEWLINE INDENT block DEDENT }
                   [ "else" [ ":" ] NEWLINE INDENT block DEDENT ] ;

while_stmt     ::= "while" expr [ ":" ] NEWLINE INDENT block DEDENT ;

for_stmt       ::= "for" IDENT "in" expr ".." expr [ ":" ] NEWLINE INDENT block DEDENT ;

match_stmt     ::= "match" expr NEWLINE INDENT { match_arm } DEDENT ;
match_arm      ::= pattern [ ":" ] NEWLINE INDENT block DEDENT ;

return_stmt    ::= "return" [ expr ] NEWLINE ;
break_stmt     ::= "break" NEWLINE ;
continue_stmt  ::= "continue" NEWLINE ;
use_stmt       ::= "use" IDENT "=" expr ":" NEWLINE INDENT block DEDENT ;
unsafe_stmt    ::= "unsafe" [ ":" ] NEWLINE INDENT block DEDENT ;

(* Expressions — Pratt precedence climbing *)
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
unary_expr     ::= ( "not" | "-" | "~" | "move" | "ref" | "mut_ref" ) unary_expr
                 | postfix_expr ;
postfix_expr   ::= primary_expr { postfix_op } ;
postfix_op     ::= "." IDENT
                 | "(" arg_list ")"
                 | "**" unary_expr
                 | "if" expr
                 | "unless" expr ;

primary_expr   ::= literal
                 | IDENT
                 | "(" expr ")"
                 | struct_lit
                 | lambda
                 | comprehension ;

struct_lit     ::= IDENT "(" arg_list ")" ;
lambda         ::= "fn" "(" param_list ")" [ "->" type_expr ] expr ;
comprehension  ::= "[" expr "for" IDENT "in" expr ".." expr [ "if" expr ] "]" ;

pattern        ::= "_" | IDENT | literal | enum_pat ;
enum_pat       ::= IDENT [ "(" [ IDENT ] ")" ] ;

arg_list       ::= [ expr { "," expr } ] ;
literal        ::= INT_LIT | FLOAT_LIT | STRING_LIT | CHAR_LIT | "true" | "false" ;
(* INT_LIT: decimal [0-9]+, hex 0x[0-9A-Fa-f]+, binary 0b[01]+ — patrz §2.1 *)
```

---

## 4. System typów

### 4.1 Typy podstawowe

| Typ | Rozmiar | Opis |
|-----|---------|------|
| `i64` | 8 B | liczba całkowita (domyślny typ liczbowy) |
| `bool` | 1 B | `true` / `false` |
| `str` | 8 B | wskaźnik do null-terminated stringu |
| `float` | 8 B | liczba zmiennoprzecinkowa (double) |

Brak typów `i8`–`i128`, `u8`–`u128`, `f32`, `f64`, `isize`, `usize`, `String`, `Array`, `Slice`.  
`char` literały (`'a'`) istnieją — są parsowane jako `NODE_INT` (wartość całkowita).  
`as i64` — type cast, no‑op w kodzie (ta sama reprezentacja rejestrowa).

### 4.2 Typy złożone

- `struct` — heap-allocated (malloc), pola dostępne przez `.`
- `enum` — tagged union, heap-allocated (malloc), tag + payload
- `Option<T>`, `Result<T, E>` — zdefiniowane w `prelude.cl`

### 4.3 Wnioskowanie typów

Kompilator ma prosty `infer_expr_type()` w `check.c` — nie jest to Hindley-Milner. Typy są wnioskowane lokalnie dla wyrażeń; deklaracje zmiennych mogą być opcjonalnie annotowane.

---

## 5. Model pamięci

### 5.1 Ownership

- Każda wartość ma dokładnie jednego właściciela.
- Przez domyślne przypisanie **move** — stary binding staje się nie ważny (E1001 przy próbie użycia).
- `let y = x` — move (x staje się invalid).
- `let y = copy(x)` — kopia (jeśli zaimplementowane).
- `move x` — jawne przeniesienie własności.

### 5.2 Borrowing

- `&x` — immutable borrow.
- `&mut x` — mutable borrow.
- Reguła: dowolna liczba `&` ALBO jeden `&mut`, nigdy oba.
- Sprawdzane przez `borrowck.c` (NLL, scope-based loans).

### 5.3 Alokacja

- `struct` i `enum` literały alokowane przez `malloc`.
- Zmienne lokalne na stosie (slot w ramce funkcji).
- Brak GC — scope-based free dla heap zmiennych przy wyjściu z funkcji.
- Przypisanie do heap zmiennej zwalnia starą wartość przed nadpisaniem.

---

## 6. Wbudowane funkcje

### 6.1 I/O

| Funkcja | Opis |
|---------|------|
| `print_int(i64)` | Wypisuje liczbę z newline |
| `print_float(float)` | Wypisuje float |
| `print_str(str, len)` | Wypisuje string bez newline |
| `read_int()` | Czyta liczbę ze stdin |
| `input(str)` | Wyświetla prompt, czyta linię (bufor 1024 B) |

### 6.2 Sterowanie

| Funkcja | Opis |
|---------|------|
| `sleep(i64)` | Usypia na N sekund |
| `time_ms()` | Zwraca czas w ms (CLOCK_MONOTONIC) |
| `calc_expr()` | Czyta i ewaluuje wyrażenie arytmetyczne |

### 6.3 Debug

| Funkcja | Opis |
|---------|------|
| `inspect(x)` | Wypisuje `inspect: N\n`, zwraca x |
| `assert(x)` | Przerywa z `assertion failed\n` jeśli x == 0 |

### 6.4 Terminal

| Funkcja | Opis |
|---------|------|
| `clear_screen()` | Czyści terminal |
| `reset_attr()` | Resetuje atrybuty |
| `set_fg(i64)` | Ustawia kolor tekstu (256) |
| `set_bg(i64)` | Ustawia kolor tła (256) |
| `hide_cursor()` | Ukrywa kursor |
| `show_cursor()` | Pokazuje kursor |

### 6.5 String/frame

| Funkcja | Opis |
|---------|------|
| `strlen(str)` | Długość stringu (repne scasb) |
| `string_clone(str, len)` | Kopiuje string przez malloc |
| `string_concat(a, b)` | Łączy dwa stringi (malloc'owany) |
| `get_frame_ptr(data, idx, fsize)` | Wskaźnik do ramki `idx` |

---

## 7. Struktura projektu

```
clean/
├── README.md
├── Makefile
├── AGENTS.md
├── lib/
│   └── prelude.cl              # Option, Result, trait stubs
├── docs/
│   ├── SPECIFICATION.md
│   └── TUTORIAL.md
├── examples/
│   ├── hello.cl
│   └── features.cl
└── src/
    ├── main.c                  # CLI entry, pipeline orchestration
    ├── ast.h / ast.c           # AST node definitions
    ├── diag.h / diag.c         # Diagnostics (Rust-style errors)
    ├── check.h / check.c       # Ownership checker
    ├── borrowck.h / borrowck.c # NLL borrow checker
    ├── parser/
    │   ├── lexer.h / lexer.c   # Tokenizer
    │   └── parser.h / parser.c # Recursive descent parser
    ├── mir/
    │   ├── mir.h               # MIR types
    │   ├── mir_build.c         # MIR builder (partial)
    │   └── mir_opt.c           # MIR optimizations (partial)
    ├── lir/
    │   ├── lir.h               # LIR types
    │   └── lir_lower.c         # LIR lowering (partial)
    ├── codegen/
    │   ├── codegen.h / codegen.c  # x86-64 codegen
    │   ├── emit_asm.c          # ASM emission helpers
    │   └── regalloc.c          # Register allocation (partial)
    └── runtime/
        ├── clgui.c             # Xlib wrapper for GUI programs
        └── clgui_embed.h       # Embedded clgui.c fallback
```

Język implementacji: **C11**. Kompilator bootstrappable — wystarczy `gcc -std=c11 -O2`.

---

## 8. Format diagnostyki

```
error[E1001]: cannot use moved value 'x'
  --> source.cl:5:10
   |
 4 |     let y = x
   |             - value moved here
 5 |     print_int(x)
   |              ^ value used after move
   |
   = note: use 'copy' to clone the value
```

Kody błędów:
- `E1xxx` — ownership/borrow errors (check.c, borrowck.c)
- `E2xxx` — syntax/parse errors (parser.c)
- `E3xxx` — codegen/assembly/link errors (codegen.c)

---

## 9. Ograniczenia

- Brak rejestrów — zmienne zawsze na stosie.
- Tylko x86-64 Linux.
- `extern` tylko na poziomie top-level.
- Typy: tylko `i64`, `bool`, `str`, `float`.
- `pub` na struct/enum/trait nie egzekwowany.
- Struct field lookup po nazwie we wszystkich structach (może być niejednoznaczny).
- Comprehensions używają `print_int` (brak czystej iteracji).
- Enum type parameters (`Option<T>`) — monomorfizacja częściowa.
- Brak async, channel, green threads.
- Brak `@memoize`, `@lazy`.
- GC: scope-based free — cross-function heap transfery mogą leakować.
