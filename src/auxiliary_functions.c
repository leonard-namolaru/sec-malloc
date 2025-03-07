/*
 * Le code contient des commentaires dont la source est le projet de pages de manuel Linux ou du manuel du programmeur POSIX
 * (The Linux man-pages project / POSIX Programmer's Manual)
 */
#define _GNU_SOURCE // Pour mremap()
#include <stdio.h> // fprintf()
#include <stdlib.h> // exit(), atexit(), getenv(), EXIT_FAILURE
#include <alloca.h> // alloca()
#include <unistd.h> // write(), sysconf(), fcntl()
#include <sys/mman.h> // mmap(), mremap(), munmap()
#include <string.h> // memcpy(), memset(), strerror()
#include <stdarg.h> // vsnprintf()
#include <pthread.h> // pthread_once(), pthread_create(), pthread_mutex_ ... , pthread_mutexattr_ ...
#include <dlfcn.h> // dlsym()
#include <errno.h> // errno
#include <sys/types.h> // fcntl()
#include <sys/stat.h> // fcntl()
#include <fcntl.h> // open(), fcntl()
#include "auxiliary_functions.private.h"
#include "my_secmalloc.private.h"
#include "basic_operations.private.h"

/* ****************************************************************** */
/* ******************** DÉTECTION D'OVERFLOW ************************ */
/* ****************************************************************** */

void *dynamic_overflow_detection(void *arg) {
	(void) arg;

	while (1) {
		struct meta_information *result = metadata_array_map(meta_information_pool_root, 1, overflow_detection, NULL, 0, 0);
		if (result != NULL) {
			LOG_ERROR("Detection d'overflow : bloc mémoire commençant à l'adresse %p, (l'adresse du bloc de metadonnees concerne est %p) \n", result->data_ptr, result);
			mutex_unlock(&(result->mutex));
			exit (EXIT_FAILURE);
		}

		sleep(1);
	}
}

/* ****************************************************************** */
/* **************** GESTION DES RESSOURCES GLOBALES ***************** */
/* ****************************************************************** */

void init_logs_file_descriptor() {
	if (logs_file_descriptor == -1) {
		// char *getenv(const char *name);
		// Pour obtenir une variable d'environnement
		// La fonction getenv() renvoie un pointeur vers la valeur,
		// ou NULL s'il n'y a pas de correspondance.
		const char *logs_file_path = getenv("MSM_OUPUT");

		if (logs_file_path != NULL) {
			// int open(const char *pathname, int flags [, mode_t mode]);
			// 0666 & ~022 = 0644 <=> rw-r--r--
			int open_result = open(logs_file_path, O_CREAT | O_WRONLY, 0666);
			if (open_result != -1)
				logs_file_descriptor = open_result;
			else
				logs_file_descriptor = STDOUT_FILENO;
		} else {
			logs_file_descriptor = -2;
		}
	}
}

int get_logs_file_descriptor() {
	if (logs_file_descriptor == -1)
		init_logs_file_descriptor();
	return logs_file_descriptor;
}

long get_canary() {
	return (long) clean;
}

void init_page_size() {
	if (page_size == 0) {
		// long int sysconf (int parameter)
		// Cette fonction est utilisée pour se renseigner sur les paramètres du système d'exécution.
		// Une valeur de -1 est renvoyée à la fois si l'implémentation n'impose pas de limite et en cas d'erreur.
		long tmp = sysconf(_SC_PAGE_SIZE);
		if (tmp == -1)
			page_size = 4096;
		else
			page_size = tmp;
	}
}

size_t get_page_size() {
	if (page_size == 0)
		init_page_size();
	return page_size;
}

struct struct_canary *get_data_pool() {
	if (data_pool == NULL)
		pthread_init_once();
	return data_pool;
}

struct meta_information *get_meta_information_pool_root() {
	if (meta_information_pool_root == NULL)
		pthread_init_once();
	return meta_information_pool_root;
}

size_t get_data_pool_size() {
	if (data_pool_size == 0)
		pthread_init_once();
	return data_pool_size;
}

size_t get_meta_information_pool_size() {
	if (meta_information_pool_size == 0)
		pthread_init_once();
	return meta_information_pool_size;
}

/* ****************************************************************** */
/* ********************* GESTION DES ERREURS ************************ */
/* ****************************************************************** */

