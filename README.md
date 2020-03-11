Implémentation d'un allocateur mémoire
======================================

Rendez vous sur [mon github](https://github.com/Gui114ume/custom_malloc) pour avoir accès aux sources

PLAN
====

* Fonctionnement de la mémoire : point de vue utilisateur
* Description de la librairie implémentée et explication du code
* Complexité des algorithmes
* Comparaison des performances


Fonctionnement de la mémoire : point de vue utilisateur
-------------------------------------------------------


La fonction malloc ( et les autres fonctions utilisés lors de la gestion mémoire), n'est plus aussi 'magique' qu'auparavent, dès lors que l'on s'intéresse à ce qu'il se passe lors d'un appel à malloc( size ). Il faut cependant prendre le temps de comprendre quelques concepts. 

* La mémoire virtuelle

Les adresses vers lesquelles pointent les pointeurs (type* pointeur), ne sont pas les adresses physique, dans le matériel. Nous manipulons des adresses virtuelles. C'est au système d'exploitation de faire la traduction adresse_virtuelle <---> adresse_physique.

* L'allocation mémoire

Lorsque nous parlons d'allocation mémoire, nous parlons d'allocation dynamique, c'est à dire allocation à la demande de l'utilisateur. L'espace mémoire demandé est ensuite placé dans le tas. 
Même si nous avons accès uniquement à la mémoire virtuelle, c'est la mémoire physique ( RAM ) qui stocke les données que nous utilisons. Il faut donc faire ce qui s'appelle mapper de l'espace mémoire, ou réserver de l'espace mémoire. Cela est fait grâce à un appel système, mmap() dans notre cas. L'action inverse, qui libère la mémoire, est faite grâce à munmap(). Il est également possible d'utiliser brk() et sbrk() afin d'allouer de la mémoire, mais nous ne nous intéresserons pas à ces fonctions ici.

* Le recyclage de la mémoire

Chaque appel système mmap() et munmap() coûte cher en terme de performance. Les allocateurs ont donc toujours intérêt à minimiser ces appels, et à recycler les zones mémoires déjà mapper, pour gagner en vitesse d'allocation. Il convient donc de trouver des méthodes permettant de faire cela. Pour notre allocateur, nous nous sommes inspirés de la littérature sans chercher à s'y conformer complètement. Nous n'avons donc pas implémenté une des méthodes traditionnelles en particulier.


Description de la librairie implémentée et explication du code
--------------------------------------------------------------

Notre librairie implémente plusieurs fonctions de gestion de la mémoire au niveau utilisateur.

* malloc()
* calloc()
* realloc()
* reallocarray()
* free()

Elle dispose également d'une fonction get_stat(), permettant de connaitre les informations suivante:

* nombre de malloc() utilisé
* nombre de free() utilisé
* nombre de realloc() utilisé
* nombre de calloc() utilisé
* nombre de recyclage de zone mémoire
* nombre de mmap() utilisé
* nombre de munmap() utilisé

Ces fonctions peuvent être utilisés en remplacement des fonctions de la librairie standard, soit en compilant son programme contre la librairie, soit en faisant une interposition de symbole grâce au chargement dynamique de librairie ( LD_PRELOAD ).





Complexité des algorithmes
--------------------------

Comparaison des performances
----------------------------




Sources
=======


