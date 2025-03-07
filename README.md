# SecMalloc : Allocateur de mémoire sécurisé
Ce projet porte sur la réécriture des fonctions `malloc()`, `calloc()`, `realloc()` et `free()` sous un axe de sécurité, étant donné que même si beaucoup de correction ont été apporté aux implémentations standard de `malloc()`, la gestion du tas reste encore un vecteur d’attaque puissant.

### Fonctionnalités implémentées
- Le comportement des fonctions réécrites (`my_malloc()`, `my_calloc()`, `my_realloc()` et `my_free()`) est le même que celui des fonctions correspondantes décrites dans `man 3 malloc`.
- L'implémentation se fait à travers 2 pools distincts : un pool de data et un pool de meta-information.
- Ajout d'un canari à la fin de chaque bloc mémoire afin de détecter un overflow.
- Prise en charge des allocations mémoire pour les applications multithread grâce à l'utilisation de mutex afin de protéger les structures de données.
- Détection dynamique de l’overflow via un thread de parcours du tas.
- Détection des cas où le pointeur passé aux fonctions `my_realloc()` ou `my_free()` ne pointe pas vers une zone mémoire qui a été renvoyée par un précédent appel à `my_malloc()`, `my_calloc()` ou `my_realloc()`.
- Détection de double free.
- Possibilité de générer un rapport d'exécution dans un fichier dont le chemin est fourni par l'utilisateur à l'aide de la variable d'environnement `MSM_OUPUT`.

### Explications concernant l'implémentation

#### Allocation, redimensionnement et libération de mémoire
- L'allocation de mémoire avec `my_malloc()` se fait en utilisant l'approche _first fit_ tout en divisant le bloc de mémoire de la manière la plus optimale si la taille du bloc est supérieure à la taille de l'allocation demandée.
	- Si la taille restante dans le bloc est supérieure à la taille de `struct_canary` : division en 2 blocs.
	- Si la taille restante est inférieure ou égale à la taille de `struct_canary`, et si ce bloc est le dernier bloc du pool de data, une expansion du pool de data est effectuée afin que la taille restante puisse être utilisée pour une allocation future.
	
	
- Le redimensionnement d'une allocation à l'aide de la fonction `my_realloc` s'effectue de la manière suivante :
	- Si la nouvelle taille est la même que la précédente : aucune opération n'est effectuée et la fonction retourne avec le même pointeur.
	- Si la taille demandée est inférieure à la taille actuelle :
		- Tentative de diviser le bloc en utilisant le même algorithme que celui utilisé pour la fonction `my_malloc`.
		- Si une division n'est pas possible (lorsque la taille restante est inférieure à `struct_canary` et que le bloc n'est pas le dernier bloc du pool de data) : nous fusionnons la taille restante avec le bloc suivant, s'il est libre.
	- Si la taille demandée est supérieure à la taille actuelle :
		- Tentative de fusion avec le bloc suivant, s'il est libre et s'il est suffisamment grand (après cela, nous effectuons une division pour que la taille restante après la fusion puisse être utilisée pour une allocation future).
		- Si cela n'est pas possible, une nouvelle allocation de mémoire est effectuée, le contenu qui existait dans l'allocation précédente est copié dans le nouveau bloc, puis l'ancienne allocation est libérée.


- Fusion de blocs vides consécutifs après chaque libération d'un bloc mémoire avec `my_free()`.

#### Gestion des métadonnées
Le pool de meta-information contient `meta_information_pool_size / sizeof(struct meta_information)` blocs consécutifs de la structure de données `struct meta_information`.
Ce pool pourrait donc être géré comme un tableau, mais une telle gestion nous limiterait, car nous souhaitons que l'ordre des blocs des métadonnées soit le même que l'ordre des blocs dans le pool data. Par conséquent, il faut tenir compte, par exemple, du fait que les blocs de données du pool data peuvent être divisés, auquel cas il est nécessaire d'ajouter un bloc de métadonnées supplémentaire, éventuellement entre deux blocs de métadonnées existants. Le moyen le plus évident de modifier facilement l'ordre des blocs est d'utiliser une liste chaînée, et c'est ainsi que le pool de meta-information est géré.


Dans de nombreuses implémentations "classiques" de listes chaînées, la création d'un nouvel élément dans la liste se fait en appelant malloc(). Dans notre cas, à chaque fois qu'il est nécessaire d'ajouter un élément à la liste chaînée, un parcours de l'espace mémoire du pool de méta-information est effectué, bloc par bloc (et non dans l'ordre des pointeurs de la liste chaînée) afin de trouver un bloc qui n'est pas encore utilisé, c'est-à-dire : un bloc qui existe dans cet espace mémoire mais qu'aucun élément de la liste chaînée ne pointe encore vers lui.

Etant donné que de nombreuses opérations nécessitent de parcourir l'espace mémoire du pool de meta-information, soit sous forme de liste chaînée, en suivant les pointeurs, soit sous forme de tableau (bloc par bloc, pour initialiser tous les blocs, ou pour rechercher un bloc inutilisé afin de l'ajouter à la liste chaînée), l'implémentation utilise les 2 fonctions suivantes :

```
struct meta_information *metadata_linked_list_map(struct meta_information * meta_information_root, int return_if_func_true,
		int (*func) (struct meta_information *, void *), void *func_arg2, int unlock_mutex_before_return);
```

```
struct meta_information *metadata_array_map(struct meta_information * meta_information_root, int return_if_func_true,
		int (*func) (struct meta_information *, void *), void *func_arg2, size_t start_index, int unlock_mutex_before_return);
```

