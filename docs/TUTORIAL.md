# Clean Language — Kompletny poradnik

## Spis treści

1. [Wprowadzenie](#1-wprowadzenie)
2. [Instalacja i uruchamianie](#2-instalacja-i-uruchamianie)
3. [Podstawy składni](#3-podstawy-składni)
4. [Zmienne i mutowalność](#4-zmienne-i-mutowalność)
5. [Funkcje](#5-funkcje)
6. [Instrukcje warunkowe](#6-instrukcje-warunkowe)
7. [Pętle](#7-pętle)
8. [Operatory](#8-operatory)
9. [Postfix warunki i unless](#9-postfix-warunki-i-unless)
10. [Operator pipe (|>)](#10-operator-pipe-)
11. [Stringi i tekst](#11-stringi-i-tekst)
12. [Struktury](#12-struktury)
13. [List składane (comprehensions)](#13-list-składane-comprehensions)
14. [Zarządzanie zasobami (use)](#14-zarządzanie-zasobami-use)
15. [Funkcje wbudowane — kompendium](#15-funkcje-wbudowane--kompendium)
16. [Funkcje terminalowe](#16-funkcje-terminalowe)
17. [Ownership i borrow checking](#17-ownership-i-borrow-checking)
18. [Effect — system czystości](#18-effect--system-czystości)
19. [extern — wołanie C z Cleana](#19-extern--wołanie-c-z-cleana)
20. [GUI — programy okienkowe](#20-gui--programy-okienkowe)
21. [Programy CLI i argumenty](#21-programy-cli-i-argumenty)
22. [Debugowanie z inspect i assert](#22-debugowanie-z-inspect-i-assert)
23. [Wydajność i optymalizacje](#23-wydajność-i-optymalizacje)
24. [Znane ograniczenia](#24-znane-ograniczenia)
25. [Przykłady krok po kroku](#25-przykłady-krok-po-kroku)
26. [Zadania do samodzielnego rozwiązania](#26-zadania-do-samodzielnego-rozwiązania)
27. [Szybka ściągawka](#27-szybka-ściągawka)

---

## 1. Wprowadzenie

Clean to język systemowy kompilowany natywnie na x86-64. Łączy:

- **Składnię Pythona** — wcięcia zamiast nawiasów klamrowych
- **Wydajność C** — kompilacja wprost do assembly, brak maszyny wirtualnej
- **Bezpieczeństwo Rusta** — ownership i borrow checking bez garbage collectora

Clean nie używa LLVM. Kompilator sam generuje x86-64 assembly, które jest składane przez `as` i linkowane przez `ld`. To oznacza, że cały łańcuch narzędziowy to jedna binarka + systemowy assembler.

### Filozofia

| Zasada | Znaczenie |
|--------|-----------|
| Zero-cost abstrakcje | Nie płacisz za to, czego nie używasz |
| Deterministyczna pamięć | Brak GC, brak stop-the-world pauz |
| Własny backend | Zero zależności, pełna kontrola |
| Prostota | Kompilator w ~5000 liniach C |

---

## 2. Instalacja i uruchamianie

### Wymagania

- System Linux x86-64
- GCC lub Clang (tylko do zbudowania kompilatora)
- GNU Assembler (`as`) i linker (`ld`)
- Opcjonalnie: `libx11-dev` dla GUI

### Budowa

```bash
make                    # produkuje binarkę `clean`
cp clean ~/.local/bin/  # instalacja
hash -r                 # odświeżenie cache PATH
```

### Użycie

```bash
# Uruchom jak Python
clean run program.cl
cl run program.cl       # krótsza forma

# Zbuduj natywną binarkę
clean build program.cl output
./output

# Program GUI (detach, wraca natychmiast)
clean run gui_program.cl
```

Gdy uruchamiasz `clean run`, kompilator:
1. Czyta plik `.cl`
2. Parsuje do AST
3. Sprawdza ownership i czystość
4. Emituje x86-64 assembly
5. Woła `as` → assembler
6. Woła `ld` → ELF (lub `cc` dla GUI)
7. Uruchamia binarkę
8. Po zakończeniu usuwa plik tymczasowy

---

## 3. Podstawy składni

### Wcięcia

Clean używa wcięć do definiowania bloków kodu — zamiast nawiasów `{}`:

```clean
fn check(x: i64) -> i64:
    if x > 0:
        return 1          # ← wewnątrz if, 8 spacji od fn
    return 0              # ← w ciele funkcji, 4 spacje od fn
```

Zalecane: 4 spacje na poziom wcięcia. Tab jest normalizowany do 4 spacji.

### Komentarze

```clean
# To jest komentarz — zaczyna się od # i trwa do końca linii
let x = 5    # komentarz po kodzie też działa
```

### Liczby

```clean
let a = 42          # dziesiętnie
let b = 0xFF        # szesnastkowo
```

Liczby całkowite są 64-bitowe (`i64`).

### Stringi

```clean
let msg = "Hello, World!"
let escaped = "Line 1\nLine 2\tTabulator\"Quote\""
let empty = ""
```

Wspierane sekwencje escape:
- `\n` — newline
- `\r` — carriage return
- `\t` — tab
- `\\` — backslash
- `\"` — cudzysłów

### Wartości logiczne

```clean
let ok = true
let fail = false
```

`true` i `false` to słowa kluczowe. Wewnętrznie: 1 i 0.

---

## 4. Zmienne i mutowalność

### let — zmienna niemutowalna (domyślnie)

```clean
let x = 10
x = 20              # BŁĄD — x jest niemutowalne
```

### var — zmienna mutowalna

`var` to cukier składniowy za `let mut`:

```clean
var count = 0
count = count + 1   # OK
count += 5          # też OK
```

### let mut — jawnie mutowalna

```clean
let mut i = 0
while i < 10:
    print_int(i)
    i += 1
```

### Adnotacja typu

Możesz dodać typ po dwukropku (opcjonalne — typ jest wnioskowany):

```clean
let x: i64 = 42
let name: str = "Alice"
```

Dostępne typy:
- `i64` (domyślny dla liczb całkowitych)
- `str` (wskaźnik do null-terminated stringa)
- `bool` (wartość logiczna)

### Zasięg zmiennych

Zmienne są widoczne od miejsca deklaracji do końca bloku:

```clean
fn test():
    let x = 1
    if true:
        let y = 2       # y istnieje tylko w tym bloku
        print_int(y)
    print_int(x)        # OK — x wciąż w zasięgu
    print_int(y)        # BŁĄD — y nie istnieje poza if
```

---

## 5. Funkcje

### Deklaracja

```clean
fn nazwa(parametr: Typ) -> TypZwracany:
    ciało
```

### Funkcja bez parametrów

```clean
fn greet():
    print_str("Hello!\n", 7)
```

### Funkcja z parametrami i zwracaniem wartości

```clean
fn add(a: i64, b: i64) -> i64:
    return a + b
```

### Funkcja z efektami (I/O)

Jeśli funkcja wywołuje operacje wejścia/wyjścia (print_int, read_int, input, sleep, itp.), musi oznaczyć to słowem `effect`:

```clean
fn show_sum(x: i64, y: i64) effect:
    let sum = x + y
    print_int(sum)
```

`effect` w sygnaturze pozwala kompilatorowi sprawdzać czystość funkcji. Jeśli wywołasz funkcję I/O w funkcji bez `effect`, kompilator wyświetli ostrzeżenie E1003.

### Funkcja zwracająca i mająca efekty

```clean
fn read_and_double() effect -> i64:
    let val = read_int()
    return val * 2
```

### Wiele parametrów

```clean
fn multiply(a: i64, b: i64, c: i64) -> i64:
    return a * b * c

fn main():
    let result = multiply(2, 3, 4)
    print_int(result)           # 24
```

### Komunikacja między funkcjami

Funkcje mogą wołać inne funkcje zdefiniowane w tym samym pliku:

```clean
fn square(x: i64) -> i64:
    return x * x

fn sum_of_squares(a: i64, b: i64) -> i64:
    return square(a) + square(b)

fn main():
    print_int(sum_of_squares(3, 4))    # 25
```

### Funkcja main

`main` to punkt wejścia programu. Powinna zwracać `i64` (kod wyjścia):

```clean
fn main() -> i64:
    print_int(42)
    return 0
```

Kod wyjścia 0 oznacza sukces. Zwróć 1 dla błędu.

---

## 6. Instrukcje warunkowe

### if

```clean
let x = 10
if x > 5:
    print_str("x is greater than 5\n", 22)
```

### if / elif / else

```clean
fn classify(score: i64) -> i64:
    if score >= 90:
        return 1
    elif score >= 75:
        return 2
    elif score >= 50:
        return 3
    else:
        return 4
```

Warunki mogą być dowolnie zagnieżdżane:

```clean
if x > 0:
    if x > 100:
        print_str("large\n", 6)
    else:
        print_str("small positive\n", 15)
else:
    print_str("negative or zero\n", 17)
```

---

## 7. Pętle

### while

```clean
var i = 0
while i < 5:
    print_int(i)
    i += 1
# wypisze: 0 1 2 3 4
```

### for

`for i in start..end` to cukier składniowy — desugaruje do `let mut i = start; while i < end { body; i += 1 }`:

```clean
for i in 0..5:
    print_int(i)
# wypisze: 0 1 2 3 4
```

### break

Przerywa pętlę:

```clean
var i = 0
while true:
    if i >= 5:
        break
    print_int(i)
    i += 1
```

### continue

Pomija resztę iteracji i przechodzi do następnej:

```clean
for i in 0..10:
    if i % 2 == 0:
        continue
    print_int(i)    # wypisze tylko nieparzyste: 1 3 5 7 9
```

### Zagnieżdżone pętle

```clean
for i in 0..3:
    for j in 0..3:
        let product = i * j
        print_int(product)
```

---

## 8. Operatory

### Arytmetyczne

| Operator | Działanie | Przykład | Wynik |
|----------|-----------|----------|-------|
| `+` | Dodawanie | `5 + 3` | 8 |
| `-` | Odejmowanie | `5 - 3` | 2 |
| `*` | Mnożenie | `5 * 3` | 15 |
| `/` | Dzielenie całkowite | `7 / 3` | 2 |
| `%` | Modulo (reszta) | `7 % 3` | 1 |
| `**` | Potęgowanie | `2 ** 4` | 16 |

### Bitowe

| Operator | Działanie | Przykład | Wynik |
|----------|-----------|----------|-------|
| `\|` | OR | `5 \| 2` | 7 |
| `^` | XOR | `5 ^ 3` | 6 |
| `&` | AND | `5 & 3` | 1 |
| `<<` | Przesunięcie w lewo | `1 << 3` | 8 |
| `>>` | Przesunięcie w prawo | `16 >> 2` | 4 |
| `~` | NOT bitowy | `~0` | -1 |

### Porównania

| Operator | Znaczenie | Przykład | Wynik |
|----------|-----------|----------|-------|
| `==` | Równe | `5 == 5` | true |
| `!=` | Różne | `5 != 3` | true |
| `<` | Mniejsze | `3 < 5` | true |
| `<=` | Mniejsze lub równe | `5 <= 5` | true |
| `>` | Większe | `5 > 3` | true |
| `>=` | Większe lub równe | `5 >= 5` | true |

### Logiczne

| Operator | Znaczenie | Przykład | Wynik |
|----------|-----------|----------|-------|
| `and` | Koniunkcja (i) | `true and false` | false |
| `or` | Alternatywa (lub) | `true or false` | true |
| `not` | Negacja | `not true` | false |

### Compound assignment

| Operator | Desugar |
|----------|---------|
| `x += y` | `x = x + y` |
| `x -= y` | `x = x - y` |
| `x *= y` | `x = x * y` |
| `x /= y` | `x = x / y` |

### Priorytety operatorów

Od najwyższego do najniższego:

1. `**` (potęgowanie, prawostronne)
2. `not`, `-` (unarny), `~` (bitowy NOT)
3. `*`, `/`, `%`
4. `+`, `-`
5. `<<`, `>>`
6. `&`
7. `^`
8. `|`
9. `<`, `<=`, `>`, `>=`
10. `==`, `!=`
11. `and`
12. `or`
13. `|>` (pipe)
14. `=` `+=` `-=` `*=` `/=` (przypisanie, prawostronne)

Użyj nawiasów do wymuszenia kolejności:

```clean
let result = (2 + 3) * 4    # 20, nie 14
```

---

## 9. Postfix warunki i unless

### Postfix if

Warunek po instrukcji — desugaruje do `if cond: stmt`:

```clean
return -1 if error
print_str("done") if finished
```

### Postfix unless

Zanegowany warunek po instrukcji — desugaruje do `if !cond: stmt`:

```clean
return -1 unless ok
skip() unless ready
```

Łączenie z pipe:

```clean
x |> process() if ready
```

---

## 10. Operator pipe (|>)

Operator `|>` przekazuje wartość z lewej strony jako pierwszy argument funkcji z prawej.

### Podstawowe użycie

```clean
let x = 5
x |> print_int          # to samo co print_int(5)
```

### Łańcuch pipe

```clean
3 |> double |> print_int   # double(3) → print_int(...)
```

### Pipe z wieloma argumentami

Gdy prawa strona to wywołanie funkcji z argumentami, lewa strona jest dodawana jako pierwszy argument:

```clean
a |> f(b)    # to samo co f(a, b)
```

Czyli:

```clean
let result = 10 |> add(5)   # add(10, 5) = 15
```

### Praktyczny przykład

```clean
fn double(x: i64) -> i64:
    return x * 2

fn triple(x: i64) -> i64:
    return x * 3

fn main():
    5 |> double |> triple |> print_int    # (5*2)*3 = 30
```

---

## 11. Stringi i tekst

### Literały stringowe

```clean
let hello = "Hello, World!"
```

### Sekwencje escape

```clean
let s1 = "Line 1\nLine 2"           # newline
let s2 = "Column 1\tColumn 2"       # tab
let s3 = "Backslash: \\"            # backslash
let s4 = "Quote: \""                # cudzysłów
```

### print_str

Wypisuje `len` bajtów ze wskaźnika `str`:

```clean
let msg = "Hello"
print_str(msg, 3)       # wypisze: Hel
print_str(msg, 5)       # wypisze: Hello
```

### strlen

Zwraca długość null-terminated stringa:

```clean
let name = "Alice"
let len = strlen(name)
print_int(len)          # 5
```

### input

Czyta linię tekstu z konsoli, wyświetlając prompt:

```clean
let name = input("Enter your name: ")
# Użytkownik wpisuje "Bob\n"
# name wskazuje na "Bob\0"
let len = strlen(name)
print_str(name, len)    # Bob
```

`input` używa statycznego bufora 1024 bajtów. Każde kolejne wołanie nadpisuje poprzednią wartość.

### Łączenie operacji na stringach

```clean
fn main():
    let name = input("What is your name? ")
    let len = strlen(name)
    print_str("Hello, ", 7)
    print_str(name, len)
    print_str("!", 1)
```

### Stringi w strukturach

Stringi to tylko wskaźniki (8 bajtów). Możesz je przechowywać w strukturach:

```clean
struct Person
    name
    age

fn main():
    let p = Person(input("Name: "), 30)
    print_str(p.name, strlen(p.name))
```

---

## 12. Struktury

### Definicja

```clean
struct Point
    x
    y
```

Pola nie mają typów — obecnie wszystkie są 8-bajtowe.

### Tworzenie (literał)

```clean
let p = Point(10, 20)
```

Struktura jest alokowana na stercie przez `malloc`. Zwraca wskaźnik do zaalokowanego bloku.

### Dostęp do pól

```clean
print_int(p.x)     # 10
print_int(p.y)     # 20
```

### Modyfikacja

```clean
var p = Point(10, 20)
p.x = 15
print_int(p.x)     # 15
```

### Wiele pól

```clean
struct Rectangle
    x
    y
    width
    height

fn main():
    let r = Rectangle(0, 0, 800, 600)
    print_int(r.width)     # 800
    print_int(r.height)    # 600
```

### Zagnieżdżone struktury

```clean
struct Line
    start
    end

fn main():
    let l = Line(Point(0, 0), Point(10, 20))
    print_int(l.start.x)   # dostęp przez łańcuch . (kropka)
    print_int(l.end.y)     # 20
```

Każde pole to wskaźnik, więc `l.start` ładuje wskaźnik do `Point`, a drugie `.x` dereferencuje.

### Funkcje przyjmujące struktury

```clean
fn print_point(p):
    print_str("(", 1)
    print_int(p.x)
    print_str(", ", 2)
    print_int(p.y)
    print_str(")", 1)

fn main():
    let p = Point(3, 7)
    print_point(p)
```

### Uwaga: rozpoznawanie pól

Obecnie kompilator szuka pola o danej nazwie we **wszystkich** zdefiniowanych strukturach. Jeśli dwie struktury mają pole o tej samej nazwie, kompilator może znaleźć złe przesunięcie. Używaj unikalnych nazw pól.

---

## 13. List składane (comprehensions)

### Składnia

```clean
[expr for var in start..end if cond]
```

### Działanie

1. Tworzy pętlę od `start` do `end-1`
2. Dla każdej iteracji sprawdza `if cond` (jeśli podano)
3. Oblicza `expr`
4. Wypisuje wynik przez `print_int`
5. Zwraca liczbę wypisanych elementów

### Przykłady

```clean
# Kwadraty liczb 1-5
[x * x for x in 1..6]
# wypisze: 1 4 9 16 25
# zwróci: 5

# Parzyste liczby 0-9
[x for x in 0..10 if x % 2 == 0]
# wypisze: 0 2 4 6 8

# Z filtrem i transformacją
[x * 10 for x in 1..10 if x > 5]
# wypisze: 60 70 80 90 100
```

### Użycie zwracanej wartości

Comprehension zwraca liczbę elementów:

```clean
let count = [x for x in 0..100 if x % 7 == 0]
print_int(count)    # 14 (bo tyle liczb 0-99 dzieli się przez 7)
```

### Zagnieżdżone comprehensions (eksperymentalne)

```clean
[x + y for x in 1..3 if x > 1 for y in 1..3 if y > 1]
```

Uwaga: w obecnej wersji comprehensions są uproszczone — działają tylko z `start..end` i zawsze wypisują przez `print_int`.

---

## 14. Zarządzanie zasobami (use)

`use` tworzy zmienną tymczasową na czas trwania bloku:

```clean
use x = expr
    body
```

Desugaruje do:

```clean
let x = expr
body    # x jest dostępne tylko tu
```

### Przykład

```clean
use data = load_large_file()
    process(data)
    analyze(data)
# data jest zwalniane (traci zasięg) po bloku
```

### Zagnieżdżone use

```clean
use a = acquire_a()
    use b = acquire_b()
        process(a, b)
```

---

## 15. Funkcje wbudowane — kompendium

### print_int(n)

Wypisuje liczbę całkowitą zakończoną znakiem nowej linii.

```clean
print_int(42)       # → 42\n
print_int(-7)       # → -7\n
```

Implementacja: asemblerowa — konwertuje liczbę na cyfry, woła `write(1, buf, len)`.

### print_str(ptr, len)

Wypisuje `len` znaków spod wskaźnika `ptr`.

```clean
let msg = "Hello"
print_str(msg, 3)       # → Hel
print_str(msg, 5)       # → Hello
```

Implementacja: `write(1, ptr, len)`.

### read_int()

Czyta liczbę całkowitą ze standardowego wejścia. Wspiera liczby ujemne.

```clean
let n = read_int()
print_int(n * 2)
```

Wejście: `-42` → wypisze: `-84`

Implementacja: czyta bajt po bajcie, buduje liczbę, obsługuje `-` na początku.

### input(prompt)

Wyświetla prompt, czyta linię tekstu do bufora statycznego (1024 bajty), zwraca wskaźnik.

```clean
let name = input("Name: ")
```

BUFOR JEST WSPÓŁDZIELONY — każde kolejne wołanie nadpisuje poprzednie.

### strlen(str)

Zwraca długość null-terminated stringa.

```clean
let len = strlen("Hello")   # 5
```

Implementacja: `repne scasb` — instrukcja asemblerowa x86-64.

### sleep(n)

Usypia na `n` sekund.

```clean
print_str("Waiting...", 10)
sleep(2)
print_str("Done!\n", 6)
```

Implementacja: `nanosleep` syscall (35).

### time_ms()

Zwraca bieżący czas w milisekundach (od epoch).

```clean
let start = time_ms()
# ... obliczenia ...
let elapsed = time_ms() - start
print_int(elapsed)
```

Implementacja: `clock_gettime(CLOCK_MONOTONIC, ...)`.

### calc_expr()

Czyta wyrażenie arytmetyczne z stdin, oblicza z uwzględnieniem priorytetów operatorów (+, -, *, /), zwraca wynik.

```clean
let result = calc_expr()
print_int(result)
```

Wejście: `3 + 4 * 2` → wynik: `11` (bo `*` ma wyższy priorytet)

Wspiera: `+`, `-`, `*`, `/`, `x`, `X`, `:`.

### inspect(x)

Debug: wypisuje `"inspect: N\n"` i zwraca wartość `x`.

```clean
let y = inspect(42)      # wypisze: inspect: 42
# y == 42
```

Przydatne do debugowania w łańcuchach pipe:

```clean
5 |> double |> inspect |> triple |> print_int
# wypisze: inspect: 10  (po double, przed triple)
#         30            (wynik końcowy)
```

Działa tylko w trybie non-GUI.

### assert(x)

Jeśli `x == 0`, wypisuje `"assertion failed\n"` i kończy program z kodem 1.

```clean
let x = 0
assert(x)           # → assertion failed, exit(1)

let y = 42
assert(y)           # → OK, program kontynuuje
```

Działa tylko w trybie non-GUI.

### print_float(n)

Wypisuje liczbę zmiennoprzecinkową.

```clean
print_float(3.14)    # → 3.140000
```

### string_clone(str, len)

Tworzy kopię stringa przez malloc.

```clean
let cloned = string_clone(original, strlen(original))
```

### string_concat(a, b)

Łączy dwa stringi w jeden (malloc'owany). Długości są obliczane automatycznie.

```clean
let msg = string_concat("Hello, ", "world")
```

### get_frame_ptr(data, idx, fsize)

Oblicza wskaźnik do ramki `idx` w buforze: `data + idx * fsize`.

```clean
let frame = get_frame_ptr(buffer, 5, 80)
# frame wskazuje na początek 6. ramki (indeks 5) w buforze
```

Przydatne do animacji i pracy z buforami ramek.

---

## 16. Funkcje terminalowe

Clean oferuje zestaw funkcji do sterowania terminalem przez sekwencje ANSI.

### clear_screen()

Czyści cały terminal (emituje `\033[2J\033[H`):

```clean
clear_screen()
```

### reset_attr()

Resetuje wszystkie atrybuty (kolory, podkreślenia, itp.) do domyślnych:

```clean
reset_attr()
```

### set_fg(n) / set_bg(n)

Ustawia kolor tekstu/tła w palecie 256-kolorowej:

```clean
set_fg(1)           # czerwony tekst
print_int(42)
set_fg(2)           # zielony tekst
print_int(99)
set_fg(7)           # biały tekst
reset_attr()         # powrót do domyślnych
```

Standardowe kolory (0-15):

| Nr | Kolor | Nr | Kolor |
|----|-------|----|-------|
| 0 | Black | 8 | Bright Black |
| 1 | Red | 9 | Bright Red |
| 2 | Green | 10 | Bright Green |
| 3 | Yellow | 11 | Bright Yellow |
| 4 | Blue | 12 | Bright Blue |
| 5 | Magenta | 13 | Bright Magenta |
| 6 | Cyan | 14 | Bright Cyan |
| 7 | White | 15 | Bright White |

Kolory 16-231 to sześcienna siatka 6×6×6 RGB. Kolory 232-255 to skala szarości.

### hide_cursor() / show_cursor()

Ukrywa lub pokazuje kursor terminala:

```clean
hide_cursor()
# rysuj animację bez migającego kursora
show_cursor()
```

### Przykład: kolorowy terminal

```clean
fn main():
    clear_screen()
    set_fg(2)                       # zielony
    print_str("Success!\n", 9)
    set_fg(1)                       # czerwony
    print_str("Error!\n", 6)
    set_fg(4)                       # niebieski
    print_str("Info\n", 5)
    reset_attr()
    return 0
```

---

## 17. Ownership i borrow checking

Clean ma prosty ownership checker, który zapobiega używaniu zmiennych po ich przeniesieniu (move).

### Move semantics

Gdy przypisujesz wartość do innej zmiennej, oryginał staje się nieaktywny:

```clean
let a = 42
let b = a           # a jest moved do b
print_int(a)        # BŁĄD E1001: use of moved value `a`
print_int(b)        # OK — 42
```

### Dlaczego?

W przyszłości Clean będzie miał typy, które nie są `Copy` (np. własnościowe stringi, uchwyty do plików). Move semantics zapobiega podwójnemu zwalnianiu i użyciu po zwolnieniu. Na razie wszystkie wartości są proste (liczby, wskaźniki), ale mechanizm już działa.

### Co NIE jest move?

Wywołanie funkcji nie przenosi własności (w obecnej wersji):

```clean
let x = 10
print_int(x)        # OK — x wciąż żyje
print_int(x)        # OK — x wciąż żyje
```

### move/ref/mut_ref — jawne operacje własnościowe

Słowa kluczowe `move`, `ref`, `mut_ref` to jawne operacje na własności i pożyczkach:

- `move x` — jawnie przenosi własność (oznacza intencję, kod jest no-op)
- `ref x` — tworzy niezmienną pożyczkę (`NODE_BORROW`)
- `mut_ref x` — tworzy zmienną pożyczkę (`NODE_MUT_BORROW`)

```clean
let x = 10
let y = move x      # jawny move, x staje się nieaktywny
```

### Purity checking (effect)

Jeśli funkcja jest oznaczona jako czysta (brak `effect`), kompilator ostrzeże, jeśli wywołasz w niej funkcję I/O:

```clean
fn pure() -> i64:  # brak 'effect'
    print_int(42)    # OSTRZEŻENIE E1003: call to impure function
    return 0
```

Popraw:

```clean
fn impure() effect -> i64:
    print_int(42)    # OK
    return 0
```

---

## 18. Effect — system czystości

Słowo kluczowe `effect` w sygnaturze funkcji oznacza, że funkcja ma efekty uboczne (najczęściej I/O).

### Zasady

1. Funkcja **bez** `effect` nie powinna wołać funkcji **z** `effect`
2. Funkcja **z** `effect` może wołać wszystko
3. `main` powinna mieć `effect` jeśli robi I/O

### Przykłady

```clean
fn pure_calc(x: i64) -> i64:  # czysta funkcja
    return x * x

fn side_effect() effect:  # nieczysta
    print_int(42)

fn mixed() effect -> i64:  # nieczysta, ale też zwraca wartość
    print_str("calculating...", 15)
    return 99

fn main():
    # main może wołać zarówno czyste jak i nieczyste funkcje
    let r = pure_calc(5)         # OK
    side_effect()                # OK
    return 0
```

### Dlaczego?

System czystości pomaga:
- Utrzymać kod przewidywalnym
- Oddzielić logikę biznesową od I/O
- W przyszłości: ułatwić testowanie i wnioskowanie o programie

---

## 19. extern — wołanie C z Cleana

`extern fn` pozwala zadeklarować funkcję zdefiniowaną w C (lub innej bibliotece), którą linker może dołożyć.

### Składnia

```clean
extern fn nazwa(parametry) -> typ
```

### Przykład

```clean
extern fn write(fd: i64, buf, len: i64) -> i64

fn main():
    let msg = "Hello from extern!\n"
    write(1, msg, 19)
    return 0
```

### GUI detection

Jeśli w programie pojawi się `extern fn` z nazwą zaczynającą się od `clgui_`, kompilator automatycznie przełącza się w tryb GUI:

- Zamiast `_start` emituje `clean_main`
- Linkuje przez `cc` (z C runtime) a nie `ld`
- Dodaje `-lX11`

```clean
extern fn clgui_init() -> i64
extern fn clgui_window(w: i64, h: i64) -> i64
extern fn clgui_fill(idx: i64, color: i64)
extern fn clgui_draw_str(idx: i64, x: i64, y: i64, text)
extern fn clgui_event(idx: i64) -> i64
```

### Ograniczenia

- `extern` tylko na poziomie top-level (nie wewnątrz funkcji)
- Brak automatycznego generowania bindings — musisz zdefiniować sygnaturę ręcznie
- Wszystkie typy to 64-bit integers lub wskaźniki

---

## 20. GUI — programy okienkowe

Clean ma wbudowane wsparcie dla GUI przez X11 (Xlib). GUI jest wykrywany automatycznie po obecności funkcji `extern` z prefiksem `clgui_`.

### Pipeline GUI

1. Kompilator wykrywa `clgui_*` externy
2. Pomija emisję `_start`, `print_int`, `sleep` itp. (są w clgui.c)
3. Kompiluje `clgui.c` przez `cc`
4. Linkuje z `-lX11`
5. Uruchamia z double-fork (detach, wraca natychmiast)

### Funkcje GUI

```clean
clgui_init() -> i64                 # inicjalizuje X11
clgui_window(w, h) -> i64           # tworzy okno o rozmiarze w×h
clgui_title(idx, title)             # ustawia tytuł okna
clgui_fill(idx, color)              # wypełnia okno kolorem
clgui_color(r, g, b) -> i64        # tworzy kolor RGB (0-255 każdy)
clgui_draw_str(idx, x, y, text)    # rysuje tekst
clgui_draw_int(idx, x, y, n)       # rysuje liczbę
clgui_event(idx) -> i64            # czeka na zdarzenie (zwraca typ)
clgui_event_x() -> i64             # X zdarzenia
clgui_event_y() -> i64             # Y zdarzenia
clgui_event_btn() -> i64           # przycisk myszy
clgui_event_key() -> i64           # klawisz
print_int(n)                        # print_int działa też w GUI
sleep(n)                            # sleep działa też w GUI
```

### Przykład: kalkulator GUI

```clean
extern fn clgui_init() -> i64
extern fn clgui_window(w: i64, h: i64) -> i64
extern fn clgui_fill(idx: i64, color: i64)
extern fn clgui_draw_str(idx: i64, x: i64, y: i64, text)
extern fn clgui_color(r: i64, g: i64, b: i64) -> i64
extern fn clgui_event(idx: i64) -> i64
extern fn clgui_event_key() -> i64
extern fn sleep(sec: i64)

fn main() effect -> i64:
    let win = clgui_window(400, 300)
    clgui_title(win, "Calculator")
    let bg = clgui_color(235, 235, 235)
    let black = clgui_color(0, 0, 0)

    clgui_fill(win, bg)
    clgui_draw_str(win, 10, 30, "Click a key, or ESC to quit")

    while true:
        let ev = clgui_event(win)
        if ev == 2: # KeyPress
            let key = clgui_event_key()
            if key == 65307: # ESC
                break
            clgui_draw_str(win, 10, 60, "Key pressed!")
            clgui_draw_int(win, 10, 80, key)

    return 0
```

### Uruchamianie

```bash
clean run calc.cl
```

Program GUI robi double-fork i wraca natychmiast. Binarka tymczasowa jest usuwana po 50ms (żeby grandchild zdążył się uruchomić).

---

## 21. Programy CLI i argumenty

Możesz przekazywać argumenty do programu uruchomionego przez `clean run`:

```bash
clean run program.cl arg1 arg2 arg3
```

Wewnątrz programu możesz użyć `extern fn` do dostępu do `argc`/`argv`:

```clean
extern fn main(argc: i64, argv) -> i64

fn real_main(argc: i64, argv) effect:
    print_int(argc)
    print_str(argv, strlen(argv))

fn main() -> i64:
    # obecnie main nie otrzymuje argc/argv automatycznie
    return 0
```

Uwaga: obecnie `main` nie otrzymuje automatycznie argumentów. To jest planowane.

---

## 22. Debugowanie z inspect i assert

### inspect

`inspect(x)` wypisuje wartość `x` i ją zwraca. Idealne do debugowania środka wyrażenia:

```clean
# Sprawdź, co zwraca funkcja przed dalszym przetwarzaniem
let result = complex_calc() |> inspect |> process
```

### assert

`assert(x)` sprawdza warunek i przerywa program jeśli fałszywy:

```clean
let x = parse_input()
assert(x > 0)       # program zatrzyma się jeśli x <= 0
process(x)
```

### Debugowanie pętli

```clean
for i in 0..10:
    let val = compute(i)
    inspect(val)    # zobaczysz każdą wartość
```

### Debugowanie w GUI

W trybie GUI `inspect` i `assert` nie są dostępne (są emitowane tylko dla non-GUI). Zamiast tego używaj `print_int` i `clgui_draw_str`.

---

## 23. Wydajność i optymalizacje

### Obecny stan

- Wszystkie zmienne na stosie (brak alokacji rejestrów)
- Brak optymalizacji (constant folding, DCE)
- Kompilacja przez `as` i `ld` (bez link-time optimization)

### Benchmark: count-to-1-billion

| Język | Czas | Mnożnik |
|-------|------|---------|
| Python | 132 s | 18× |
| Clean | 7.3 s | 1× |
| C -O0 | 3.6 s | 0.5× |

### Dlaczego Clean jest wolniejszy od C?

1. **Brak optymalizacji rejestrów** — zmienne są zawsze zapisywane i odczytywane ze stosu (mov [rbp-X], mov rax, [rbp-X])
2. **Brak LICM** (Loop Invariant Code Motion)
3. **Brak inliningu**
4. **Brak stałej propagacji**

### Jak pisać szybki kod w Cleanie?

1. **Minimalizuj zmienne lokalne** — mniej zmiennych = mniejszy stack frame
2. **Używaj prostych pętli** — unikaj zagnieżdżonych comprehensions
3. **Preferuj while nad for** — for desugaruje do while z dodatkowym let (minimalny narzut)
4. **Unikaj call w gorących pętlach** — każde wołanie to push/pop argumentów

---

## 24. Znane ograniczenia

### Składnia i parser
- `:` po `while`/`if`/`fn` jest wymagany
- Token pod caret w błędach może być źle wyrównany jeśli linia ma tabulatory lub Unicode
- Brak ostrzeżenia o nieużywanych zmiennych

### System typów
- Wszystkie zmienne na stosie (brak optymalizacji rejestrów)
- Brak stringów (*u8, usize, etc.)
- Brak typów poza i64/bool/str/float
- Struktury: rozpoznawanie pól po nazwie we wszystkich strukturach (może znaleźć złe przesunięcie)
- Option\<T\> i Result\<T, E\> istnieją w prelude.cl ale wsparcie kompilatora jest częściowe
- `assert` abort(1) — brak własnej wiadomości

### Funkcje
- `extern` tylko na poziomie top-level (nie wewnątrz funkcji)
- List comprehensions zawsze używają print_int (brak czystej iteracji)
- Comprehensions tylko range (start..end)
- Niektóre `diag_add` wołane z `0,0,1` jako lokacją (brak pozycji tokena)

### Współbieżność
- Brak kanałów i zielonych wątków
- Brak async

### Anotacje
- `@memoize`, `@lazy` — jeszcze nie zaimplementowane

---

## 25. Przykłady krok po kroku

### 25.1 Silnia (factorial)

```clean
fn factorial(n: i64) -> i64:
    let mut result = 1
    let mut i = 1
    while i <= n:
        result = result * i
        i = i + 1
    return result

fn main() -> i64:
    let n = 10
    print_int(factorial(n))
    return 0
```

### 25.2 Ciąg Fibonacciego

```clean
fn fibonacci(n: i64) -> i64:
    if n <= 1:
        return n
    var a = 0
    var b = 1
    var i = 2
    while i <= n:
        let temp = a + b
        a = b
        b = temp
        i += 1
    return b

fn main():
    for i in 0..20:
        print_int(fibonacci(i))
```

### 25.3 Gra w zgadywanie liczby

```clean
fn main() effect:
    let secret = 42
    print_str("Guess the number (0-100):\n", 27)

    while true:
        print_str("Your guess: ", 12)
        let guess = read_int()

        if guess == secret:
            print_str("Correct!\n", 9)
            break
        elif guess < secret:
            print_str("Too low!\n", 9)
        else:
            print_str("Too high!\n", 10)
```

### 25.4 Odliczanie rakiety

```clean
fn main() effect:
    var i = 10
    while i >= 0:
        print_int(i)
        sleep(1)
        i -= 1
    print_str("LIFTOFF!\n", 9)
```

### 25.5 Tabliczka mnożenia

```clean
fn main():
    var i = 1
    while i <= 10:
        var j = 1
        while j <= 10:
            print_int(i * j)
            j += 1
        i += 1
```

### 25.6 Liczby pierwsze (sito Eratostenesa)

```clean
fn is_prime(n: i64) -> i64:
    if n < 2:
        return 0
    var i = 2
    while i * i <= n:
        if n % i == 0:
            return 0
        i += 1
    return 1

fn main():
    for i in 2..50:
        if is_prime(i):
            print_int(i)
```

### 25.7 Animacja ASCII z kolorami

```clean
fn main() effect:
    hide_cursor()
    var frame = 0
    while frame < 100:
        clear_screen()
        set_fg(frame % 7 + 1)
        let mut y = 0
        while y < 10:
            let mut x = 0
            while x < 20:
                let val = (x + y + frame) % 10
                print_int(val)
                x += 1
            y += 1
        sleep(1)   # 1000ms (sleep przyjmuje i64, sekundy)
        frame += 1
    show_cursor()
```

### 25.8 Struktury: punkty i odległość

```clean
struct Point
    x
    y

fn distance(p1, p2) -> i64:
    let dx = p1.x - p2.x
    let dy = p1.y - p2.y
    return dx * dx + dy * dy

fn main():
    let a = Point(0, 0)
    let b = Point(3, 4)
    let d = distance(a, b)
    print_int(d)       # 25 (3² + 4²)
```

### 25.9 Użycie calc_expr

```clean
fn main() effect:
    print_str("Enter expression (e.g. 3 + 4 * 2):\n", 36)
    let result = calc_expr()
    print_str("Result: ", 8)
    print_int(result)
```

### 25.10 Pomiar czasu wykonania

```clean
fn main() effect:
    let start = time_ms()
    var sum = 0
    var i = 0
    while i < 1000000:
        sum = sum + i
        i = i + 1
    let elapsed = time_ms() - start
    print_str("Sum: ", 5)
    print_int(sum)
    print_str("Time: ", 6)
    print_int(elapsed)
    print_str(" ms\n", 4)
```

---

## 26. Zadania do samodzielnego rozwiązania

### Poziom 1 — Podstawy

1. **Hello World** — Napisz program, który wypisuje "Hello, Clean!"
2. **Imię i wiek** — Zapytaj użytkownika o imię i wiek, wypisz "Hello, [imię]! You are [wiek] years old."
3. **Suma liczb** — Wczytaj dwie liczby, wypisz ich sumę
4. **Parzysta/nieparzysta** — Wczytaj liczbę, wypisz 1 jeśli parzysta, 0 jeśli nie
5. **Odliczanie** — Wypisz liczby od 10 do 1, potem "Go!"

### Poziom 2 — Funkcje i pętle

6. **Silnia** — Oblicz silnię zadanej liczby (iteracyjnie)
7. **Fibonacci** — Wypisz pierwsze N wyrazów ciągu Fibonacciego
8. **NWD** — Oblicz największy wspólny dzielnik dwóch liczb (algorytm Euklidesa)
9. **Czy pierwsza?** — Sprawdź, czy liczba jest pierwsza
10. **Tabliczka mnożenia** — Wypisz tabliczkę mnożenia N×N

### Poziom 3 — Struktury i zaawansowane

11. **Kalkulator** — Użyj `calc_expr()` do zrobienia REPL-a kalkulatora
12. **Animacja** — Zrób animację ASCII z kolorami i ukrytym kursorem
13. **Gra w zgadywanie** — Pełna gra z liczbą prób i podpowiedziami
14. **Bubble sort** — Zaimplementuj sortowanie bąbelkowe (na tablicy przez struct)
15. **Zegar** — Program wyświetlający aktualny czas i odświeżający co sekundę

### Poziom 4 — Eksperymentalne

16. **List comprehension** — Użyj comprehension do wypisania liczb pierwszych z zakresu
17. **Pipe chain** — Napisz łańcuch 3+ funkcji połączonych pipe
18. **GUI okno** — Stwórz okno X11 z kolorowym prostokątem
19. **Use pattern** — Użyj `use` do zarządzania "zasobem" (symulacja pliku)
20. **Mandelbrot** — Wypisz ASCII art fraktala Mandelbrota

---

## 27. Szybka ściągawka

```clean
# Komentarze

# Zmienne
let x = 42              # niemutowalna
var y = 0               # mutowalna
let mut z = 10          # też mutowalna

# Funkcje
fn add(a: i64, b: i64) -> i64:
    return a + b

fn greet(n) effect -> str:
    print_str("Hi ", 3)
    return n

# Warunki
if x > 0:
    ...
elif x == 0:
    ...
else:
    ...

# Pętle
while cond:
    ...

for i in 0..10:
    ...

# break / continue
while true:
    if done:
        break
    if skip:
        continue

# Operatory
+ - * / % == != < <= > >=
and or not
+= -= *= /= **
| ^ & << >> ~

# Pipe
x |> f               # f(x)
x |> f(y)            # f(x, y)

# Postfix
do_it() if ready
do_it() unless done

# Struktury
struct Point
    x
    y

let p = Point(10, 20)
p.x

# List składana
[x*2 for x in 1..10 if x > 5]

# use
use r = open(path)
    process(r)

# extern
extern fn write(fd: i64, buf, len: i64) -> i64

# Wbudowane
print_int(n)
print_float(n)
print_str(ptr, len)
read_int()
input(prompt)
strlen(str)
sleep(n)
time_ms()
calc_expr()
inspect(x)
assert(x)
clear_screen()
reset_attr()
set_fg(n)
set_bg(n)
hide_cursor()
show_cursor()
get_frame_ptr(data, idx, fsize)
string_clone(str, len)
string_concat(a, b)

# Own/borrow
move x
ref x
mut_ref x

# Funkcja main
fn main() -> i64:
    ...
    return 0
```

---

*Clean — natywny, prosty, wydajny.*
