#include <stddef.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>


struct bloc // taille 40 octets
{
    void* adresse;
    void* addr_previous;  // est-ce utile ? je n'ai trouvé aucun intéret pour l'instant
    void* addr_next;
    int numero;// 0 -> libre , 1 -> occupé
    unsigned int taille;
};

void *malloc(size_t size);
void get_stat(void);
