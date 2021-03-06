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


La fonction malloc ( et les autres fonctions utilisés lors de la gestion mémoire), n'est plus aussi 'magique' qu'auparavant, dès lors que l'on s'intéresse à ce qu'il se passe lors d'un appel à malloc( size ). Il faut cependant prendre le temps de comprendre quelques concepts. 

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

Elle est rendu thread-safe par l'utilsation de mutex. Les fonctions cité précédemment lockent un mutex_"nom_de_fonction" dès l'entrée dans la fonction, puis le relâche avant de return.
Malloc permet de faire des allocations en parallèle (multithread) à condition que le taille du bloc renvoyé par malloc à chaque thread ne soit pas la même. Des blocs de tailles différentes sont gérés par des variables partagés différentes. Il n'y a donc aucun risque d'accès concurrent le cas échéant.

Ces fonctions peuvent être utilisés en remplacement des fonctions de la librairie standard, soit en compilant son programme contre la librairie, soit en faisant une interposition de symbole grâce au chargement dynamique de librairie ( LD_PRELOAD ).
LD_PRELOAD=./libcustom.so prog_name



Nous allons maintenant décrire la manière dont fonctionne malloc, le code des autres fonctions est très facile à lire et à comprendre. 

Dans les grandes lignes, le premier appel à malloc() appelle mmap() plusieurs fois, et crée des éléments composés : 

* des métadonnées du bloc
* de la zone inscriptible que l'on appellera le bloc, utilisé par l'appelant de malloc 

La taille des métadonnées est fixe, mais la taille de la zone inscriptible peut-être de 64, 128, 256, 512, 1024 bytes. Nous créons ainsi plusieurs liste chainées, chacune permettant de rechercher un bloc de taille 64, 128, 256, 512, 1024 et ainsi de "recycler" les blocs, c'est à dire donner à l'utilisateur l'un de ceux-ci.

		struct bloc 
		{
    			void* adresse;
    			void* addr_previous;
   			void* addr_next;
  			int numero;
    			unsigned int taille;
		};

La liste chainé est en forme d'anneau, le bloc qui suit le dernier bloc, est le premier bloc.

Lorsque l'utilisateur demande de la mémoire, on parcours la liste chainé ( on saute de bloc en bloc), afin d'en trouver un libre à lui fournir. Pour cela nous utilisons l'attribut numero de la structure bloc. Si on en trouve un libre, on le marque occupé, et on le donne. Si on a fait le tour de l'anneau sans rien trouver, on utilise mmap() et on agrandi la liste chainée, puis on choisi un bloc à envoyer.

La méthode pour rechercher un bloc libre est de commencer à chercher à partir du bloc dernièrement alloué, de la taille demandée. 

Dans le cas où l'utilisateur demanderait plus de 1024 bytes, on lui alloue une zone mémoire avec un mmap(). Cette zone mémoire ne sera d'ailleurs pas recyclé. Lors d'un appel à free, celui-ci sera munmappé et non pas marqué comme libre. // je recycle ou pas ? je découpe en petit morceau ?



Complexité des algorithmes
--------------------------

* Algorithme servant à la recherche d'un bloc libre à allouer

	var next_addr
	for i = 1:nb_de_bloc // on boucle comme sur un anneau
		if next_addr = 0 //libre
			next_addr += 1		
			return addr

-> Pire des cas

Le pire des cas se produit lorsque l'on parcours tout les blocs libres, avant de se rendre compte qu'aucun n'est disponible. Là démarre une création de nouveaux bloc. La complexité est donc égale au nombre de bloc créé depuis le début, elle est égale à n. (linéaire)


-> Meilleur des cas

Le meilleur des cas se produit lorsque dès la première itération, la fonction a trouvé un bloc à retourner à l'utilisateur. La complexité est en 1. (constant)


* Algorithme servant à remplir une zone nouvellement mappée

	for i=1:n
		adresse       = xxx
		addr_next     = xxx
		addr_previous = xxx
		taille        = xxx
		numero        = xxx
		
L'algorithme est linéaire, sa complexité en temps est en n, si l'on considère que chaque affectation prend un temps 1 à s'effectuer. Il n'y a pas de meilleure des cas ni de pire des cas. Il n'y a qu'un cas possible. Il faut noter que n est le nombre de bloc à créer.

Comparaison des performances
----------------------------

Les 2 programmes de benchmark sont disponible sur le github dont le lien est situé en haut de page.

L'un d'eux consiste en une boucle de malloc( sizeof( int )); free(); Nous testons ici la performance lors d'allocation et la désallocation de petite zone mémoire, en l'occurence 8 octets.

L'autre utilise à chaque tour de boucle, la fonction malloc, puis free, puis calloc, puis realloc, puis free. Les tailles demancés à chaque allocation sont calculés à l'aide de la fonction rand()%2048. Nous testons ici les performances de la librairie dans le "cas moyen".

Les graphes de performances représentent le temps mis pour exécuter un programme en fonction du nombre de tour de boucle effectué. Chaque ensemble de points de même couleur correspond à des résultats obtenues en utilisant le même allocateur. Nous utiliserons les allocateurs suivant :

* malloc
* _mm_malloc
* custom_malloc

Graphes: Fournis dans le repo github, le lien est en haut de page.




Les points faibles de cet allocateur
====================================

* L'alignement mémoire n'est pas assuré.
* La libération de zones mémoires mmapé mais pas utilisées par le processus concerné n'est pas implémenté.
* Les zones mémoires réservé par l'utilisateur lors d'un appel à malloc( size > 1024) sont libérées directement sans aucun "recyclage".
