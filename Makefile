CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS ?=

SRC     := src/main.c src/ast.c src/diag.c src/check.c \
           src/parser/lexer.c src/parser/parser.c src/codegen/codegen.c
TARGET  := clean.bin
CMD     := clean
PREFIX  ?= $(HOME)/.local

.PHONY: all install clean distclean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/$(CMD)
	install -d $(PREFIX)/share/$(CMD)
	install -m 644 src/runtime/clgui.c $(PREFIX)/share/$(CMD)/clgui.c
	install -m 644 lib/prelude.cl $(PREFIX)/share/$(CMD)/prelude.cl
	ln -sf $(PREFIX)/bin/$(CMD) $(PREFIX)/bin/cl

clean:
	rm -f $(TARGET) *.o *.s

distclean: clean
	rm -f /tmp/clean_*