void handle_errnum(const char *function_name, int errnum) {
	(void) errnum;
	(void) function_name;

	// char *strerror(int errnum);
	LOG_ERROR("%s : %s\n", function_name, strerror(errnum));
	exit(EXIT_FAILURE);
}

void handle_error(const char *error_message) {
	(void) error_message;

	LOG_ERROR("%s\n", error_message);
	exit(EXIT_FAILURE);
}

/* ****************************************************************** */
/* ********************** GESTION DES MUTEX ************************* */
/* ****************************************************************** */

// Pour éviter la corruption dans les applications multithread, des mutex sont utilisés pour protéger
// les structures de données de gestion de la mémoire.

void mutex_init(pthread_mutex_t *mutex_ptr, int recursive) {
	pthread_mutexattr_t attribute;

	// int pthread_mutexattr_init(pthread_mutexattr_t *attr);
	int mutexattr_init_result = pthread_mutexattr_init(&attribute);
	if (mutexattr_init_result != 0)
		handle_errnum("pthread_mutexattr_init()", mutexattr_init_result);

	if (recursive) {
		// int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
		// type : PTHREAD_MUTEX_NORMAL, PTHREAD_MUTEX_ERRORCHECK, PTHREAD_MUTEX_RECURSIVE ou PTHREAD_MUTEX_DEFAULT
		// Avec PTHREAD_MUTEX_RECURSIVE un thread tentant de reverrouiller ce mutex sans le déverrouiller
		// au préalable réussira à verrouiller le mutex
		int mutexattr_settype_result = pthread_mutexattr_settype(&attribute, PTHREAD_MUTEX_RECURSIVE);
		if (mutexattr_settype_result != 0)
			handle_errnum("pthread_mutexattr_settype()", mutexattr_settype_result);
	}

	// int pthread_mutex_init(pthread_mutex_t *restrict mutex, const pthread_mutexattr_t *restrict attr);
	int mutex_init_result = pthread_mutex_init(mutex_ptr, &attribute);
	if (mutex_init_result != 0)
		handle_errnum("pthread_mutex_init()", mutex_init_result);

	// int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
	// Après qu'un objet pthread_mutexattr_t a été utilisé pour initialiser un ou plusieurs mutex,
	// toute fonction affectant l'objet (y compris la destruction) ne doit pas affecter les mutex précédemment initialisés.
	int mutexattr_destroy_result = pthread_mutexattr_destroy(&attribute);
	if (mutexattr_destroy_result != 0)
		handle_errnum("pthread_mutexattr_destroy()", mutexattr_destroy_result);
}

void mutex_destroy(pthread_mutex_t *mutex_ptr) {
	// int pthread_mutex_destroy(pthread_mutex_t *mutex);
	int mutex_destroy_result = pthread_mutex_destroy(mutex_ptr);
	if (mutex_destroy_result != 0)
		handle_errnum("pthread_mutex_destroy()", mutex_destroy_result);
}

void mutex_lock(pthread_mutex_t *mutex_ptr) {
	DEBUG("lock %p \n", mutex_ptr);

	// int pthread_mutex_lock(pthread_mutex_t *mutex);
	int mutex_lock_result = pthread_mutex_lock(mutex_ptr);
	if (mutex_lock_result != 0)
		handle_errnum("pthread_mutex_lock()", mutex_lock_result);
}

int mutex_trylock(pthread_mutex_t *mutex_ptr) {
	DEBUG("try lock %p \n", mutex_ptr);

	// int pthread_mutex_trylock(pthread_mutex_t *mutex);
	int mutex_trylock_result = pthread_mutex_trylock(mutex_ptr);
	if (mutex_trylock_result != 0) {
		return 0;
		/*
		if (errno == EBUSY)
			return 0;
		else
			handle_errnum("pthread_mutex_trylock()", mutex_trylock_result);
		*/
	}

	return 1;
}

void mutex_unlock(pthread_mutex_t *mutex_ptr) {
	DEBUG("unlock %p \n", mutex_ptr);

	// int pthread_mutex_unlock(pthread_mutex_t *mutex);
	int mutex_unlock_result = pthread_mutex_unlock(mutex_ptr);
	if (mutex_unlock_result != 0)
		handle_errnum("pthread_mutex_unlock()", mutex_unlock_result);
}

/* ****************************************************************** */
/* **************** FONCTIONS AUXILIAIRES GÉNÉRALES ***************** */
/* ****************************************************************** */

