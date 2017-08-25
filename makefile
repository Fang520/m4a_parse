all:
	gcc -g -Wall -o test test.c
	gcc -g -Wall -I/usr/local/include/ -L/usr/local/lib/ -o decode decode.c -lfdk-aac
clean:
	rm -f test
	rm -f decode
