all:
	gcc -g -Wall -o test test.c
	gcc -g -Wall -o decode decode.c -lfdk-aac
clean:
	rm -f test
	rm -f decode
