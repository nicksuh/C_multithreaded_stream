test:
	gcc -O3 -g sbuffer.h sbuffer.c -lpthread -o sbuffer
	./sbuffer
