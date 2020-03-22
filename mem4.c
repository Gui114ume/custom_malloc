/*
 * Implementation d'une librairie d'allocation memoire. Le but est de pouvoir l'interposer avec le malloc
 * de la lib standart a l'aide d'un LD_PRELOAD
 *
 * La tolérance change drastiquement les perfs et peux supprimer le "PROCESSUS ARRETÉ"
 *
 * Il faudrait tester les effets de a tolérance et les effets de t = 2 << 25, je pense que plus t est grand, plus la tolerance doit etre elevé ! Sinon gachi de memoire extreme
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
    unsigned int numero;// 0 -> libre , 1 -> occupé
    unsigned int taille; //c'est peut être beaucoup, on pourrait utiliser le premier bit au lieu de "int numero" pas important pour l'instant
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

static int sizeof_struct_bloc  = 40;



struct bloc** bloc_origine = NULL; //on doit stocker NULL dans la derniere case, on commencera par faire bloc_origine = mmap( 1 page ), c'est suffisant pour stocker , supposons pour l'instant
static unsigned int nb_de_bloc = 0; // sera utilisé pour rechercher des zones memoires à renvoyer a l'user et savoir quand s'arreter de chercher  ! ;)

static unsigned int* tx = NULL;

void** next = NULL;
static unsigned int nb_de_next = 0;

static int    indice = 0;

static unsigned int* jx_max = NULL;

pthread_mutex_t* tab_mutex_malloc = NULL;
static unsigned int nb_de_mutex = 0;

static pthread_mutex_t mutex_malloc = PTHREAD_MUTEX_INITIALIZER;

struct bloc** tmp_bloc_global = NULL;

int cherche_liste_chaine( struct bloc** bloc_origine_f,
                          unsigned int* nb_de_bloc_f,
                          size_t size,
                          int tolerance )
{ // trouver un meilleur algo ! Cette fonction est utilisé à chaque malloc, ça ralenti l'execution et crée des bugs ( gedit , vlc)
    int ret = -1;
    for(int i = 0 ; i < *nb_de_bloc_f ; i++)
    {
        if(  (  (bloc_origine_f[i]->taille - sizeof_struct_bloc - size) <= tolerance ) && (size <= bloc_origine_f[i]->taille - sizeof_struct_bloc)   )
        {
            ret = i;
            break;
        }
    }
    return ret;
}

void creer_liste_chaine(struct bloc** bloc_origine_f,
                        unsigned int* nb_de_bloc_f,
                        unsigned int* tx_f,
                        unsigned int* jx_max_f,
                        size_t size)
{
    tx_f[*nb_de_bloc_f] = 2 << 25; // 2^25
    // on ne peut pas faire ça sinon on perd ce a été mmap auparavant, il faut créer un pointeur temporaire
#ifdef COUNTER
    nb_mmap += 1;
#endif
    bloc_origine_f[*nb_de_bloc_f] = mmap(NULL, tx_f[*nb_de_bloc_f], PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    int sizeof_bloc_and_writable_space = size + sizeof_struct_bloc;

    int i = 0;
    while( tx_f[*nb_de_bloc_f] > sizeof_struct_bloc + size )// il y a forcement un probleme de remplissage, comment peut il y avoir des adresse = NULL alors que l'algo est celui ci-dessous ?
        //non rien  ici !!
    {
        ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * (sizeof_bloc_and_writable_space  )))->adresse       = ((void*)bloc_origine_f[*nb_de_bloc_f] + i * ( sizeof_bloc_and_writable_space )) + sizeof_struct_bloc;//adresse decallé de la taille des metadonnees
        ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * ( sizeof_bloc_and_writable_space )))->addr_previous =  ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * (
                sizeof_bloc_and_writable_space) ))->adresse - sizeof_bloc_and_writable_space;
        ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * ( sizeof_bloc_and_writable_space )))->addr_next     = ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * (sizeof_bloc_and_writable_space )))->adresse + sizeof_bloc_and_writable_space;
        ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * ( sizeof_bloc_and_writable_space )))->taille = sizeof_bloc_and_writable_space;
        ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + i * ( sizeof_bloc_and_writable_space )))->numero = 0;


        tx_f[*nb_de_bloc_f] -= (sizeof_struct_bloc + size);

        ++i;
        ++jx_max_f[*nb_de_bloc_f];
    }
    // on ferme l'anneau, mince comment faire ? On considere ici la création d'un nouvel anneau, pas l'agrandissment. Ce sera pour une autre fonction l'agrandissement.

    ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + (i - 1)* ( sizeof_bloc_and_writable_space )))->addr_next = bloc_origine_f[*nb_de_bloc_f]->adresse;     // queue ---> tete
    bloc_origine_f[*nb_de_bloc_f]->addr_previous = ((struct bloc*)((void*)bloc_origine_f[*nb_de_bloc_f] + (i - 1)* ( sizeof_bloc_and_writable_space )))->adresse; // tete  ---> queue

    return (void)0;
}

void fusionne_anneaux(struct bloc* bloc_origine_f,
                      struct bloc* nouveau_bloc)
{
    // faire un anneau à partir de deux anneaux passés en paramètres
    ((struct bloc*)(nouveau_bloc->addr_previous - sizeof_struct_bloc))->addr_next = bloc_origine_f->adresse;
    ((struct bloc*)(bloc_origine_f->addr_previous - sizeof_struct_bloc))->addr_next = nouveau_bloc->adresse;
    bloc_origine_f->addr_previous = ((struct bloc*)(nouveau_bloc->addr_previous - sizeof_struct_bloc))->adresse;

    return (void)0;
}

void agrandi_liste_chaine( struct bloc** bloc_origine_f,
                           unsigned int* nb_de_bloc_f,
                           unsigned int* tx_f,
                           unsigned int* jx_max_f,
                           size_t size,
                           int ret )
{
    int e = ret;
    //struct bloc** tmp_bloc = mmap(NULL, 1024 * sizeof(struct bloc*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); // tmp_bloc, nécessaire pour utiliser "creer liste chaine"
    // ç adoit faire mal aux performances, il faut le faire une seule fois et basta
    creer_liste_chaine( tmp_bloc_global, &e, tx_f, jx_max_f, size);
    // relier les deux anneaux
    fusionne_anneaux( bloc_origine_f[ret], tmp_bloc_global[ret]);
    //munmap(tmp_bloc, 1024 * sizeof(struct bloc*) );

    return (void)0;
}

void creer_mutex(pthread_mutex_t* tab_mutex_malloc_f,
                 unsigned int* nb_de_mutex_f )
{
    pthread_mutex_init( &tab_mutex_malloc_f[*nb_de_mutex_f] , NULL);
    return (void)0;
}

void* recupere_adresse_bloc(struct bloc** bloc_origine_f,
                           unsigned int* nb_de_bloc_f,
                           unsigned int* jx_max_f,
                           void** next_f,
                           int ret )
{
    void* ret_ptr_f  = NULL;
    if(ret == -1)
    {
        //on utilise pas next, inutile et n'a pas de valeur valide de toute façon
        //on utilise seulement nb_de_bloc_f
        ret_ptr_f = bloc_origine_f[*nb_de_bloc_f]->adresse;
        // pas NULL
        return ret_ptr_f;
    }
    else
    {   // j'ai l'impression que le prog ne rentre jamais ici ! on ne recycle donc jamais !
        //on utilise next, et ret
        unsigned int count = jx_max_f[ret];
        while (count >
               0// il faut sauter d'adresse en adresse sans utiliser d'indice j , sinon on ne peut pas rajouter
            //de bloc. Il faudra donc un compteur permettant d'indiquer si on a fait un tour complet des adresses ( le next du dernier pointera vers le premier, il faut stocker le nb de bloc dans une
            // variable, on peut garder j64_max et le calculer de la meme manière que maintenant dans mem2.c) si on a fini un tour sans trouver de bloc libre, on en alloue d'autre et on rempli comme il faut
            // les addr next et addr previous poiur etre comme dans un anneau!
                ) //probleeeeeeeme
        {
            if (((struct bloc *) ((void *) next_f[ret] - sizeof_struct_bloc))->numero == 0) // est-ce libre ?
            {
                ((struct bloc *) ((void *) next_f[ret] - sizeof_struct_bloc))->numero = 1; //maintenant ca l'est plus

                if (next_f[ret] ==
                    (void *) NULL)// c'est pas normal d'arriver la alors que addr_next dit que t'es pas NULL juste au dessus!!!!! resoudre ca !
                {
                    abort();
                }
#ifdef COUNTER
                nb_recyclage += 1;
#endif
                ret_ptr_f = next_f[ret];
                return ret_ptr_f;
            }
            count--;
            next_f[ret] = ((struct bloc*)((void*)next_f[ret] - sizeof_struct_bloc))->addr_next;

        }
    }
    ret_ptr_f = (void*)NULL;
    return ret_ptr_f;
}

void maj_next(void** next_f,
              unsigned int* nb_de_next_f,
              int ret, // -1 ou bien l'indice du tableau de next*
              void* ret_ptr) //ret_ptr contient l'adresse d'une zone inscriptible qui va être renvoyé à l'user
{
    if(ret == -1)
    {
        next_f[*nb_de_next_f] = ((struct bloc*)(ret_ptr - sizeof_struct_bloc))->addr_next;
    }
    else
    {
        next_f[ret] = ((struct bloc*)(ret_ptr - sizeof_struct_bloc))->addr_next;;
    }
    return (void)0;
}

void* malloc(size_t size)
{
    pthread_mutex_lock(&mutex_malloc);

#ifdef COUNTER
    nb_malloc += 1;
#endif

    if (indice == 0)
    {
        // reserver de l'espace pour stocker les blocs, les mutex, donc de quoi stocker 1024 éléments de type struct bloc* par exemple etc
        indice = 1;
        //ets-ce que ça prends beaucoup de mémoire tout ça ?

        bloc_origine = mmap(NULL, 1024 * sizeof(struct bloc*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        tab_mutex_malloc = mmap(NULL, 1024 * sizeof(pthread_mutex_t*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        next = mmap(NULL, 1024 * sizeof(void*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        tmp_bloc_global = mmap(NULL, 1024 * sizeof(struct bloc*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); // tmp_bloc, nécessaire pour utiliser "creer liste chaine"


        tx = mmap(NULL, 1024 * sizeof(unsigned int*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        jx_max = mmap(NULL, 1024 * sizeof(unsigned int*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    void* ret_ptr = NULL;

    if(size > 1024)
    {
        void* p = mmap(NULL, size + sizeof_struct_bloc, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        ((struct bloc*)p)->adresse       = p + sizeof_struct_bloc;
        ((struct bloc*)p)->addr_previous = NULL;
        ((struct bloc*)p)->addr_next     = NULL;
        ((struct bloc*)p)->taille        = size + sizeof_struct_bloc;
        ((struct bloc*)p)->numero        = 0;
        pthread_mutex_unlock(&mutex_malloc);
        return (  ((struct bloc*)p)->adresse );
    }

    int tolerance = 100; // 4 octets de tolerance

    // ATTENTION AU RACE CONDITIONS SUR LES VARIABLES GLOBALES nb_de_xxx, ca risque de poser probleme...

    int ret = cherche_liste_chaine( bloc_origine, &nb_de_bloc, size, tolerance ); // renvoie -1 tout le temps

    if( ret == -1 ) // pas de liste chainee compatible
    {
        int local_nb_de_bloc  = nb_de_bloc;
        int local_nb_de_mutex = nb_de_mutex;
        int local_nb_de_next  = nb_de_next;

        nb_de_next++;
        nb_de_mutex++;
        nb_de_bloc++;
        //faire des copies locales des nb_de_xxx sinon race condition !  IMPORTANT
        //incrementer les nb_de_xxx
        creer_liste_chaine( bloc_origine, &local_nb_de_bloc, tx, jx_max, size ); //modifie jx_max

        creer_mutex( tab_mutex_malloc, &local_nb_de_mutex );

        pthread_mutex_lock( &tab_mutex_malloc[nb_de_mutex] );
        pthread_mutex_unlock(&mutex_malloc);

        ret_ptr = recupere_adresse_bloc( bloc_origine, &local_nb_de_bloc, jx_max, next, ret);
        //ret_ptr = NULL probleme !
        maj_next( next, &local_nb_de_next, ret, ret_ptr);


        pthread_mutex_unlock( &tab_mutex_malloc[local_nb_de_mutex] );
        return ret_ptr;
    }
    else // ret est tel que : bloc_origine[ ret ] , next[ ret ] , tab_mutex_malloc[ ret ]
    {
        int local_nb_de_bloc  = nb_de_bloc;
        int local_nb_de_mutex = nb_de_mutex;
        int local_nb_de_next  = nb_de_next;


        pthread_mutex_lock( &tab_mutex_malloc[ret] );
        pthread_mutex_unlock(&mutex_malloc);

        ret_ptr = recupere_adresse_bloc( bloc_origine, &local_nb_de_bloc, jx_max, next, ret); // lock le mutex correspondant a la size demandé, puis unlock mutex_malloc
        if(ret_ptr == NULL) //signifie que la liste est valide mais qu'il n'y a pas de place libre
        {
            //on fait du first fit : donc on ne cherche pas une place libre ailleurs, on rallonge la liste chainée. ON ne cree pas de bloc
            agrandi_liste_chaine( bloc_origine, &local_nb_de_bloc, tx, jx_max, size, ret ); // utilise creer_liste_chaine + relie les deux anneaux, modifie jx_max
            ret_ptr = recupere_adresse_bloc( bloc_origine, &local_nb_de_bloc, jx_max, next, ret);
            maj_next( next, &local_nb_de_next, ret, ret_ptr);
            pthread_mutex_unlock( &tab_mutex_malloc[ret] );
            return ret_ptr;
        }
        maj_next( next, &local_nb_de_mutex, ret, ret_ptr);
        pthread_mutex_unlock( &tab_mutex_malloc[ret] );
        return ret_ptr;
    }


}

static pthread_mutex_t mutex_free = PTHREAD_MUTEX_INITIALIZER;

void free(void* ptr)//que se passe t il si quelqu'un utilise beaucoup de bloc de taille < 1024 puis plus rien ? On aurait de la memoire mappé, inutilisé et inutilisable par d'autres processus
{
    //printf("indice : %d\n",indice);
    //printf("addr de bloc origine 64 : %p\n",bloc_origine_64);
    //printf("entree dans free :");
    //printf("%u %u %u %u %u\n",j64_max,j128_max,j256_max,j512_max,j1024_max);
    //printf("malloc %llu\n",nb_malloc);
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
    struct bloc* tmp_bloc = (void*)ptr - sizeof_struct_bloc; //des fois, ça crée une segfault SIGSEGV
    tmp_bloc->numero=0;
    //printf("j64_max :  %u    j128_max :  %u  j256_max : %u  j512_max : %u   j1024_max : %u\n",j64_max,j128_max,j256_max,j512_max,j1024_max);
    //printf("taille : %d",tmp_bloc->taille);0
    //printf("taille + 40 = %u\n",tmp_bloc->taille);
    if(tmp_bloc->taille > (sizeof_struct_bloc + 1024 ))
    {
        //printf("avant munmap\n");
#ifdef COUNTER
        nb_munmap += 1;
#endif
        munmap(tmp_bloc, tmp_bloc->taille); //on munmap ou on recyle i.e on agrandi les listes chainees deja existantes ?  Dans tout les cas, inutile de faire une liste de hugeblock
        // A FAIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIRE
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

    int min = MIN( size, ((struct bloc*)((void*)ptr - sizeof_struct_bloc))->taille - sizeof_struct_bloc  );  // gerer le cas ou l'user met une size trop grande auquel cas il ne faut pas faire de segfaul// t
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
    printf("nb_de_bloc : %u\n",nb_de_bloc);
    printf("nb_de_mutex : %u\n",nb_de_mutex);
    printf("nb_de_next : %u\n",nb_de_next);
    return (void)0;
}

void print_hello()
{
    printf("hello !\n");
}
