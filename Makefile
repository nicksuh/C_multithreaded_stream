test:
	gcc -O3 -g cstream.h cstream.c -lpthread -o cstream
	./cstream

test_full:
	gcc -O3 -g cstream.h cstream.c -lpthread -o cstream
	valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
		./cstream