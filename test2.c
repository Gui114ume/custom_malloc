#include <stddef.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


int main(){

    int* p = NULL;
    int i = 0;
    int t;
    while( i < 1000)
    {
        t = (int)(rand()%256 + 1);
        p = malloc( t );
        if(p == NULL)
            abort();
        p = calloc( rand() % 2 + 1, t); // calloc avec realloc cree une erreur type 256 = NULL, mais un seul des deux à la fois fonctionne parfaitement !!
        // calloc a renvoyé un null
        if(p == NULL)
            //exit(-1);
            abort();
        p = realloc(p, t);
        if(p ==NULL)
            abort();

        printf("addr :%p , taille = %d\n",p,t);
        //exit(-1);
        free(p);
        i++;
    }

}