CC=gcc
CFLAGS=-O0 #ne pas mettre d'optimisation, sinon la lib ne fonctionne plus...
LDFLAGS=-lpthread

all: lib.so test

lib.so: mem.c
	gcc -shared -fPIC $^ -o $@ $(CFLAGS) $(LDFLAGS)

test: test.c
	gcc $^ -o $@ $(CFLAGS)
clean:
	rm lib.so test
