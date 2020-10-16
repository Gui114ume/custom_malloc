/*
 * Implementation d'une librairie d'allocation memoire. Le but est de pouvoir l'interposer avec les fonctions de gestion de la mémoire de la lib standard a l'aide d'un LD_PRELOAD
 */

#include "mem.h"

#define MIN(a , b) ( ( (a) < (b) ) ? (a) : (b) )

#define COUNTER

#ifdef COUNTER
static unsigned long long nb_malloc    = 0;
static unsigned long long nb_free      = 0;
static unsigned long long nb_calloc    = 0;
static unsigned long long nb_realloc   = 0;
static unsigned long long nb_mmap      = 0;
static unsigned long long nb_munmap    = 0;
static unsigned long long nb_recyclage = 0;
#endif

static int sizeof_struct_bloc;                 // = 8 //sizeof(struct bloc);
static int sizeof_bloc_and_writable_space_64;  // = 64   + sizeof_struct_bloc;
static int sizeof_bloc_and_writable_space_128; // = 128  + sizeof_struct_bloc;
static int sizeof_bloc_and_writable_space_256; // = 256  + sizeof_struct_bloc;
static int sizeof_bloc_and_writable_space_512; // = 512  + sizeof_struct_bloc;
static int sizeof_bloc_and_writable_space_1024;// = 1024 + sizeof_struct_bloc;

struct bloc* bloc_origine_64   = NULL;
struct bloc* bloc_origine_128  = NULL;
struct bloc* bloc_origine_256  = NULL;
struct bloc* bloc_origine_512  = NULL;
struct bloc* bloc_origine_1024 = NULL;
struct bloc* bloc_origine_huge = NULL;


static unsigned int t64   = 0;
static unsigned int t128  = 0;
static unsigned int t256  = 0;
static unsigned int t512  = 0;
static unsigned int t1024 = 0;


void* next64   = NULL;
void* next128  = NULL;
void* next256  = NULL;
void* next512  = NULL;
void* next1024 = NULL;

static int    indice = 0;

static unsigned int j64_max   = 0;
static unsigned int j128_max  = 0;
static unsigned int j256_max  = 0;
static unsigned int j512_max  = 0;
static unsigned int j1024_max = 0;

static pthread_mutex_t mutex_malloc      = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_malloc_64   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_malloc_128  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_malloc_256  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_malloc_512  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_malloc_1024 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_malloc_huge = PTHREAD_MUTEX_INITIALIZER;


