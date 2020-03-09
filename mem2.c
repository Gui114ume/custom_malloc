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
#include <math.h>
#include <pthread.h>

#include "mem.h"

#define MIN(a , b) ( ( (a) < (b) ) ? (a) : (b) )

struct bloc // taille 40 octets
{
    void* adresse;
    void* addr_previous;
    void* addr_next;
    int numero;// 0 -> libre , 1 -> occupé
    unsigned int taille;
};

//pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//malloc de plus d'une page -> mmap . Puis quand il est free, munmap.
//malloc de moins d"une page, on decoupe en morceau de 64 octets par exemple, grace a une liste chainee.
//on fait un premier mmap assez gros, puis on construit une liste chainé. On peut ensuite allouer si on a la place, sinon mmap et on met à jour la liste chainé
struct bloc* bloc_origine_64 = NULL;
struct bloc* bloc_origine_128 = NULL;
struct bloc* bloc_origine_256 = NULL;
struct bloc* bloc_origine_512 = NULL;
struct bloc* bloc_origine_1024 = NULL;
struct bloc* bloc_origine_huge = NULL;

unsigned long long taille_max = pow(2,16);
int    indice = 0;

static int j64_max = 0;
static int j128_max = 0;
static int j256_max = 0;
static int j512_max = 0;

static pthread_mutex_t mutex_malloc = PTHREAD_MUTEX_INITIALIZER;