void add_log(const char *string_format, int file_descriptor, ...) { // '...' est la syntaxe pour une fonction variadique
	if (file_descriptor < 0 || string_format == NULL) {
		return;
	}

	struct flock logs_file_lock;
	logs_file_lock.l_type = F_WRLCK;
	logs_file_lock.l_whence = SEEK_SET;
	logs_file_lock.l_start = 0;
	logs_file_lock.l_len = 0;

	// va_list est utilisé par des fonctions avec un nombre variable d'arguments de différents types.
	// Une telle fonction doit déclarer un objet de type va_list qui est utilisé par les macros
	// va_start(), va_arg(), va_copy() et va_end() pour parcourir la liste d'arguments.
	va_list ap, ap_copy;

	// void va_start(va_list ap, last);
	// La macro va_start() initialise ap pour une utilisation ultérieure par va_arg() et va_end().
	// L'argument last est le nom du dernier argument avant la liste d'arguments variables,
	// c'est-à-dire le dernier argument dont la fonction appelante connaît le type.

	// Comme l'adresse de cet argument peut être utilisée dans la macro va_start(),
	// elle ne doit pas être un tableau. Pour cette raison nous nous assurons que
	// file_descriptor qui n'est pas un tableau (contrairement à string_format) soit le dernier argument connu.
	va_start(ap, file_descriptor);

	// void va_copy(va_list dest, va_list src);
	// La macro va_copy() copie la liste d'arguments (précédemment initialisée) src vers dest.
	// Le comportement de va_copy() est le même comme si va_start() était appliqué à dest avec le même dernier argument,
	// suivi du même nombre d'invocations va_arg() que celui utilisé pour atteindre l'état actuel de src.
	va_copy(ap_copy, ap);

	// int vsnprintf(char *str, size_t size, const char *format, va_list ap);
	// La fonction vsnprintf() est équivalente à la fonction snprintf(), sauf qu'elle est appelée avec une va_list
	// Cette fonction n'écrit pas plus que size octets (y compris l'octet nul de fin (« \0 »)) dans str.
	// Si la sortie a été tronquée en raison de cette limite, alors la valeur de retour est le nombre
	// de caractères (à l'exclusion de l'octet nul de fin) qui auraient été écrits dans la chaîne finale
	// si suffisamment d'espace avait été disponible.

	// Premier appel à la fonction vsnprintf() avec str = NULL et size = 0
	// pour obtenir la taille de la mémoire à allouer
	int alloc_size = vsnprintf(NULL, 0, string_format, ap_copy);

	 // void va_end(va_list ap);
	// Chaque invocation de va_start() doit correspondre à une invocation correspondante de va_end() dans la même fonction.
	// Après l'appel va_end(ap), la variable ap n'est pas définie.
	// La fonction vsnprintf() n'appelle pas la macro va_end.
	va_end(ap);

	// Définition de alloca_result dans le scope de la fonction car l'espace alloué par alloca()
	// n'est pas automatiquement libéré si le pointeur qui y fait référence sort simplement de sa portée.
	char *alloca_result = NULL;
	if (alloc_size > 0) { // Si une erreur est rencontrée, une valeur négative est renvoyée.
		alloc_size++; // Pour prendre en compte le caractère '\0'

		// void *alloca(size_t size);
		// La fonction alloca() alloue des octets dans la stack frame de l'appelant.
		// Cet espace temporaire est automatiquement libéré lorsque la fonction qui
		// a appelé alloca() revient à son appelant. La fonction alloca() renvoie un
		// pointeur vers le début de l'espace alloué.

		// Il n'y a aucune indication d'erreur si le cadre de pile ne peut pas être étendu.
		// (Cependant, après un échec d'allocation, le programme est susceptible de recevoir
		// un signal SIGSEGV s'il tente d'accéder à l'espace non alloué.)
		alloca_result = (char *) alloca((size_t) alloc_size + 1);
		if (alloca_result != NULL) {
			// int vsnprintf(char *str, size_t size, const char *format, va_list ap);
			int vsnprintf_result = vsnprintf(alloca_result, (size_t) alloc_size, string_format, ap);
			// void va_end(va_list ap);
			va_end(ap_copy);

			if (vsnprintf_result > 0) { // Si une erreur est rencontrée, une valeur négative est renvoyée.

				// int fcntl(int fd, int cmd, ... /* arg */ );
				if (fcntl(file_descriptor, F_SETLKW, &logs_file_lock) == -1) {
					handle_error("Une erreur a empeche de placer un verrou sur le fichier des logs");
				}

				// ssize_t write (int fd, const void *buffer, size_t size)
				/* ssize_t write_result = */ write(file_descriptor, (void *) alloca_result, (size_t) alloc_size);
				/*
				if (write_result < alloc_size) {

				} */

				logs_file_lock.l_type = F_UNLCK;
				if (fcntl(file_descriptor, F_SETLKW, &logs_file_lock) == -1) {
					handle_error("Une erreur a empeche la liberation du verrou sur le fichier des logs");
				}
			}
		}
	}
}

