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
    void* addr_previous;  // est-ce utile ? je n'ai trouvé aucun intéret pour l'instant
    void* addr_next;
    int numero;// 0 -> libre , 1 -> occupé
    unsigned int taille;
};
#define COUNTER

#ifdef COUNTER
static unsigned long long nb_malloc = 0;
static unsigned long long nb_free = 0;
static unsigned long long nb_calloc = 0;
static unsigned long long nb_realloc = 0;
static unsigned long long nb_mmap = 0;
static unsigned long long nb_munmap = 0;
static unsigned long long nb_recyclage = 0;
#endif


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

static unsigned int t64 = 0;
static unsigned int t128 = 0;
static unsigned int t256 = 0;
static unsigned int t512 = 0;
static unsigned int t1024 = 0;


void* next64 = NULL;
void* next128 = NULL;
void* next256 = NULL;
void* next512 = NULL;
void* next1024 = NULL;

static int    indice = 0;

static unsigned int j64_max = 0;
static unsigned int j128_max = 0;
static unsigned int j256_max = 0;
static unsigned int j512_max = 0;
static unsigned int j1024_max = 0;

static pthread_mutex_t mutex_malloc = PTHREAD_MUTEX_INITIALIZER;

void* malloc(size_t size)
{
    pthread_mutex_lock(&mutex_malloc);
#ifdef COUNTER
    nb_malloc += 1;
#endif
    //printf("entrée dans le malloc\n");

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
        t64  = 2 << 25;   //  = 2 >> 25
        t128 = 2 << 25;
        t256 = 2 << 25;  // 130 MB
        t512 = 2 << 25;
        t1024 = 2 << 25;

#ifdef COUNTER
        nb_mmap += 5;
#endif
        //ce serait intelligent que l'on mmap de moins grosse donnees, mais qu'à la fin, quand on arrive pas a trouver un bloc a donner, on recommence a mmaper, ainsi pu de pbleme si plusieurs processus utilise mon allocateur en meme temps
        ret_ptr64  = mmap(NULL, t64 , PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr128 = mmap(NULL, t128, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr256 = mmap(NULL, t256, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr512 = mmap(NULL, t512, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr1024 = mmap(NULL, t1024, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);



        bloc_origine_64  = ret_ptr64;
        bloc_origine_128 = ret_ptr128;
        bloc_origine_256 = ret_ptr256;
        bloc_origine_512 = ret_ptr512;
        bloc_origine_1024 = ret_ptr1024;
        bloc_origine_huge = ret_ptr_huge;//initialiser le premier pour mettre des NULL partout, on pourra ainsi
        // faire un recyclage par une boucle while en cas d'appel a malloc pour une huge size

        int i = 0;
        while( t64 > sizeof(struct bloc) + 64 )// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
            //non rien  ici !!
        {
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->adresse       = ((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_64 + i * ( 64 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_64 + i * (64 +
                    sizeof(struct bloc) )))->adresse == ret_ptr64 + sizeof(struct bloc)  ) ? NULL : ((struct bloc*)((void*)bloc_origine_64 + i * (64 +
                sizeof(struct bloc)) ))->adresse - 64 - sizeof(struct bloc);  // on ne met plus de null, on est maintenant sur un anneau
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

        ((struct bloc*)((void*)bloc_origine_64 + (i - 1)* ( 64 + sizeof(struct bloc) )))->addr_next     = bloc_origine_64->adresse;
        bloc_origine_64->addr_previous = ((struct bloc*)((void*)bloc_origine_64 + (i - 1)* ( 64 + sizeof(struct bloc) )))->adresse;

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

        j128_max = i;
        ((struct bloc*)((void*)bloc_origine_128 + (i - 1)* ( 128 + sizeof(struct bloc) )))->addr_next     = bloc_origine_128->adresse;
        bloc_origine_128->addr_previous = ((struct bloc*)((void*)bloc_origine_128 + (i - 1)* ( 128 + sizeof(struct bloc) )))->adresse;


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


        j256_max = i;
        ((struct bloc*)((void*)bloc_origine_256 + (i - 1)* ( 256 + sizeof(struct bloc) )))->addr_next     = bloc_origine_256->adresse;
        bloc_origine_256->addr_previous = ((struct bloc*)((void*)bloc_origine_256 + (i - 1)* ( 256 + sizeof(struct bloc) )))->adresse;
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
        j512_max = i;
        ((struct bloc*)((void*)bloc_origine_512 + (i - 1)* ( 512 + sizeof(struct bloc) )))->addr_next     = bloc_origine_512->adresse;
        bloc_origine_512->addr_previous = ((struct bloc*)((void*)bloc_origine_512 + (i - 1)* ( 512 + sizeof(struct bloc) )))->adresse;
        i=0;


        while( t1024 > sizeof(struct bloc) + 1024 )
        {
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->adresse       = ((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 +
                    sizeof(struct bloc)) ))->adresse == ret_ptr1024 + sizeof(struct bloc) ) ? NULL : ((struct bloc*)((void*)bloc_origine_1024 + i * (1024 +
                            sizeof(struct bloc)) ))->adresse - 1024 - sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->addr_next     = ((struct bloc*)((void*)bloc_origine_1024 + i * (1024 + sizeof(struct bloc)) ))->adresse + sizeof(struct bloc) + 1024;
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->taille = 1024 + sizeof(struct bloc);
            ((struct bloc*)((void*)bloc_origine_1024 + i * ( 1024 + sizeof(struct bloc) )))->numero = 0;

            t1024 -= (sizeof(struct bloc) + 1024);
            ++i;
        }

        j1024_max = i;
        ((struct bloc*)((void*)bloc_origine_1024 + (i - 1)* ( 1024 + sizeof(struct bloc) )))->addr_next     = bloc_origine_1024->adresse;
        bloc_origine_1024->addr_previous = ((struct bloc*)((void*)bloc_origine_1024 + (i - 1)* ( 1024 + sizeof(struct bloc) )))->adresse;
        i = 0;

        if( size < 64 )
        {
            ret_ptr64 = bloc_origine_64->adresse;
            bloc_origine_64->numero = 1;
            if(ret_ptr64 == (void*)NULL)
            {
                //printf("ret_ptr64 = NULL, abort()\n");
                abort();
            }
            next128 = bloc_origine_128->adresse;
            next256 = bloc_origine_256->adresse;
            next512 = bloc_origine_512->adresse;
            next1024 = bloc_origine_1024->adresse;
            next64 = bloc_origine_64->addr_next;

            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr64;
        }
        else if (size < 128 )
        {
            ret_ptr128 = bloc_origine_128->adresse;
            bloc_origine_128->numero = 1;
            if(ret_ptr128 == (void*)NULL)
            {
                //printf("ret_ptr128 = NULL, abort()\n");
                abort();
            }
            next128 = bloc_origine_128->addr_next;
            next256 = bloc_origine_256->adresse;
            next512 =  bloc_origine_512->adresse;
            next1024 = bloc_origine_1024->adresse;
            next64 = bloc_origine_64->adresse;
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr128;
        }
        else if (size < 256 )
        {
            ret_ptr256 = bloc_origine_256->adresse;
            bloc_origine_256->numero = 1;
            if(ret_ptr256 == (void*)NULL)
            {
                //printf("ret_ptr256 = NULL, abort()\n");
                abort();
            }
            next128 = bloc_origine_128->adresse;
            next256 = bloc_origine_256->addr_next;
            next512=  bloc_origine_512->adresse;
            next1024 = bloc_origine_1024->adresse;
            next64 = bloc_origine_64->adresse;
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr256;
        }
        else if (size < 512 )
        {
            ret_ptr512 = bloc_origine_512->adresse;
            bloc_origine_512->numero = 1;
            if(ret_ptr512 == (void*)NULL)
            {
                //printf("ret_ptr512 = NULL, abort()\n");
                abort();
            }
            next128 = bloc_origine_128->adresse;
            next256 = bloc_origine_256->adresse;
            next512 = bloc_origine_512->addr_next;
            next1024 = bloc_origine_1024->adresse;
            next64 = bloc_origine_64->adresse;
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr512;
        }

        else if(size < 1024)
        {
            ret_ptr1024 = bloc_origine_1024->adresse;
            bloc_origine_1024->numero = 1;
            if(ret_ptr1024 == (void*)NULL)
            {
                abort();
            }
            next128 = bloc_origine_128->adresse;
            next256 = bloc_origine_256->adresse;
            next512= bloc_origine_512->adresse;
            next1024 = bloc_origine_1024->addr_next;
            next64 = bloc_origine_64->adresse;
            pthread_mutex_unlock(&mutex_malloc);
            return ret_ptr1024;
        }

        else  // c'est dommage de ne pas recycler les grosses zones alloues, soit en le gardant tel quel, soit en le decoupant en morceau plus petit, mais si un prog fait des gros mmap, on est mort, on va mapper a chaque fois et recycler inutilement
        {
#ifdef COUNTER
            nb_mmap += 1;
#endif
            ret_ptr_huge = mmap(NULL, sizeof(struct bloc) + size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            bloc_origine_huge = ret_ptr_huge;
            bloc_origine_huge->numero = 1;
            bloc_origine_huge->adresse = ret_ptr_huge + sizeof(struct bloc);
            bloc_origine_huge->addr_previous = NULL;
            bloc_origine_huge->addr_next = NULL;
            bloc_origine_huge->taille = size + sizeof(struct bloc);

            next128 = bloc_origine_128->adresse;
            next256 = bloc_origine_256->adresse;
            next512= bloc_origine_512->adresse;
            next1024 = bloc_origine_1024->adresse;
            next64 = bloc_origine_64->adresse;

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
    {   if(next64 == NULL)
            abort();
        int count = j64_max;
        if (size < 64 ) { // 65 nan ? on peut ecrire jusqu'a 64 octets, donc size < 65
            while (count >
                   0// il faut sauter d'adresse en adresse sans utiliser d'indice j , sinon on ne peut pas rajouter
                //de bloc. Il faudra donc un compteur permettant d'indiquer si on a fait un tour complet des adresses ( le next du dernier pointera vers le premier, il faut stocker le nb de bloc dans une
                // variable, on peut garder j64_max et le calculer de la meme manière que maintenant dans mem2.c) si on a fini un tour sans trouver de bloc libre, on en alloue d'autre et on rempli comme il faut
                // les addr next et addr previous poiur etre comme dans un anneau!
                    ) //probleeeeeeeme
            {
                if (((struct bloc *) ((void *) next64 - sizeof(struct bloc)))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next64 - sizeof(struct bloc)))->numero = 1; //maintenant ca l'est plus

                    if (next64 == (void *) NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                    {
                        //printf("64 = NULL, j = %u,abort()\n",j);// GRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
                        pthread_mutex_unlock(&mutex_malloc);
                        //perror("ici\n");
                        abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next64;
                    next64 = ((struct bloc *) ((void *) next64 - sizeof(struct bloc)))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc);
                    return ret;
                }
                count--;
                next64 = ((struct bloc*)((void*)next64 - sizeof(struct bloc)))->addr_next;
            }
        }
        //if(size < 64 + 1) on recrée de nouveaux bloc de 64 octets puis on lui en donne un avec un return;

        if(size < 64 )
        {
        int i = 0;
#ifdef COUNTER
        nb_mmap += 1;
#endif
        struct bloc* tmp_bloc = mmap(NULL, t64,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        while( t64 > sizeof(struct bloc) + 64 )// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
            //non rien  ici !!
        {
            ((struct bloc*)((void*)tmp_bloc + i * (64 + sizeof(struct bloc) )))->adresse        = ((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )))->addr_previous = (((struct bloc*)((void*)tmp_bloc + i * (64 +
                    sizeof(struct bloc) )))->adresse == ( (void*)tmp_bloc + sizeof(struct bloc) )  ) ? bloc_origine_64->addr_previous : ((struct bloc*)((void*)tmp_bloc + i * (64 +
                            sizeof(struct bloc)) ))->adresse - 64 - sizeof(struct bloc);  // on ne met plus de null, on est maintenant sur un anneau
            ((struct bloc*)((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )))->addr_next     = ((struct bloc*)((void*)tmp_bloc + i * (64 + sizeof(struct bloc) )))->adresse + sizeof(struct bloc) + 64;
            ((struct bloc*)((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )))->taille = 64 + sizeof(struct bloc);
            ((struct bloc*)((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )))->numero = 0;

            if(((struct bloc*)((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )))->adresse  == (void*)NULL) //ok cest pas null
                exit(-1);
            if(((struct bloc*)((void*)tmp_bloc + i * ( 64 + sizeof(struct bloc) )))->addr_next == (void*)NULL  )//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                exit(-1);
            t64 -= (sizeof(struct bloc) + 64);

            ++i;
            ++j64_max;
        }

        ((struct bloc*)((void*)tmp_bloc + (i - 1)* ( 64 + sizeof(struct bloc) )))->addr_next     = bloc_origine_64->adresse;
        ((struct bloc*)(bloc_origine_64->addr_previous - sizeof(struct bloc)))->addr_next        = tmp_bloc->adresse;
        bloc_origine_64->addr_previous = ((struct bloc*)((void*)tmp_bloc + (i - 1)* ( 64 + sizeof(struct bloc) )))->adresse;
        next64 = tmp_bloc->addr_next;
        tmp_bloc->numero = 1;

        pthread_mutex_unlock(&mutex_malloc);
        return (tmp_bloc->adresse);
        // #on mmap des nouveaux blocs;
        // #on les remplis avec un while comme au dessus
        // #on met a jour la valeur j64_max
        // #on renvoie l'un des blocs nouvellement créé

        }

        count = j128_max;

        if (size < 128 ) { // 65 nan ? on peut ecrire jusqu'a 64 octets, donc size < 65
            while (count >
                   0// il faut sauter d'adresse en adresse sans utiliser d'indice j , sinon on ne peut pas rajouter
                //de bloc. Il faudra donc un compteur permettant d'indiquer si on a fait un tour complet des adresses ( le next du dernier pointera vers le premier, il faut stocker le nb de bloc dans une
                // variable, on peut garder j64_max et le calculer de la meme manière que maintenant dans mem2.c) si on a fini un tour sans trouver de bloc libre, on en alloue d'autre et on rempli comme il faut
                // les addr next et addr previous poiur etre comme dans un anneau!
                    ) //probleeeeeeeme
            {
                if (((struct bloc *) ((void *) next128 - sizeof(struct bloc)))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next128 - sizeof(struct bloc)))->numero = 1; //maintenant ca l'est plus

                    if (next128 ==
                        (void *) NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                    {
                        //printf("64 = NULL, j = %u,abort()\n",j);// GRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
                        pthread_mutex_unlock(&mutex_malloc);
                        //perror("ici\n");
                        abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next128;
                    next128 = ((struct bloc *) ((void *)next128 - sizeof(struct bloc)))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc);
                    return ret;
                }
                count--;
                next128 = ((struct bloc*)((void*)next128 - sizeof(struct bloc)))->addr_next;

            }
        }
        //if(j < 128 + 1)

        if(size < 128 )
        {

            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            struct bloc *tmp_bloc = mmap(NULL, t128,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t128 > sizeof(struct bloc) + 128)// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
                //non rien  ici !!
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->adresse = ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->addr_previous = (((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->adresse == (void*)tmp_bloc + sizeof(struct bloc))
                        ? bloc_origine_128->addr_previous : ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->adresse - 128 - sizeof(struct bloc);  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->adresse + sizeof(struct bloc) + 128;
                ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->taille = 128 + sizeof(struct bloc);
                ((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->adresse == (void *) NULL) //ok cest pas null
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (128 + sizeof(struct bloc))))->addr_next ==  (void *) NULL)//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                    exit(-1);
                t128 -= (sizeof(struct bloc) + 128);

                ++i;
                ++j128_max;
            }

            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (128 + sizeof(struct bloc))))->addr_next = bloc_origine_128->adresse;
            ((struct bloc *) (bloc_origine_128->addr_previous - sizeof(struct bloc)))->addr_next = tmp_bloc->adresse;
            bloc_origine_128->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (128 + sizeof(struct bloc))))->adresse;
            next128 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;

            pthread_mutex_unlock(&mutex_malloc);
            return (tmp_bloc->adresse);
        }

        count = j256_max;

        if (size < 256 ) { // 65 nan ? on peut ecrire jusqu'a 64 octets, donc size < 65
            while (count >
                   0// il faut sauter d'adresse en adresse sans utiliser d'indice j , sinon on ne peut pas rajouter
                //de bloc. Il faudra donc un compteur permettant d'indiquer si on a fait un tour complet des adresses ( le next du dernier pointera vers le premier, il faut stocker le nb de bloc dans une
                // variable, on peut garder j64_max et le calculer de la meme manière que maintenant dans mem2.c) si on a fini un tour sans trouver de bloc libre, on en alloue d'autre et on rempli comme il faut
                // les addr next et addr previous poiur etre comme dans un anneau!
                    ) //probleeeeeeeme
            {
                if (((struct bloc *) ((void *) next256 - sizeof(struct bloc)))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next256 - sizeof(struct bloc)))->numero = 1; //maintenant ca l'est plus

                    if (next256 ==
                        (void *) NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                    {
                        //printf("64 = NULL, j = %u,abort()\n",j);// GRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
                        pthread_mutex_unlock(&mutex_malloc);
                        //perror("ici\n");
                        abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next256;
                    next256 = ((struct bloc *) ((void *) next256 - sizeof(struct bloc)))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc);
                    return ret;
                }
                count--;
                next256 = ((struct bloc*)((void*)next256 - sizeof(struct bloc)))->addr_next;

            }
        }


        //if(j < 256 + 1)


        if(size < 256)
        {
            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            struct bloc *tmp_bloc = mmap(NULL, t256,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t256 > sizeof(struct bloc) + 256)// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
                //non rien  ici !!
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->adresse = ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->addr_previous = (((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->adresse == (void*)tmp_bloc + sizeof(struct bloc))
                        ? bloc_origine_256->addr_previous : ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->adresse - 256 - sizeof(struct bloc);  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->adresse + sizeof(struct bloc) + 256;
                ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->taille = 256 + sizeof(struct bloc);
                ((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->adresse == (void *) NULL) //ok cest pas null
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (256 + sizeof(struct bloc))))->addr_next ==  (void *) NULL)//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                    exit(-1);
                t256 -= (sizeof(struct bloc) + 256);

                ++i;
                ++j256_max;
            }

            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (256 + sizeof(struct bloc))))->addr_next = bloc_origine_256->adresse;
            ((struct bloc *) (bloc_origine_256->addr_previous - sizeof(struct bloc)))->addr_next = tmp_bloc->adresse;
            bloc_origine_256->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (256 + sizeof(struct bloc))))->adresse;
            next256 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;
            pthread_mutex_unlock(&mutex_malloc);
            return (tmp_bloc->adresse);
        }



        count = j512_max;

        if (size < 512 ) { // 65 nan ? on peut ecrire jusqu'a 64 octets, donc size < 65
            while (count >
                   0// il faut sauter d'adresse en adresse sans utiliser d'indice j , sinon on ne peut pas rajouter
                //de bloc. Il faudra donc un compteur permettant d'indiquer si on a fait un tour complet des adresses ( le next du dernier pointera vers le premier, il faut stocker le nb de bloc dans une
                // variable, on peut garder j64_max et le calculer de la meme manière que maintenant dans mem2.c) si on a fini un tour sans trouver de bloc libre, on en alloue d'autre et on rempli comme il faut
                // les addr next et addr previous poiur etre comme dans un anneau!
                    ) //probleeeeeeeme
            {
                if (((struct bloc *) ((void *) next512 - sizeof(struct bloc)))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next512 - sizeof(struct bloc)))->numero = 1; //maintenant ca l'est plus

                    if (next512 ==
                        (void *) NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                    {
                        //printf("64 = NULL, j = %u,abort()\n",j);// GRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
                        pthread_mutex_unlock(&mutex_malloc);
                        //perror("ici\n");
                        abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next512;
                    next512 = ((struct bloc *) ((void *) next512 - sizeof(struct bloc)))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc);
                    return ret;
                }
                count--;
                next512 = ((struct bloc*)((void*)next512 - sizeof(struct bloc)))->addr_next;

            }
        }
        //if(j < 512 + 1)

        if(size < 512 )
        {
            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            struct bloc *tmp_bloc = mmap(NULL, t512, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t512 > sizeof(struct bloc) + 512)// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
                //non rien  ici !!
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->adresse = ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->addr_previous = (((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->adresse == (void*)tmp_bloc + sizeof(struct bloc))
                        ? bloc_origine_512->addr_previous : ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->adresse - 512 - sizeof(struct bloc);  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->adresse + sizeof(struct bloc) + 512;
                ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->taille = 512 + sizeof(struct bloc);
                ((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->adresse == (void *) NULL) //ok cest pas null
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (512 + sizeof(struct bloc))))->addr_next ==  (void *) NULL)//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                    exit(-1);
                t512 -= (sizeof(struct bloc) + 512);

                ++i;
                ++j512_max;
            }

            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (512 + sizeof(struct bloc))))->addr_next = bloc_origine_512->adresse;
            ((struct bloc *) (bloc_origine_512->addr_previous - sizeof(struct bloc)))->addr_next = tmp_bloc->adresse;
            bloc_origine_512->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (512 + sizeof(struct bloc))))->adresse;
            next512 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;

            pthread_mutex_unlock(&mutex_malloc);
            return (tmp_bloc->adresse);
        }


        count = j1024_max;

        if (size < 1024 )
        {
            while (count > 0// il faut sauter d'adresse en adresse sans utiliser d'indice j , sinon on ne peut pas rajouter
                //de bloc. Il faudra donc un compteur permettant d'indiquer si on a fait un tour complet des adresses ( le next du dernier pointera vers le premier, il faut stocker le nb de bloc dans une
                // variable, on peut garder j64_max et le calculer de la meme manière que maintenant dans mem2.c) si on a fini un tour sans trouver de bloc libre, on en alloue d'autre et on rempli comme il faut
                // les addr next et addr previous poiur etre comme dans un anneau!
                    ) //probleeeeeeeme
            {
                if (((struct bloc *) ((void *) next1024 - sizeof(struct bloc)))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next1024 - sizeof(struct bloc)))->numero = 1; //maintenant ca l'est plus
                    if (next1024 ==
                        (void *) NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                    {
                        //printf("64 = NULL, j = %u,abort()\n",j);// GRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
                        pthread_mutex_unlock(&mutex_malloc);
                        //perror("ici\n");
                        abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next1024;
                    next1024 = ((struct bloc *) ((void *) next1024 - sizeof(struct bloc)))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc);
                    return ret;
                }
                count--;
                next1024 = ((struct bloc*)((void*)next1024 - sizeof(struct bloc)))->addr_next;

            }
        }
        //if(j < 1024 + 1)

        if(size < 1024 )
        {
            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            struct bloc *tmp_bloc = mmap(NULL, t1024,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t1024 > sizeof(struct bloc) + 1024)// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
                //non rien  ici !!
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->adresse = ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))) + sizeof(struct bloc);//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->addr_previous = (((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->adresse == (void*)tmp_bloc + sizeof(struct bloc))
                        ? bloc_origine_1024->addr_previous : ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->adresse - 1024 - sizeof(struct bloc);  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->adresse + sizeof(struct bloc) + 1024;
                ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->taille = 1024 + sizeof(struct bloc);
                ((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->adresse == (void *) NULL) //ok cest pas null
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (1024 + sizeof(struct bloc))))->addr_next ==  (void *) NULL)//ok cest pas null, donc pourquoi ca le devient pendant le recyclage de bloc
                    exit(-1);
                t1024 -= (sizeof(struct bloc) + 1024);

                ++i;
                ++j1024_max;
            }

            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (1024 + sizeof(struct bloc))))->addr_next = bloc_origine_1024->adresse;
            ((struct bloc *) (bloc_origine_1024->addr_previous - sizeof(struct bloc)))->addr_next = tmp_bloc->adresse;
            bloc_origine_1024->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (1024 + sizeof(struct bloc))))->adresse;
            next1024 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;

            pthread_mutex_unlock(&mutex_malloc);
            return (tmp_bloc->adresse);
        }


            //au dessus on prend le risque d'allouer des gros blocs pour rien ! ON laisse ou bien si on a pas trouver un bloc libre de la bonne taille, on fait des nouveaux bloc ici
        else // si on est la c'est qu'il n'y a plus aucun bloc disponible, on pourrait faire un nouveau gros mmap() (si size est < 1024 sinon mmpa)
        // et rajouter des blocs dans les bloc_origines_xxxx, e, prenant garde a ne pas depasser les capacites de la machine
        {
#ifdef COUNTER
            nb_mmap += 1;
#endif
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
    //printf("entree dans free :");
    pthread_mutex_lock(&mutex_free);
#ifdef COUNTER
    nb_free += 1;
#endif
    //printf("entre dans le free ptr_addr = %p\n",ptr); // parfois le ptr est NULL ou plutot nil ! c'est ca le blem !
    if(ptr == NULL)
    {
        //printf("NULL pointeur free()\n ");
        pthread_mutex_unlock(&mutex_free);
        return (void)0;
    }
    struct bloc* tmp_bloc = (void*)ptr - sizeof(struct bloc); //des fois, ça crée une segfault SIGSEGV
    tmp_bloc->numero=0;
    //printf("j64_max :  %u    j128_max :  %u  j256_max : %u  j512_max : %u   j1024_max : %u\n",j64_max,j128_max,j256_max,j512_max,j1024_max);
    //printf("taille : %d",tmp_bloc->taille);0
    //printf("taille + 40 = %u\n",tmp_bloc->taille);
    if(tmp_bloc->taille > (1024 + sizeof(struct bloc)) )
    {
        //printf("avant munmap\n");
#ifdef COUNTER
        nb_munmap += 1;
#endif
        munmap(tmp_bloc, tmp_bloc->taille);
        pthread_mutex_unlock(&mutex_free);
        return (void)0;
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
#ifdef COUNTER
    nb_calloc += 1;
#endif
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
#ifdef COUNTER
    nb_realloc += 1;
#endif

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
    //si on realloc une taille plus petite, inutile de changer de zone memoire et de recopier, on change juste la metadonnee
    for(int i = 0 ; i < min ; i++)
    {
        ((char*)ret)[i] = ((char*)ptr)[i];
    }
    //memcpy(ret, ptr, size);
    //printf("sortie realloc\n");
    pthread_mutex_unlock(&mutex_realloc);

    return ret;
}


static pthread_mutex_t mutex_reallocarray = PTHREAD_MUTEX_INITIALIZER;
void* reallocarray(void* ptr, size_t nmemb, size_t size)
{
    pthread_mutex_lock(&mutex_reallocarray);
    void* ret = realloc(ptr, nmemb * size);
    pthread_mutex_unlock(&mutex_reallocarray);
    return ret;
}

void get_stat() __attribute__((destructor));
//void print_hello() __attribute__((constructor));

void get_stat()
{
    printf("nb_malloc : %llu\n",nb_malloc);
    printf("nb_free : %llu\n",nb_free);
    printf("nb_realloc : %llu\n",nb_realloc);
    printf("nb_calloc : %llu\n",nb_calloc);
    printf("nb_recyclage : %llu\n",nb_recyclage);
    printf("nb_mmap : %llu\n",nb_mmap);
    printf("nb_munmap : %llu\n",nb_munmap);
    return (void)0;
}

void print_hello()
{
    printf("hello !\n");
}