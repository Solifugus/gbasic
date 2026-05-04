CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g

OBJS := src/main.o src/lexer.o src/parser.tab.o src/ast.o src/eval.o src/builtins.o

.PHONY: all clean

all: gbasic

gbasic: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

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