/**
 * La fonction get_delta_size() renvoie le nombre de pages qu'il faut ajouter à la mémoire afin de stocker
 * additional_memory_size octets supplémentaires. Ce calcul permet d'augmenter la mémoire en utilisant
 * une quantité qui est un multiple de la taille d'une page (taille alignée sur l'adresse d'une page).
 */
size_t get_delta_size(size_t additional_memory_size) {
	// Si le reste de la division additional_memory_size / PAGE_SIZE est supérieur à zéro,
	// cela signifie qu'une page supplémentaire doit être ajoutée
	// Exemple (avec la console de Python) : (4097 % 4096) = 1, ((4097)//4096 + (4097) % 4096) = 2
	size_t number_of_pages = ((additional_memory_size / page_size) + ((additional_memory_size % page_size != 0) ? 1 : 0));
	size_t delta_size =  number_of_pages * page_size;

	return delta_size;
}


/* ****************************************************************** */
/* ********* CRÉATION ET ÉLARGISSEMENT DE MAPPAGE DE MÉMOIRE ******** */
/* ****************************************************************** */

void	*remap_memeory(void *memeory_to_realloc, size_t memeory_old_size, size_t delta_size) {
	// void *mremap(void *old_address, size_t old_size, size_t new_size, int flags);
	// mremap() étend (ou réduit) un mappage de mémoire existant.
	// En cas de succès, mremap() renvoie un pointeur vers la nouvelle zone de mémoire virtuelle.
	// En cas d'erreur, la valeur MAP_FAILED est renvoyée.

	// MREMAP_MAYMOVE : Par défaut, s'il n'y a pas suffisamment d'espace pour développer un mappage à son emplacement actuel,
	// alors mremap() échoue. Si cet indicateur est spécifié, alors le noyau est autorisé à déplacer le mappage vers une
	// nouvelle adresse virtuelle, si nécessaire. Si le mappage est déplacé, les pointeurs absolus vers l'ancien emplacement
	// de mappage deviennent invalides (des décalages par rapport à l'adresse de départ du mappage doivent être utilisés).

	// Source : Linux manual page
	struct struct_canary* mremap_result = mremap(memeory_to_realloc, memeory_old_size, memeory_old_size + delta_size,  MREMAP_MAYMOVE);
	if (mremap_result == MAP_FAILED) {
		handle_error("Echec de la fonction mremap()");
	}

	return mremap_result;
}

