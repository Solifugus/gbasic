CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g
GTK_AVAILABLE := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 && printf 1 || printf 0)
GTK_CFLAGS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS := $(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --libs gtk+-3.0 2>/dev/null)

ifeq ($(GTK_AVAILABLE),1)
CFLAGS += -DHAVE_GTK=1 $(GTK_CFLAGS)
LDLIBS += $(GTK_LIBS)
else
CFLAGS += -DHAVE_GTK=0
endif

OBJS := src/main.o src/lexer.o src/parser.tab.o src/ast.o src/eval.o src/builtins.o

.PHONY: all clean

all: gbasic

gbasic: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

src/parser.tab.c src/parser.tab.h: src/parser.y include/ast.h include/lexer.h
	bison -d -o src/parser.tab.c src/parser.y

src/main.o: src/main.c include/ast.h include/eval.h include/lexer.h include/builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

src/lexer.o: src/lexer.c include/lexer.h
	$(CC) $(CFLAGS) -c $< -o $@

src/parser.tab.o: src/parser.tab.c src/parser.tab.h include/ast.h include/lexer.h
	$(CC) $(CFLAGS) -c src/parser.tab.c -o $@

src/ast.o: src/ast.c include/ast.h
	$(CC) $(CFLAGS) -c $< -o $@

src/eval.o: src/eval.c include/eval.h include/ast.h include/builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

src/builtins.o: src/builtins.c include/builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f gbasic $(OBJS) src/parser.tab.c src/parser.tab.h
