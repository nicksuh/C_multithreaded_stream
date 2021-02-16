test:
	gcc -O3 -g cstream.h cstream.c -lpthread -o cstream
	./cstream