void	*init_memeory(void *memeory_to_init, void *address) {
	if (memeory_to_init == NULL) {
		// void *mmap(void addr, size_t length, int prot, int flags, int fd, off_t offset);
		// mmap() crée un nouveau mappage dans l'espace d'adressage virtuel du processus appelant.
		// L'adresse du nouveau mappage est renvoyée à la suite de l'appel.
		// En cas d'erreur, la valeur MAP_FAILED est renvoyée

		// MAP_ANON : synonyme de MAP_ANONYMOUS ; fourni pour la compatibilité avec d’autres implémentations.
		// MAP_ANONYMOUS : Le mappage n'est soutenu par aucun fichier ; son contenu est initialisé à zéro.
		//            	   L'argument fd est ignoré ; cependant, certaines implémentations exigent que fd soit -1
		//                 si MAP_ANONYMOUS (ou MAP_ANON) est spécifié. L'argument offset doit être nul.

		// MAP_PRIVATE : Créer un mappage copy-on-write privé.
		//               Les mises à jour du mappage ne sont pas visibles par les autres processus
		// PROT_READ : les pages peuvent être lues ; PROT_WRITE : les pages peuvent être écrites.

		// Source : Linux manual page
		memeory_to_init = mmap(address, get_page_size(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (memeory_to_init == MAP_FAILED) {
			handle_error("Echec de la fonction mmap()");
		}
	}

	return memeory_to_init;
}

void exit_handler() {
	metadata_linked_list_map(meta_information_pool_root, 1, clean_data, NULL, 1);
	int munmap_result;

	if (data_pool != NULL) {
		// int munmap(void *addr, size_t len);
		munmap_result = munmap(data_pool, data_pool_size);
		if (munmap_result != 0)
			handle_error("Echec de la fonction munmap()");

		data_pool = NULL;
	}

	if (meta_information_pool_root != NULL) {
		munmap_result = munmap(meta_information_pool_root, meta_information_pool_size);
		if (munmap_result != 0)
			handle_error("Echec de la fonction munmap()");

		meta_information_pool_root = NULL;
	}
}

/* ****************************************************************** */
/* ************************ INITIALISATION ************************** */
/* ****************************************************************** */

void init() {
	DEBUG("init() \n");

	if (data_pool == NULL && meta_information_pool_root == NULL) {
		init_logs_file_descriptor();
		init_page_size();

		data_pool = init_data_pool();
		meta_information_pool_root = init_meta_information_pool();

		if (data_pool == NULL || meta_information_pool_root == NULL)
			handle_error("Echec de l'initialisation de la memoire");

		dynamic_overflow_detection_activated = 0;
		mutex_init(&dynamic_overflow_detection_activated_mutex, 0);
	}
}


void pthread_init_once() {
	DEBUG("pthread_init_once() \n");

	// int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
	int pthread_once_result = pthread_once(&already_initialized, init);
	if (pthread_once_result != 0)
		handle_errnum("pthread_once()", pthread_once_result);

	pthread_t thread_id;
	int trylock_result = mutex_trylock(&dynamic_overflow_detection_activated_mutex);
	if (trylock_result) {
		if (!dynamic_overflow_detection_activated) {
			// int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
			int pthread_create_result = pthread_create(&thread_id, NULL, dynamic_overflow_detection, NULL);
			if (pthread_create_result != 0)
				handle_errnum("pthread_create()", pthread_create_result);

			LOG("Activation du détecteur dynamique de l'overflow via un thread de parcours du tas \n");

			// int atexit(void (*function)(void));
			// int atexit_result = atexit(exit_handler);
			// if (atexit_result != 0)
			// 	   handle_error("Echec de la fonction atexit()");

			dynamic_overflow_detection_activated = 1;
		}
		mutex_unlock(&dynamic_overflow_detection_activated_mutex);
	}
}

struct struct_canary *init_data_pool() {
	if (data_pool == NULL && meta_information_pool_root == NULL) {
		data_pool_size = page_size;
		data_pool = (struct struct_canary *) init_memeory(data_pool, (void*) (page_size * 1500000));
		LOG("Initialisation du pool de data. L'adresse de debut de ce pool est %p \n", data_pool);

		struct struct_canary *ptr_end = (struct struct_canary *) ((size_t) data_pool + (page_size - sizeof(struct struct_canary)));
		ptr_end->canary = get_canary();
	}

	return data_pool;
}

struct meta_information *init_meta_information_pool() {
	if (meta_information_pool_root == NULL && data_pool != NULL) {
		meta_information_pool_size = page_size;
		meta_information_pool_root = (struct meta_information *) init_memeory(meta_information_pool_root, (void*) page_size);
		LOG("Initialisation du pool de meta-information. L'adresse de debut de ce pool est %p \n", meta_information_pool_root);

		// Pour initialiser la première structure de données
		meta_information_pool_root->data_ptr = data_pool;
		meta_information_pool_root->size = page_size - sizeof(struct struct_canary);
		meta_information_pool_root->status = FREE;

		meta_information_pool_root->prev = NULL;
		meta_information_pool_root->next = NULL;

		mutex_init(&(meta_information_pool_root->mutex), 1);
		metadata_array_map(meta_information_pool_root, 0, init_empty_meta_information_struct, NULL, 1, 1);
	}

	return meta_information_pool_root;
}

/* ****************************************************************** */
/* ***************** EXTENSION DES ZONES MÉMOIRE ******************** */
/* ****************************************************************** */

void extend_meta_information_pool() {
	struct meta_information *new_meta_information_pool = (struct meta_information*) remap_memeory(meta_information_pool_root, meta_information_pool_size, page_size);
	if (new_meta_information_pool != meta_information_pool_root) {
		LOG("Le pool de meta-information a change d'adresse apres un redimensionnement. "
				"La nouvelle adresse est %p \n", new_meta_information_pool);
		meta_information_pool_root = new_meta_information_pool;
	}

	size_t meta_information_pool_elements_nb_before = meta_information_pool_size / sizeof(struct meta_information);
	meta_information_pool_size += page_size;
	LOG("Le pool de meta-informations a ete elargi. La nouvelle taille est %lu. \n", meta_information_pool_size);

	metadata_array_map(meta_information_pool_root, 0, init_empty_meta_information_struct, NULL, meta_information_pool_elements_nb_before, 1);
}

void extend_data_pool(struct meta_information* last_meta_information_item, size_t data_pool_delta_size, size_t last_meta_information_item_new_size_including_canary) {
	struct struct_canary* new_data_pool = remap_memeory(data_pool, data_pool_size, data_pool_delta_size);
	if (new_data_pool != data_pool) {
		LOG("Le pool de data a change d'adresse apres un redimensionnement. "
				"La nouvelle adresse est %p \n", new_data_pool);
		data_pool = new_data_pool;
	}

	if (last_meta_information_item->data_ptr == NULL && last_meta_information_item->status == UNUSED) {
		last_meta_information_item->status = FREE;
		last_meta_information_item->data_ptr = (struct struct_canary *) ((size_t) data_pool + data_pool_size);
	}

	data_pool_size += data_pool_delta_size;
	LOG("Le pool de data a ete elargi. La nouvelle taille est %lu\n", data_pool_size);

	// Mettre à jour les informations concernant le dernier morceau
	last_meta_information_item->size = last_meta_information_item_new_size_including_canary - sizeof(struct struct_canary);

	struct struct_canary *ptr_end = (struct struct_canary *) ((size_t) last_meta_information_item->data_ptr + last_meta_information_item->size);
	ptr_end->canary = get_canary();
	LOG("La nouvelle taille du dernier bloc de metadonnees (%p) : %lu\n", last_meta_information_item, last_meta_information_item->size);
}

/* ************************************************************************************************ */
/* * FONCTIONS POUVANT ÊTRE PASSÉES EN PARAMÈTRE À METADATA_LINKED_LIST_MAP OU METADATA_ARRAY_MAP * */
/* *********************************************************************************************** */

int clean_data(struct meta_information *meta_information_element, void *arg2) {
	(void) arg2;
	if (meta_information_element != NULL && meta_information_element->status == BUSY) {
		clean(meta_information_element->data_ptr);
	}

	return 0;
}

int overflow_detection(struct meta_information *meta_information_element, void *arg2) {
	(void) arg2;
	if (meta_information_element == NULL || meta_information_element->data_ptr == NULL)
		return 0;

	struct struct_canary *chunck = (struct struct_canary *) ((size_t) meta_information_element->data_ptr + meta_information_element->size);
	DEBUG("canary_position %ld get_canary %ld meta_information_element %p \n", struct_canary->canary, get_canary(), meta_information_element);

	return(chunck->canary != get_canary());
}

int is_last_meta_information_struct(struct meta_information *meta_information_element, void *arg2) {
	(void) arg2;
	return (meta_information_element->next == NULL);
}

int is_meta_information_of_memory_ptr(struct meta_information * meta_information_element, void *memory_ptr) {
	return (meta_information_element->data_ptr == memory_ptr);
}

int is_meta_information_of_free_memory(struct meta_information * meta_information_element, void *memory_size) {
	size_t memory_size_value = *((size_t*) memory_size);
	return (meta_information_element->status == FREE && meta_information_element->size >= memory_size_value);
}

int init_empty_meta_information_struct(struct meta_information * meta_information_element, void *arg2) {
	(void) arg2;

	meta_information_element->size = 0;
	meta_information_element->data_ptr = NULL;
	meta_information_element->status = UNUSED;

	meta_information_element->next = NULL;
	meta_information_element->prev = NULL;

	mutex_init(&(meta_information_element->mutex), 1);
	return 0;
}

int init_if_empty_meta_information_struct(struct meta_information * meta_information_element, void *arg2) {
	(void) arg2;

	if (meta_information_element->status == UNUSED) {
		// Pour s'assurer que toutes les données de la structure sont initialisées
		meta_information_element->size = 0;
		meta_information_element->next = NULL;
		meta_information_element->prev = NULL;
		meta_information_element->data_ptr = NULL;
		return 1;
	}
	return 0;
}


/* ****************************************************************** */
/* *********** GESTION DE LA LISTE CHAÎNÉE DES MÉTADONNÉES ********** */
/* ****************************************************************** */

struct meta_information *get_empty_meta_information_struct(struct meta_information *prev_meta_information_struct) {
	if (prev_meta_information_struct == NULL) {
		prev_meta_information_struct = metadata_linked_list_map(meta_information_pool_root, 1, is_last_meta_information_struct, NULL, 0);
	}

	struct meta_information *empty_meta_information_struct = metadata_array_map(meta_information_pool_root, 1, init_if_empty_meta_information_struct, NULL, 0, 0);
	if (empty_meta_information_struct == NULL) {
		extend_meta_information_pool();
		empty_meta_information_struct = metadata_array_map(meta_information_pool_root, 1, init_if_empty_meta_information_struct, NULL, 0, 0);
	}

	empty_meta_information_struct->prev = prev_meta_information_struct;
	empty_meta_information_struct->next = prev_meta_information_struct->next;
	prev_meta_information_struct->next = empty_meta_information_struct;

	return empty_meta_information_struct;
}

struct meta_information *metadata_linked_list_map(struct meta_information * meta_information_root, int return_if_func_true,
		int (*func) (struct meta_information *, void *), void *func_arg2, int unlock_mutex_before_return) {
	if (meta_information_root == NULL) {
		return NULL;
	}

	mutex_lock(&(meta_information_root->mutex));

	DEBUG("metadata_linked_list_map root %p size %lu - status %u data_ptr %p prev %p next %p \n", meta_information_root,
			meta_information_root->size, meta_information_root->status, meta_information_root->data_ptr, meta_information_root->prev, meta_information_root->next);

	if (func(meta_information_root, func_arg2) && return_if_func_true) {
		if (unlock_mutex_before_return) {
			mutex_unlock(&(meta_information_root->mutex));
		}

		return meta_information_root;
	}

	struct meta_information *prev_element_ptr = meta_information_root;
	struct meta_information *curr_element_ptr = prev_element_ptr->next;

	while (curr_element_ptr != NULL) {
		mutex_lock(&(curr_element_ptr->mutex));

		DEBUG("metadata_linked_list_map %p size %lu - status %u data_ptr %p prev %p next %p \n", curr_element_ptr,
				curr_element_ptr->size, curr_element_ptr->status, curr_element_ptr->data_ptr, curr_element_ptr->prev, curr_element_ptr->next);

		if (func(curr_element_ptr, func_arg2) && return_if_func_true) {
			if (unlock_mutex_before_return) {
				mutex_unlock(&(curr_element_ptr->mutex));
			}

			mutex_unlock(&(prev_element_ptr->mutex));
			return curr_element_ptr;
		}

		mutex_unlock(&(prev_element_ptr->mutex));
		prev_element_ptr = curr_element_ptr;
		curr_element_ptr = curr_element_ptr->next;
	}

	mutex_unlock(&(prev_element_ptr->mutex));

	DEBUG("metadata_linked_list_map : NULL \n");
	return NULL;
}

struct meta_information *metadata_array_map(struct meta_information * meta_information_root, int return_if_func_true,
		int (*func) (struct meta_information *, void *), void *func_arg2, size_t start_index, int unlock_mutex_before_return) {
	for (size_t i = start_index ; i < (meta_information_pool_size / sizeof(struct meta_information)) ; i++) {
		if (func == init_empty_meta_information_struct || mutex_trylock(&(meta_information_pool_root[i].mutex))) {

			DEBUG("metadata_array_map %p size %lu - status %u data_ptr %p prev %p next %p \n", &meta_information_pool_root[i],
					meta_information_pool_root[i].size, meta_information_pool_root[i].status, meta_information_pool_root[i].data_ptr,
					meta_information_pool_root[i].prev, meta_information_pool_root[i].next);

			if (func(&meta_information_root[i], func_arg2) && return_if_func_true) {
				if (unlock_mutex_before_return && func != init_empty_meta_information_struct) {
					mutex_unlock(&(meta_information_pool_root[i].mutex));
				}
				return &meta_information_root[i];
			}

			if (func != init_empty_meta_information_struct)
				mutex_unlock(&(meta_information_pool_root[i].mutex));
		} else {
			DEBUG("metadata_array_map %p \n", &meta_information_pool_root[i]);
		}
	}

	DEBUG("metadata_array_map : NULL \n");
	return NULL;
}
