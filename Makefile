# Clean compiler — stage-0 bootstrap (C11)
# Natywny kompilator: parser → x86-64 asm → as + ld → binarka
# `cl run file.cl` jak Python, `cl build file.cl output` jak C

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -Iinclude -D_GNU_SOURCE
LDFLAGS ?=

SRC     := src/main.c src/ast.c src/diag.c src/check.c src/borrowck.c src/parser/lexer.c src/parser/parser.c src/codegen/codegen.c src/codegen/emit_asm.c src/codegen/regalloc.c src/mir/mir_build.c src/mir/mir_opt.c src/lir/lir_lower.c
TARGET  := clean

.PHONY: all clean install distclean

all: $(TARGET)

src/runtime/clgui_embed.h: src/runtime/clgui.c
	echo '#ifndef CLGUI_EMBED_H' > $@
	echo '#define CLGUI_EMBED_H' >> $@
	echo 'static const char *clgui_embedded =' >> $@
	sed 's/\\/\\\\/g; s/"/\\"/g; s/.*/  "&\\n"/' $< >> $@
	echo '  "";' >> $@
	echo '#endif' >> $@

$(TARGET): $(SRC) src/runtime/clgui_embed.h
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

install: $(TARGET)
	install -m 755 $(TARGET) $(BINDIR)/clean
	install -m 644 src/runtime/clgui.c $(BINDIR)/clgui.c
	ln -sf $(BINDIR)/clean $(BINDIR)/cl

distclean:
	rm -f $(TARGET)
