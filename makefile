CFLAGS=-std=c17 -Wall -Wextra -Werror -W -Wshadow -Wcast-align -Wredundant-decls -Wbad-function-cast -O2 -g

all:
	gcc main.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs`

debug:
	gcc main.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUG

debugrom:
	gcc main.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUGROM