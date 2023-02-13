all: main.c thpool.c thpool.h
	gcc -g main.c thpool.c

clean:
	rm -f a.out