void* malloc(size_t size)
{
    pthread_mutex_lock(&mutex_malloc);
#ifdef COUNTER
    nb_malloc += 1;
#endif
    
    void* ret_ptr64   = NULL;
    void* ret_ptr128  = NULL;
    void* ret_ptr256  = NULL;
    void* ret_ptr512  = NULL;
    void* ret_ptr1024 = NULL;

    void* ret_ptr_huge = NULL;

    if(indice == 0)
    {
        sizeof_struct_bloc = sizeof(struct bloc);
        sizeof_bloc_and_writable_space_64   = 64   + sizeof_struct_bloc;
        sizeof_bloc_and_writable_space_128  = 128  + sizeof_struct_bloc;
        sizeof_bloc_and_writable_space_256  = 256  + sizeof_struct_bloc;
        sizeof_bloc_and_writable_space_512  = 512  + sizeof_struct_bloc;
        sizeof_bloc_and_writable_space_1024 = 1024 + sizeof_struct_bloc;
        indice =1;
	
        //creation de la liste chainé a l'aide d'un gros mmap
        t64   = 2 << 25;
        t128  = 2 << 25;  
        t256  = 2 << 25;
        t512  = 2 << 25;
        t1024 = 2 << 25;

#ifdef COUNTER
        nb_mmap += 5;
#endif
        ret_ptr64  = mmap(NULL, t64 , PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr128 = mmap(NULL, t128, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr256 = mmap(NULL, t256, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr512 = mmap(NULL, t512, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ret_ptr1024 = mmap(NULL, t1024, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);



        bloc_origine_64   = ret_ptr64;
        bloc_origine_128  = ret_ptr128;
        bloc_origine_256  = ret_ptr256;
        bloc_origine_512  = ret_ptr512;
        bloc_origine_1024 = ret_ptr1024;
        bloc_origine_huge = ret_ptr_huge;

        int i = 0; //
        while( t64 > sizeof_struct_bloc + 64 )
        {
            ((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->adresse       = ((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->addr_previous =  ((struct bloc*)((void*)bloc_origine_64 + i * (64 +
                sizeof_struct_bloc) ))->adresse - 64 - sizeof_struct_bloc;  // on ne met plus de null, on est maintenant sur un anneau
            ((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->addr_next     = ((struct bloc*)((void*)bloc_origine_64 + i * (sizeof_bloc_and_writable_space_64 )))->adresse + sizeof_struct_bloc + 64;
            ((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->taille = sizeof_bloc_and_writable_space_64;
            ((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->numero = 0;

            if(((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->adresse  == (void*)NULL)
                exit(-1);
            if(((struct bloc*)((void*)bloc_origine_64 + i * ( sizeof_bloc_and_writable_space_64 )))->addr_next == (void*)NULL  )
                exit(-1);
            t64 -= (sizeof_struct_bloc + 64);
            ++i;
        }
        j64_max = i;

        ((struct bloc*)((void*)bloc_origine_64 + (i - 1)* ( sizeof_bloc_and_writable_space_64 )))->addr_next     = bloc_origine_64->adresse;
        bloc_origine_64->addr_previous = ((struct bloc*)((void*)bloc_origine_64 + (i - 1)* ( sizeof_bloc_and_writable_space_64 )))->adresse;

        i = 0;

        while( t128 > sizeof_struct_bloc + 128 )
        {
            ((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->adresse      = ((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->addr_previous =  ((struct bloc*)((void*)bloc_origine_128 + i * (128 +
                sizeof_struct_bloc) ))->adresse - 128 - sizeof_struct_bloc;
            ((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->addr_next     =  ((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128) ))->adresse + sizeof_struct_bloc + 128;
            ((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->taille = sizeof_bloc_and_writable_space_128;
            ((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->numero = 0;

            if(((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->adresse  == (void*)NULL) 
                exit(-1);
            if(((struct bloc*)((void*)bloc_origine_128 + i * ( sizeof_bloc_and_writable_space_128 )))->addr_next == (void*)NULL  )
                exit(-1);
            t128 -= (sizeof_struct_bloc + 128);
            ++i;
        }

        j128_max = i;
        ((struct bloc*)((void*)bloc_origine_128 + (i - 1)* ( sizeof_bloc_and_writable_space_128 )))->addr_next     = bloc_origine_128->adresse;
        bloc_origine_128->addr_previous = ((struct bloc*)((void*)bloc_origine_128 + (i - 1)* ( sizeof_bloc_and_writable_space_128 )))->adresse;


        i = 0;

        while( t256 > sizeof_struct_bloc + 256 )
        {
            ((struct bloc*)((void*)bloc_origine_256 + i * ( sizeof_bloc_and_writable_space_256 )))->adresse       = ((void*)bloc_origine_256 + i * ( sizeof_bloc_and_writable_space_256 )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_256 + i * ( sizeof_bloc_and_writable_space_256 )))->addr_previous =  ((struct bloc*)((void*)bloc_origine_256 + i * ( 256 +
                sizeof_struct_bloc) ))->adresse - 256 - sizeof_struct_bloc;
            ((struct bloc*)((void*)bloc_origine_256 + i * ( sizeof_bloc_and_writable_space_256 )))->addr_next     =((struct bloc*)((void*)bloc_origine_256 + i * (sizeof_bloc_and_writable_space_256) ))->adresse + sizeof_struct_bloc + 256;
            ((struct bloc*)((void*)bloc_origine_256 + i * ( sizeof_bloc_and_writable_space_256 )))->taille = sizeof_bloc_and_writable_space_256;
            ((struct bloc*)((void*)bloc_origine_256 + i * ( sizeof_bloc_and_writable_space_256 )))->numero = 0;

            t256 -= (sizeof_struct_bloc + 256);
            ++i;
        }


        j256_max = i;
        ((struct bloc*)((void*)bloc_origine_256 + (i - 1)* ( sizeof_bloc_and_writable_space_256 )))->addr_next     = bloc_origine_256->adresse;
        bloc_origine_256->addr_previous = ((struct bloc*)((void*)bloc_origine_256 + (i - 1)* ( sizeof_bloc_and_writable_space_256 )))->adresse;
        i = 0;

        while( t512 > sizeof_struct_bloc + 512 )
        {
            ((struct bloc*)((void*)bloc_origine_512 + i * ( sizeof_bloc_and_writable_space_512 )))->adresse       = ((void*)bloc_origine_512 + i * ( sizeof_bloc_and_writable_space_512 )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_512 + i * ( sizeof_bloc_and_writable_space_512 )))->addr_previous =  ((struct bloc*)((void*)bloc_origine_512 + i * (512 +
                sizeof_struct_bloc) ))->adresse - 512 - sizeof_struct_bloc;
            ((struct bloc*)((void*)bloc_origine_512 + i * ( sizeof_bloc_and_writable_space_512 )))->addr_next     = ((struct bloc*)((void*)bloc_origine_512 + i * (sizeof_bloc_and_writable_space_512) ))->adresse + sizeof_struct_bloc + 512;
            ((struct bloc*)((void*)bloc_origine_512 + i * ( sizeof_bloc_and_writable_space_512 )))->taille = sizeof_bloc_and_writable_space_512;
            ((struct bloc*)((void*)bloc_origine_512 + i * ( sizeof_bloc_and_writable_space_512 )))->numero = 0;

            t512 -= (sizeof_struct_bloc + 512);
            ++i;
        }
        j512_max = i;
        ((struct bloc*)((void*)bloc_origine_512 + (i - 1)* ( sizeof_bloc_and_writable_space_512 )))->addr_next     = bloc_origine_512->adresse;
        bloc_origine_512->addr_previous = ((struct bloc*)((void*)bloc_origine_512 + (i - 1)* ( sizeof_bloc_and_writable_space_512 )))->adresse;
        i=0;


        while( t1024 > sizeof_struct_bloc + 1024 )
        {
            ((struct bloc*)((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )))->adresse       = ((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )))->addr_previous =  ((struct bloc*)((void*)bloc_origine_1024 + i * (1024 +
                            sizeof_struct_bloc) ))->adresse - 1024 - sizeof_struct_bloc;
            ((struct bloc*)((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )))->addr_next     = ((struct bloc*)((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )))->adresse + sizeof_struct_bloc + 1024;
            ((struct bloc*)((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )))->taille =sizeof_bloc_and_writable_space_1024;
            ((struct bloc*)((void*)bloc_origine_1024 + i * (sizeof_bloc_and_writable_space_1024 )))->numero = 0;

            t1024 -= (sizeof_struct_bloc + 1024);
            ++i;
        }

        j1024_max = i;
        ((struct bloc*)((void*)bloc_origine_1024 + (i - 1)* (sizeof_bloc_and_writable_space_1024 )))->addr_next     = bloc_origine_1024->adresse;
        bloc_origine_1024->addr_previous = ((struct bloc*)((void*)bloc_origine_1024 + (i - 1)* (sizeof_bloc_and_writable_space_1024 )))->adresse;
        i = 0;

        if( size < 64 )
        {
            ret_ptr64 = bloc_origine_64->adresse;
            bloc_origine_64->numero = 1;
            if(ret_ptr64 == (void*)NULL)
            {
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

        else
        {
#ifdef COUNTER
            nb_mmap += 1;
#endif
            ret_ptr_huge = mmap(NULL, sizeof_struct_bloc + size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            bloc_origine_huge = ret_ptr_huge;
            bloc_origine_huge->numero = 1;
            bloc_origine_huge->adresse = ret_ptr_huge + sizeof_struct_bloc;
            bloc_origine_huge->addr_previous = NULL;
            bloc_origine_huge->addr_next = NULL;
            bloc_origine_huge->taille = size + sizeof_struct_bloc;

            next128 = bloc_origine_128->adresse;
            next256 = bloc_origine_256->adresse;
            next512= bloc_origine_512->adresse;
            next1024 = bloc_origine_1024->adresse;
            next64 = bloc_origine_64->adresse;

            if( bloc_origine_huge->adresse == (void*)NULL)
            {
	      abort();
            }
            pthread_mutex_unlock(&mutex_malloc);
            return ((struct bloc*)ret_ptr_huge)->adresse;
        }

    }
    else
    {
        pthread_mutex_unlock(&mutex_malloc);


        if (size < 64 ) {
            pthread_mutex_lock(&mutex_malloc_64);
            unsigned int count_64 = j64_max;

            while (count_64 > 0) 
            {
                if (((struct bloc *) ((void *) next64 - sizeof_struct_bloc))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next64 - sizeof_struct_bloc))->numero = 1; //maintenant ca l'est plus

                    if (next64 == (void *) NULL)
                    {
                        pthread_mutex_unlock(&mutex_malloc_64);
                        abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next64;
                    next64 = ((struct bloc *) ((void *) next64 - sizeof_struct_bloc))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc_64);
                    return ret;
                }
                count_64--;
                next64 = ((struct bloc*)((void*)next64 - sizeof_struct_bloc))->addr_next;
            }
        }

        if(size < 64 )
        {
        int i = 0;
#ifdef COUNTER
        nb_mmap += 1;
#endif
        t64 = 2 << 25;
        struct bloc* tmp_bloc = mmap(NULL, t64,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        while( t64 > sizeof_struct_bloc + 64 )
        {
            ((struct bloc*)((void*)tmp_bloc + i * (sizeof_bloc_and_writable_space_64 )))->adresse        = ((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
            ((struct bloc*)((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )))->addr_previous =  ((struct bloc*)((void*)tmp_bloc + i * (64 +
                            sizeof_struct_bloc) ))->adresse - 64 - sizeof_struct_bloc;
            ((struct bloc*)((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )))->addr_next     = ((struct bloc*)((void*)tmp_bloc + i * (sizeof_bloc_and_writable_space_64 )))->adresse + sizeof_struct_bloc + 64;
            ((struct bloc*)((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )))->taille = sizeof_bloc_and_writable_space_64;
            ((struct bloc*)((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )))->numero = 0;

            if(((struct bloc*)((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )))->adresse  == (void*)NULL)
                exit(-1);
            if(((struct bloc*)((void*)tmp_bloc + i * ( sizeof_bloc_and_writable_space_64 )))->addr_next == (void*)NULL  )
                exit(-1);
            t64 -= (sizeof_struct_bloc + 64);

            ++i;
            ++j64_max;
        }

        tmp_bloc->addr_previous = bloc_origine_64->addr_previous;
	
        ((struct bloc*)((void*)tmp_bloc + (i - 1) * ( sizeof_bloc_and_writable_space_64) ))->addr_next     = bloc_origine_64->adresse;

        ((struct bloc*)(bloc_origine_64->addr_previous - sizeof_struct_bloc) )->addr_next        = tmp_bloc->adresse;
        bloc_origine_64->addr_previous = ((struct bloc*)((void*)tmp_bloc + (i - 1)* ( sizeof_bloc_and_writable_space_64 )))->adresse;

        next64 = tmp_bloc->addr_next;
        tmp_bloc->numero = 1;

        pthread_mutex_unlock(&mutex_malloc_64);


        return (tmp_bloc->adresse);
        }



        if (size < 128 ) { 
            pthread_mutex_lock(&mutex_malloc_128);
            unsigned int count_128 = j128_max;
            while (count_128 > 0)
            {
                if (((struct bloc *) ((void *) next128 - sizeof_struct_bloc))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next128 - sizeof_struct_bloc))->numero = 1; //maintenant ca l'est plus

                    if (next128 == (void *) NULL)
		      {
			pthread_mutex_unlock(&mutex_malloc_128);
			abort();
		      }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next128;
                    next128 = ((struct bloc *) ((void *)next128 - sizeof_struct_bloc))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc_128);
                    return ret;
                }
                count_128--;
                next128 = ((struct bloc*)((void*)next128 - sizeof_struct_bloc))->addr_next;

            }
        }

        if(size < 128 )
        {

            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            t128 = 2 << 25;
            struct bloc *tmp_bloc = mmap(NULL, t128,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t128 > sizeof_struct_bloc + 128) {
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->adresse = ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->addr_previous =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->adresse - 128 - sizeof_struct_bloc;  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->adresse + sizeof_struct_bloc + 128;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->taille = sizeof_bloc_and_writable_space_128;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->adresse == (void *) NULL) 
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_128)))->addr_next ==  (void *) NULL)
                    exit(-1);
                t128 -= (sizeof_struct_bloc + 128);

                ++i;
                ++j128_max;
            }

            tmp_bloc->addr_previous = bloc_origine_128->addr_previous;
            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_128)))->addr_next = bloc_origine_128->adresse;
            ((struct bloc *) (bloc_origine_128->addr_previous - sizeof_struct_bloc))->addr_next = tmp_bloc->adresse;
            bloc_origine_128->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_128)))->adresse;
            next128 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;

            pthread_mutex_unlock(&mutex_malloc_128);
            return (tmp_bloc->adresse);
        }


        if (size < 256 ) { 
            pthread_mutex_lock(&mutex_malloc_256);
            unsigned int count_256 = j256_max;
            while (count_256 > 0)
            {
                if (((struct bloc *) ((void *) next256 - sizeof_struct_bloc))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next256 - sizeof_struct_bloc))->numero = 1; //maintenant ca l'est plus

                    if (next256 == (void *) NULL)
		      {
                        pthread_mutex_unlock(&mutex_malloc_256);
			abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next256;
                    next256 = ((struct bloc *) ((void *) next256 - sizeof_struct_bloc))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc_256);
                    return ret;
                }
                count_256--;
                next256 = ((struct bloc*)((void*)next256 - sizeof_struct_bloc))->addr_next;

            }
        }

        if(size < 256)
        {
            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            t256 = 2 << 25;
            struct bloc *tmp_bloc = mmap(NULL, t256,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t256 > sizeof_struct_bloc + 256)
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->adresse = ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->addr_previous =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->adresse - 256 - sizeof_struct_bloc;  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->adresse + sizeof_struct_bloc + 256;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->taille = sizeof_bloc_and_writable_space_256;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->adresse == (void *) NULL)
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_256)))->addr_next ==  (void *) NULL)
                    exit(-1);
                t256 -= (sizeof_struct_bloc + 256);

                ++i;
                ++j256_max;
            }

            tmp_bloc->addr_previous = bloc_origine_256->addr_previous;
            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_256)))->addr_next = bloc_origine_256->adresse;
            ((struct bloc *) (bloc_origine_256->addr_previous - sizeof_struct_bloc))->addr_next = tmp_bloc->adresse;
            bloc_origine_256->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_256)))->adresse;
            next256 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;
            pthread_mutex_unlock(&mutex_malloc_256);
            return (tmp_bloc->adresse);
        }




        if (size < 512 ) { 
            pthread_mutex_lock(&mutex_malloc_512);
            unsigned int count_512 = j512_max;

            while (count_512 > 0)
            {
                if (((struct bloc *) ((void *) next512 - sizeof_struct_bloc))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next512 - sizeof_struct_bloc))->numero = 1; //maintenant ca l'est plus

                    if (next512 == (void *) NULL)
                    {
                        pthread_mutex_unlock(&mutex_malloc_512);
			abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next512;
                    next512 = ((struct bloc *) ((void *) next512 - sizeof_struct_bloc))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc_512);
                    return ret;
                }
                count_512--;
                next512 = ((struct bloc*)((void*)next512 - sizeof_struct_bloc))->addr_next;

            }
        }

        if(size < 512 )
        {
            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            t512 = 2 << 25;
            struct bloc *tmp_bloc = mmap(NULL, t512, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t512 > sizeof_struct_bloc + 512)
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->adresse = ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->addr_previous =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->adresse - 512 - sizeof_struct_bloc;  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->adresse + sizeof_struct_bloc + 512;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->taille = sizeof_bloc_and_writable_space_512;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->adresse == (void *) NULL) 
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_512)))->addr_next ==  (void *) NULL)
                    exit(-1);
                t512 -= (sizeof_struct_bloc + 512);

                ++i;
                ++j512_max;
            }


            tmp_bloc->addr_previous = bloc_origine_512->addr_previous;
            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_512)))->addr_next = bloc_origine_512->adresse;
            ((struct bloc *) (bloc_origine_512->addr_previous - sizeof_struct_bloc))->addr_next = tmp_bloc->adresse;
            bloc_origine_512->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_512)))->adresse;
            next512 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;

            pthread_mutex_unlock(&mutex_malloc_512);
            return (tmp_bloc->adresse);
        }



        if (size < 1024 )
        {
            pthread_mutex_lock(&mutex_malloc_1024);
            unsigned int count_1024 = j1024_max;

            while (count_1024 > 0 )
            {
                if (((struct bloc *) ((void *) next1024 - sizeof_struct_bloc))->numero == 0) // est-ce libre ?
                {
                    ((struct bloc *) ((void *) next1024 - sizeof_struct_bloc))->numero = 1; //maintenant ca l'est plus
                    if (next1024 == (void *) NULL)
                    {
		      pthread_mutex_unlock(&mutex_malloc_1024);
		      abort();
                    }
#ifdef COUNTER
                    nb_recyclage += 1;
#endif
                    void *ret = next1024;
                    next1024 = ((struct bloc *) ((void *) next1024 - sizeof_struct_bloc))->addr_next;
                    pthread_mutex_unlock(&mutex_malloc_1024);
                    return ret;
                }
                count_1024--;
                next1024 = ((struct bloc*)((void*)next1024 - sizeof_struct_bloc))->addr_next;

            }
        }

        if(size < 1024 )
        {
            int i = 0;
#ifdef COUNTER
            nb_mmap += 1;
#endif
            t1024 = 2 << 25;
            struct bloc *tmp_bloc = mmap(NULL, t1024,  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            while (t1024 > sizeof_struct_bloc + 1024)
            {
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->adresse = ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024) + sizeof_struct_bloc);//adresse decallé de la taille des metadonnees
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->addr_previous =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->adresse - 1024 - sizeof_struct_bloc;  // on ne met plus de null, on est maintenant sur un anneau
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->addr_next =  ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->adresse + sizeof_struct_bloc + 1024;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->taille =sizeof_bloc_and_writable_space_1024;
                ((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->numero = 0;

                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->adresse == (void *) NULL) 
                    exit(-1);
                if (((struct bloc *) ((void *) tmp_bloc + i * (sizeof_bloc_and_writable_space_1024)))->addr_next ==  (void *) NULL)
                    exit(-1);
                t1024 -= (sizeof_struct_bloc + 1024);

                ++i;
                ++j1024_max;
            }

            tmp_bloc->addr_previous = bloc_origine_1024->addr_previous;
            ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_1024)))->addr_next = bloc_origine_1024->adresse;
            ((struct bloc *) (bloc_origine_1024->addr_previous - sizeof_struct_bloc))->addr_next = tmp_bloc->adresse;
            bloc_origine_1024->addr_previous = ((struct bloc *) ((void *) tmp_bloc + (i - 1) * (sizeof_bloc_and_writable_space_1024)))->adresse;
            next1024 = tmp_bloc->addr_next;
            tmp_bloc->numero = 1;

            pthread_mutex_unlock(&mutex_malloc_1024);
            return (tmp_bloc->adresse);
        }


        else
        {
            pthread_mutex_lock(&mutex_malloc_huge);
#ifdef COUNTER
            nb_mmap += 1;
#endif
            ret_ptr_huge = mmap(NULL, sizeof_struct_bloc + size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            bloc_origine_huge = ret_ptr_huge;
            bloc_origine_huge->taille = size + sizeof_struct_bloc;
            bloc_origine_huge->numero=1;
            bloc_origine_huge->adresse = (void*)bloc_origine_huge + sizeof_struct_bloc;
            bloc_origine_huge->addr_previous=NULL;
            bloc_origine_huge->addr_next=NULL;
	    
            if( bloc_origine_huge->adresse == (void*)NULL)
            {
	      pthread_mutex_unlock(&mutex_malloc_huge);
	      abort();
	    }
            pthread_mutex_unlock(&mutex_malloc_huge);
            return bloc_origine_huge->adresse;
        }
    }

}