void* malloc(size_t size)
{
    pthread_mutex_lock(&mutex_malloc);
    //printf("entrée dans le malloc\n");
#define ADDR_FINALE_64  0   //comment faire ca ? etant donne que c'est une adresse ?
#define ADDR_FINALE_128 0
#define ADDR_FINALE_256 0
#define ADDR_FINALE_512 0
#define ADDR_FINALE_1024 0

    void* ret_ptr64 = NULL;
    void* ret_ptr128 = NULL;
    void* ret_ptr256 = NULL;
    void* ret_ptr512 = NULL;
    void* ret_ptr1024 = NULL;

    void* ret_ptr_huge = NULL;
    //int local =-1;
    //pthread_mutex_lock(&mutex);
    //local = indice;
    //indice = 1;
    //pthread_mutex_unlock(&mutex);
    if(indice == 0)
    {

        indice =1;
        //pthread_mutex_lock(&mutex);
        //creation de la liste chainé a l'aide d'un gros mmap
        unsigned int t64  = (pow(2,29) - 1)/4;
        unsigned int t128 = (pow(2,29) - 1)/4;
        unsigned int t256 = (pow(2,29) - 1)/4;
        unsigned int t512 = (pow(2,29) - 1)/4;
        unsigned int t1024 = (pow(2,29) - 1)/4;

        ret_ptr64  = mmap(NULL, t64 , PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr128 = mmap(NULL, t128, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr256 = mmap(NULL, t256, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr512 = mmap(NULL, t512, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        //ret_ptr1024 = mmap(NULL, t1024, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);


        bloc_origine_64  = ret_ptr64;
        bloc_origine_128 = ret_ptr128;
        bloc_origine_256 = ret_ptr256;
        bloc_origine_512 = ret_ptr512;
        //bloc_origine_1024 = ret_ptr1024;
        bloc_origine_huge = ret_ptr_huge;//initialiser le premier pour mettre des NULL partout, on pourra ainsi
        // faire un recyclage par une boucle while en cas d'appel a malloc pour une huge size

        int i = 0;
        while( t64 > sizeof(struct bloc) + 64 )// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
            //non rien  ici !!
        {
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->adresse       = ((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_64 + i * (64 +
                    sizeof(struct bloc) )))->adresse == ret_ptr64 + sizeof(struct bloc)  ) ? NULL : ((struct bloc*)((void*)bloc_origine_64 + i * (64 +
                sizeof(struct bloc)) ))->adresse - 64 - sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->addr_next     = ((struct bloc*)((void*)bloc_origine_64 + i * (64 + sizeof(struct bloc) )))->adresse + sizeof(struct bloc) + 64;
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->taille = 64 + sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->numero = 0;

            if(((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->adresse  == (void*)NULL) //ok cest pas null
                exit(-1);
            if(((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->addr_next == (void*)NULL  )//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                exit(-1);
            t64 -= (sizeof(struct bloc) + 64);
            ++i;
        }
        j64_max = i;

        ((struct bloc*)((void*)bloc_origine_64 + (i - 1)* ( 64 + sizeof(struct bloc) )))->addr_next     = (void*)NULL;

        //comme on conserve i, on pourra après la boucle while, mettre à jour le dernier bloc de metadonnée et ainsi mettre NULL dans le dernier addr_next, sans utilisé de variable ADDR_FINALE_XXXX
        // IMPORTAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAANT


        i = 0;

        while( t128 > sizeof(struct bloc) + 128 )
        {
            //c'est ca qu'il faut ecrire  la ou il faut, on caste en void* pour decaller le pointeur correctement, puis on recaste en struct bloc* pour acceder aux champs des structures
            ((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->adresse      = ((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_128 + i * ( 128 +
                    sizeof(struct bloc) )))->adresse == ret_ptr128 + sizeof(struct bloc) ) ? NULL : ((struct bloc*)((void*)bloc_origine_128 + i * (128 +
                sizeof(struct bloc)) ))->adresse - 128 - sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->addr_next     =  ((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc)) ))->adresse + sizeof(struct bloc) + 128;
            ((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->taille = 128 + sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->numero = 0;

            if(((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->adresse  == (void*)NULL) //ok cest pas null
                exit(-1);
            if(((struct bloc*)((void*)bloc_origine_128 + i * ( 128 + sizeof(struct bloc) )))->addr_next == (void*)NULL  )//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                exit(-1);
            t128 -= (sizeof(struct bloc) + 128);
            ++i;
        }
        ((struct bloc*)((void*)bloc_origine_128 + (i - 1)* ( 128 + sizeof(struct bloc) )))->addr_next     = (void*)NULL;
        j128_max = i;
        i = 0;

        while( t256 > sizeof(struct bloc) + 256 )
        {
            ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 + sizeof(struct bloc) )))->adresse       = ((void*)bloc_origine_256 + i * ( 256 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_256 + i * (256 +
                    sizeof(struct bloc)) ))->adresse == ret_ptr256 + sizeof(struct bloc) ) ? NULL : ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 +
                sizeof(struct bloc)) ))->adresse - 256 - sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 + sizeof(struct bloc) )))->addr_next     =((struct bloc*)((void*)bloc_origine_256 + i * (256 + sizeof(struct bloc)) ))->adresse + sizeof(struct bloc) + 256;
            ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 + sizeof(struct bloc) )))->taille = 256 + sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 + sizeof(struct bloc) )))->numero = 0;

            t256 -= (sizeof(struct bloc) + 256);
            ++i;
        }

        ((struct bloc*)((void*)bloc_origine_256 + (i - 1)* ( 256 + sizeof(struct bloc) )))->addr_next     = (void*)NULL;
        j256_max = i;
        i = 0;

        while( t512 > sizeof(struct bloc) + 512 )
        {
            ((struct bloc*)((void*)bloc_origine_512 + i * ( 512 + sizeof(struct bloc) )))->adresse       = ((void*)bloc_origine_512 + i * ( 512 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_512 + i * ( 512 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_512 + i * ( 512 +
                    sizeof(struct bloc)) ))->adresse == ret_ptr512 + sizeof(struct bloc) ) ? NULL : ((struct bloc*)((void*)bloc_origine_512 + i * (512 +
                sizeof(struct bloc)) ))->adresse - 512 - sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_512 + i * ( 512 + sizeof(struct bloc) )))->addr_next     = ((struct bloc*)((void*)bloc_origine_512 + i * (512 + sizeof(struct bloc)) ))->adresse + sizeof(struct bloc) + 512;
            ((struct bloc*)((void*)bloc_origine_512 + i * ( 512 + sizeof(struct bloc) )))->taille = 512 + sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_512 + i * ( 512 + sizeof(struct bloc) )))->numero = 0;

            t512 -= (sizeof(struct bloc) + 512);
            ++i;
        }
        ((struct bloc*)((void*)bloc_origine_512 + (i - 1)* ( 512 + sizeof(struct bloc) )))->addr_next     = (void*)NULL;
        j512_max = i;
        i=0;

        /*
        while( t1024 > sizeof(struct bloc) + 1024 )
        {
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->adresse       = ((void*)bloc_origine_1024 + i * ( 1024+ sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 +
                                                                                                                                                            sizeof(struct bloc)) ))->adresse == ret_ptr1024 + sizeof(struct bloc) ) ? NULL : ((struct bloc*)((void*)bloc_origine_1024 + i * (1024 +
                                                                                                                                                                                                                                                                                           sizeof(struct bloc)) ))->adresse - 1024 - sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->addr_next     = (((struct bloc*)((void*)bloc_origine_1024 + i * (1024 +
                                                                                                                                                           sizeof(struct bloc)) ))->adresse != ADDR_FINALE_1024 ) ? ((struct bloc*)((void*)bloc_origine_1024 + i * (1024 + sizeof(struct bloc)) ))->adresse + sizeof(struct bloc) + 1024: NULL;

            t1024 -= (sizeof(struct bloc) + 1024);
            ++i;
        }
         */

        if( size < 64 )
        {
            ret_ptr64 = bloc_origine_64->adresse;
            bloc_origine_64->numero = 1;
            if(ret_ptr64 == (void*)NULL)
            {
                //printf("ret_ptr64 = NULL, abort()\n");
                abort();
            }
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr64;
        }
        else if (size < 128)
        {
            ret_ptr128 = bloc_origine_128->adresse;
            bloc_origine_128->numero = 1;
            if(ret_ptr128 == (void*)NULL)
            {
                //printf("ret_ptr128 = NULL, abort()\n");
                abort();
            }
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr128;
        }
        else if (size < 256)
        {
            ret_ptr256 = bloc_origine_256->adresse;
            bloc_origine_256->numero = 1;
            if(ret_ptr256 == (void*)NULL)
            {
                //printf("ret_ptr256 = NULL, abort()\n");
                abort();
            }
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr256;
        }
        else if (size < 512)
        {
            ret_ptr512 = bloc_origine_512->adresse;
            bloc_origine_512->numero = 1;
            if(ret_ptr512 == (void*)NULL)
            {
                //printf("ret_ptr512 = NULL, abort()\n");
                abort();
            }
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr512;
        }
        else
        {
            ret_ptr_huge = mmap(NULL, sizeof(struct bloc) + size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            bloc_origine_huge = ret_ptr_huge;
            bloc_origine_huge->numero = 1;
            bloc_origine_huge->adresse = ret_ptr_huge + sizeof(struct bloc);
            bloc_origine_huge->addr_previous = NULL;
            bloc_origine_huge->addr_next = NULL;
            bloc_origine_huge->taille = size + sizeof(struct bloc);
            if( bloc_origine_huge->adresse == (void*)NULL)
            {
                //printf("bloc_origine_huge->adresse = NULL, abort()\n");
                abort();
            }
            pthread_mutex_unlock(&mutex_malloc);
            return ((struct bloc*)ret_ptr_huge)->adresse;
        }

    }
    else // on ecrit un algo qui cherche une place pour satisfaire la demande et met à jour les adresses previous et next, puis
    // si on y arrive pas , mmap
    {
        int j = rand()%j64_max;
        if(size < 64)
        {
            while( ((struct bloc*)((void*)bloc_origine_64 + (j )* (64 + sizeof(struct bloc)) ))->addr_next != NULL ) //probleeeeeeeme
            {
                if(  ( ((struct bloc*)((void*)bloc_origine_64 + (j + 1)* (64 + sizeof(struct bloc) )) )->numero == 0 ) ) // est-ce libre ?
                {
                    ((struct bloc*)((void*)bloc_origine_64 + (j + 1)* (64 + sizeof(struct bloc) )) )->numero = 1; //maintenant ca l'est plus

                    if( ((struct bloc*)((void*)bloc_origine_64 + (j + 1) * (64 + sizeof(struct bloc) )) )->adresse == (void*)NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                    {
                        //printf("64 = NULL, j = %u,abort()\n",j);// GRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
                        pthread_mutex_unlock(&mutex_malloc);
                        //perror("ici\n");
                        abort();
                    }
                    pthread_mutex_unlock(&mutex_malloc);
                    return ((struct bloc*)((void*)bloc_origine_64 + (j + 1) * (64 + sizeof(struct bloc) )) )->adresse;
                }
                j++;
            }
            j = 0;
        }
        j = rand()%j128_max;
        if(size < 128)
        {
            while( ( ((struct bloc*)((void*)bloc_origine_128 + j * ( 128 + sizeof(struct bloc)) ))->addr_next != (void*)NULL ) )
            {
                if( ((struct bloc*)((void*)bloc_origine_128 + (j + 1)* (128 + sizeof(struct bloc) )) )->numero == 0) // est-ce libre ?
                {
                    ((struct bloc*)((void*)bloc_origine_128 + (j + 1)* (128 + sizeof(struct bloc) )) )->numero = 1; //maintenant ca l'est plus
                    if( ((struct bloc*)((void*)bloc_origine_128 + (j + 1) * (128 + sizeof(struct bloc) )) )->adresse == (void*)NULL)
                    {
                        //printf("128 = NULL, abort()\n");
                        //perror("ici\n");
                        abort();
                    }
                    pthread_mutex_unlock(&mutex_malloc);

                    return ((struct bloc*)((void*)bloc_origine_128 + (j + 1)* (128 + sizeof(struct bloc) )) )->adresse;
                }
                j++;
            }
            j = 0;
        }
        j = rand()%j256_max;
        if(size < 256)
        {
            while( ( ((struct bloc*)((void*)bloc_origine_256 + j * ( 256 + sizeof(struct bloc)) ))->addr_next != (void*)NULL ) )
            {
                if( ((struct bloc*)((void*)bloc_origine_256 + (j + 1)* (256 + sizeof(struct bloc) )) )->numero == 0) // est-ce libre ?
                {
                    ((struct bloc*)((void*)bloc_origine_256 + (j + 1)* (256 + sizeof(struct bloc) )) )->numero = 1; //maintenant ca l'est plus
                    if( ((struct bloc*)((void*)bloc_origine_256 + (j + 1) * (256 + sizeof(struct bloc) )) )->adresse == (void*)NULL)
                    {
                        //printf("256 = NULL, abort()\n");
                        //perror("ici\n");
                        abort();
                    }
                    pthread_mutex_unlock(&mutex_malloc);

                    return ((struct bloc*)((void*)bloc_origine_256 + (j + 1)* (256 + sizeof(struct bloc) )) )->adresse;
                }
                j++;
            }
            j = 0;
        }
        j = rand()%j512_max;
        if (size < 512)
        {
            while( ( ((struct bloc*)((void*)bloc_origine_512 + j * ( 512 + sizeof(struct bloc)) ))->addr_next != (void*)NULL ) )
            {
                if( ((struct bloc*)((void*)bloc_origine_512 + (j + 1)* (512 + sizeof(struct bloc) )) )->numero == 0) // est-ce libre ?
                {
                    ((struct bloc*)((void*)bloc_origine_512 + (j + 1)* (512 + sizeof(struct bloc) )) )->numero = 1; //maintenant ca l'est plus
                    if( ((struct bloc*)((void*)bloc_origine_512 + (j + 1) * (512 + sizeof(struct bloc) )) )->adresse == (void*)NULL)
                    {
                        //printf("512 = NULL, abort()\n");
                        //perror("ici\n");
                        abort();
                    }
                    pthread_mutex_unlock(&mutex_malloc);

                    return ((struct bloc*)((void*)bloc_origine_512 + (j + 1)* (512 + sizeof(struct bloc) )) )->adresse;
                }
                j++;
            }
            j=0;
        }
        else //probleme pour les free() et le recyclage, munmap
        {
            //trouver le dernier bloc de metadonnée, puis modifier son next_addr
            ret_ptr_huge = mmap(NULL, sizeof(struct bloc) + size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            bloc_origine_huge = ret_ptr_huge;
            bloc_origine_huge->taille = size + sizeof(struct bloc);
            bloc_origine_huge->numero=1;
            bloc_origine_huge->adresse = (void*)bloc_origine_huge + sizeof(struct bloc);
            bloc_origine_huge->addr_previous=NULL;
            bloc_origine_huge->addr_next=NULL;
            //perror("huge ! \n");
            if( bloc_origine_huge->adresse == (void*)NULL)
            {
                //printf("huge = NULL, abort()\n");
                //perror("ici\n");

                abort();
                //exit(-1);
            }
            pthread_mutex_unlock(&mutex_malloc);

            return bloc_origine_huge->adresse;
        }
    }


}
static pthread_mutex_t mutex_free = PTHREAD_MUTEX_INITIALIZER;
void free(void* ptr)
{
    //printf("indice : %d\n",indice);
    //printf("addr de bloc origine 64 : %p\n",bloc_origine_64);
    //printf("free ok\n");
    pthread_mutex_lock(&mutex_free);
    //printf("entre dans le free ptr_addr = %p\n",ptr); // parfois le ptr est NULL ou plutot nil ! c'est ca le blem !
    if(ptr == NULL)
    {
        //printf("NULL pointeur free()\n ");
        pthread_mutex_unlock(&mutex_free);
        return (void)0;
    }
    struct bloc* tmp_bloc = (void*)ptr - sizeof(struct bloc); //des fois, ça crée une segfault SIGSEGV
    tmp_bloc->numero=0;
    //printf("taille + 40 = %u\n",tmp_bloc->taille);
    if(tmp_bloc->taille > (512 + sizeof(struct bloc)) )
    {
        //printf("avant munmap\n");
        munmap(tmp_bloc, tmp_bloc->taille);
        //printf("apres munmap\n");
    }

    //printf("sort du free\n");
    pthread_mutex_unlock(&mutex_free);
    return (void)0;
}
static pthread_mutex_t mutex_calloc = PTHREAD_MUTEX_INITIALIZER;
void* calloc(size_t nmemb, size_t size)
    {
    pthread_mutex_lock(&mutex_calloc);
    if( nmemb == 0 || size == 0) {
        pthread_mutex_unlock(&mutex_calloc);
        return (void *)NULL;
    }
    void* ret = malloc(size * nmemb);
    for(int i = 0 ; i < (size * nmemb) ; i++)
    {
        ((char*)ret)[i] = 0;
    }
    pthread_mutex_unlock(&mutex_calloc);
    return ret;
}


static pthread_mutex_t mutex_realloc = PTHREAD_MUTEX_INITIALIZER;
void* realloc(void* ptr, size_t size)
{
    //printf("entree dans realloc\n");
    pthread_mutex_lock(&mutex_realloc);
    if(ptr == (void*)NULL)
    {
        void* ret = malloc(size);
        //printf("sortie realloc 1\n");
        pthread_mutex_unlock(&mutex_realloc);

        return ret;
    }
    if(size == 0 && ptr != (void*)NULL)
    {
        free(ptr);
        //printf("sortie realloc 2\n");
        pthread_mutex_unlock(&mutex_realloc);

        return (void*)0;
    }
    void* ret = malloc(size);
    int min = MIN( size, ((struct bloc*)((void*)ptr - sizeof(struct bloc)))->taille - sizeof(struct bloc)  );  // gerer le cas ou l'user met une size trop grande auquel cas il ne faut pas faire de segfaul// t
    // en fait c'est plutot le minimum qu'il faut !
    //printf("max = %d\n",max);
    for(int i = 0 ; i < min ; i++)
    {
        ((char*)ret)[i] = ((char*)ptr)[i];
    }
    //memcpy(ret, ptr, size);
    //printf("sortie realloc\n");
    pthread_mutex_unlock(&mutex_realloc);

    return ret;
}

void* reallocarray(void* ptr, size_t nmemb, size_t size)
{
    void* ret = realloc(ptr, nmemb * size);
    return ret;
}