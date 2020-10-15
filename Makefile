CC=gcc
CFLAGS=-O0 #ne pas mettre d'optimisation, sinon l'allocateur ne fonctionne plus
LDFLAGS=-lpthread

lib.so: mem.c
	gcc -shared -fPIC $^ -o $@ $(CFLAGS) $(LDFLAGS)
test: test.c
	gcc $^ -o $@ $(CFLAGS)
clean:
	rm lib.so test