static pthread_mutex_t mutex_free = PTHREAD_MUTEX_INITIALIZER;

void free(void* ptr)
{
    pthread_mutex_lock(&mutex_free);
#ifdef COUNTER
    nb_free += 1;
#endif
    if(ptr == NULL)
    {
        pthread_mutex_unlock(&mutex_free);
        return (void)0;
    }
    struct bloc* tmp_bloc = (void*)ptr - sizeof_struct_bloc;
    tmp_bloc->numero=0;
    if(tmp_bloc->taille > (sizeof_bloc_and_writable_space_1024 ))
    {
#ifdef COUNTER
        nb_munmap += 1;
#endif
        munmap(tmp_bloc, tmp_bloc->taille);
        pthread_mutex_unlock(&mutex_free);
        return (void)0;
    }
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
    pthread_mutex_lock(&mutex_realloc);
#ifdef COUNTER
    nb_realloc += 1;
#endif

    if(ptr == (void*)NULL)
    {
        void* ret = malloc(size);
        pthread_mutex_unlock(&mutex_realloc);
        return ret;
    }
    if(size == 0 && ptr != (void*)NULL)
    {
        free(ptr);
        pthread_mutex_unlock(&mutex_realloc);
        return (void*)0;
    }
    void* ret = malloc(size);

    int min = MIN( size, ((struct bloc*)((void*)ptr - sizeof_struct_bloc))->taille - sizeof_struct_bloc  );
    
    for(int i = 0 ; i < min ; i++)
    {
        ((char*)ret)[i] = ((char*)ptr)[i];
    }
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