L'argument le plus important dans le cas de ces 2 fonctions est `int (*func) (struct meta_information *, void *)` : c'est un pointeur vers une fonction qui sera appelée sur chacun des blocs de métadonnées, accompagné d'un paramètre supplémentaire (potentiellement NULL), `void *func_arg2`.


La fonction `func` renvoie une valeur booléenne, 0 ou 1, et si `int return_if_func_true` n'est pas égal à zéro, les fonctions `metadata_array_map` et `metadata_linked_list_map` arrêteront le parcous après le premier appel à `func` qui renvoie une valeur non nulle. Dans un tel cas, la valeur qui sera renvoyée à la fonction appelante est un pointeur vers l'élément de métadonnées sur lequel l'appel de `func` a renvoyé une valeur non nulle.

#### Synchronisation des threads
L'un des principaux avantages de l'utilisation des fonctions `metadata_linked_list_map` et `metadata_array_map` est que cela concentre les principales opérations de synchronisation en un seul endroit.

Dans le cas de `metadata_linked_list_map`, il s'agit de synchronisation à grain fin ( _fine-grained synchronization_ ). De cette façon, chaque élément du  pool de meta-information possède son propre verrou, et le parcours de la liste chaînée est effectué en prenant les verrous les uns après les autres, puis en les relâchant, au fur et à mesure que nous progressons vers les éléments suivants de la liste. La synchronisation à grain fin est la forme de synchronisation utilisée également par les fonctions `my_realloc()` et `merge_if_free()`. Cela permet un véritable parallélisme entre les threads, puisque de cette manière toute la liste chaînée n'est pas verrouillée à chaque fois qu'il est nécessaire d'effectuer une opération sur l'un des éléments, et ainsi plusieurs threads peuvent travailler simultanément sur différents éléments de la liste chaînée.

Cependant, étant donné que la fonction `metadata_array_map` effectue un parcours de la même zone mémoire, mais pas dans le même ordre, une utilisation du même algorithme de synchronisation comme pour la fonction `metadata_linked_list_map`, aurait pu conduire à un interblocage, car l'ordre de prise et de relâchement des verrous n'est pas le même.

La solution trouvée pour cela est d'utiliser `pthread_mutex_trylock()` au lieu de `pthread_mutex_lock()` de telle sorte que si la fonction _trylock_ ne parvient pas à obtenir un verrou sur un élément, nous passons à l'élément suivant du tableau. Ce n'est pas un problème car la fonction `metadata_array_map` est principalement utilisée pour trouver un bloc de métadonnées libre, ou pour vérifier qu'un overflow ne s'est pas produit, il n'est donc pas nécessaire de vérifier immédiatement chaque bloc au cours du parcours, contrairement à des opérations telles que la recherche d'un bloc de métadonnées qui représente un espace mémoire libre, ou un bloc de métadonnées qui pointe vers un bloc que nous recherchons dans le pool data (ces opérations se font donc à l'aide de la fonction `metadata_linked_list_map`).

Un autre point est que les fonctions `metadata_linked_list_map` et `metadata_array_map` permettent de déléguer la responsabilité de libérer le verrou sur l'élément pointé par le pointeur renvoyé par ces fonctions, à la fonction appelante, lorsque la valeur de l'argument `int unlock_mutex_before_return` est nulle.

De plus, l'initialisation des zones mémoire (le pool de meta-information et le pool de data) s'effectue à l'aide de la fonction `pthread_once()`.

### Utilisation
Les commandes suivantes ont été testées sous Ubuntu 22.04.

#### Mise en place de la bibliothèque Criterion
```
cd /usr/local/include/criterion/
sudo apt install meson ninja-build cmake pkg-config libffi-dev libgit2-dev
sudo git clone https://github.com/Snaipe/Criterion.git
cd Criterion/
sudo meson setup build
sudo meson compile -C build
sudo meson install -C build

cd ~/Documents/ecole2600/A1/A1S2/Cycle3/TEC-OSC/my_secmalloc/

mkdir lib
cd lib
ln -s /usr/local/include/criterion/
```

#### Exécution avec création d'un rapport d'exécution
```
export MSM_OUPUT=logs.txt
```

#### Exécution des tests
```
make clean test
```

### Utilisation de SecMalloc pour les allocations de mémoire effectuées par d'autres programmes

__Compilation d'une bibliothèque dynamique__

```
make clean dynamic
```

__Test avec ls__

```
LD_PRELOAD=~/Documents/ecole2600/A1/A1S2/Cycle3/TEC-OSC/my_secmalloc/libmy_secmalloc.so ls
```

__Test avec Chromium__

Installation sur Ubuntu avec `sudo apt-get install chromium-browser`

```
LD_PRELOAD=~/Documents/ecole2600/A1/A1S2/Cycle3/TEC-OSC/my_secmalloc/libmy_secmalloc.so chromium
```

__Test avec Firefox__

Installé par défaut sur Ubuntu

```
LD_PRELOAD=~/Documents/ecole2600/A1/A1S2/Cycle3/TEC-OSC/my_secmalloc/libmy_secmalloc.so firefox
```

__Test avec Google Chrome__

Installation sur Ubuntu :
- `wget https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb`
- `sudo dpkg -i google-chrome-stable_current_amd64.deb`

```
LD_PRELOAD=~/Documents/ecole2600/A1/A1S2/Cycle3/TEC-OSC/my_secmalloc/libmy_secmalloc.so google-chrome-stable
```

ou

```
LD_PRELOAD=~/Documents/ecole2600/A1/A1S2/Cycle3/TEC-OSC/my_secmalloc/libmy_secmalloc.so google-chrome
```