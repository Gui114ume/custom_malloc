/*
 * Implementation d'une librairie d'allocation memoire. Le but est de pouvoir l'interposer avec le malloc
 * de la lib standart a l'aide d'un LD_PRELOAD
 *
 *
 * L'idee:
 * -> Allocation de page à l'aide de mmap
 * -> Stockage dans les tableaux ci-dessous d'adresse donnant acces a 64,128, etc octets de memoire, ainsi que la taille
 * reellement demandé par l'utilsateur
 * -> lors d'une demande de memoire, on fait soit un mmap, soit on cherche une adresse dispo dans les tableaux
 * -> les tableaux sont donc un annuaire des zones memoires libres deja mappé que l'on refuse de munmapper
 *
 * Nouvelle idée: recycler les pages qui ont encore de la place
 * -> Il nous faut un annuaire, qui stocke les emplacement donnant accès à 2^n octets
 * -> à l'allocation, il faut choisir quoi faire selon la taille demandé.
 * -> après avoir mmaper, il faut mettre à jour l'annuaire
 * Structure de donnée pour l'annuaire:
 *  -> struc free_bloc64 par exemple <- a reutiliser
 *  -> struc annuaire_bloc64  <- a free un jour, ils sont utilisé
 *  -> chaque element contient une adresse, la taille est forcément de 64 octets !
 *  -> ça a l'air d'etre un peu la merde de faire ce genre de truc !
 *
 */
#include <stddef.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"


struct bloc
{
    size_t taille;
    size_t adresse;

};

struct bloc BLOC[100000];
struct bloc BLOC[100000];

int    indice = 0;

void* malloc(size_t size)
{
    if(indice > 100000)
    {
        indice = 0;//on va perdre les tailles alloués, en tout cas l'information, donc on ne pourra plus faire de free !
        printf("remise a zero d'indice\n");
    }
    void* ret_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    BLOC[indice].taille  = size;
    BLOC[indice].adresse = (size_t)ret_ptr;
    ++indice;
    return (void*)ret_ptr;

}

void free(void* ptr)
{
    printf("hello ;)\n");
    int free_taille;
    for(int i = 0; i < 100000 ; i++)
    {
        if(ptr == (void*)(BLOC[i].adresse))
        {
            free_taille = BLOC[i].taille;
            printf("free_taille = %d, indice %d\n",free_taille,i);
            break;
        }
    }

    munmap(ptr,free_taille);// vu comment on alloue, on peut munmap a chaque fois sans risque pour l'intégrité des donnees
    return (void)0;//success
}

void* calloc(size_t nmemb, size_t size)
{
    if(indice > 100000)
    {
        indice = 0;//on va perdre les tailles alloués, en tout cas l'information, donc on ne pourra plus faire de free !
        printf("remise a zero d'indice\n");
    }
    void* ret_ptr = mmap(NULL, size * nmemb, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    BLOC[indice].taille  = size;
    BLOC[indice].adresse = (size_t)ret_ptr;
    ++indice;
    return (void*)ret_ptr;

}

void* realloc(void* ptr, size_t size)
{
    size_t loc_taille;
    if(ptr == NULL)
        malloc(size);
    if(ptr != NULL && size == 0)
        free(ptr);
    for(int i = 0 ; i <  100000 ; i++)
    {
        if(ptr == (void*)(BLOC[i].adresse))
        {
            loc_taille = BLOC[i].taille;
            break;
        }
    }
    if(size < loc_taille)
    {
        return (void*)ptr;
    }
    if(size >= loc_taille)
    {
        void* new_ptr = malloc(size);
        memcpy(new_ptr, ptr, loc_taille);
        return new_ptr;
    }
    return (void*)0;
}