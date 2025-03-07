/*
 * Le code contient des commentaires dont la source est le projet de pages de manuel Linux ou du manuel du programmeur POSIX
 * (The Linux man-pages project / POSIX Programmer's Manual)
 */
#define _POSIX_C_SOURCE // Pour kill()
#include "my_secmalloc.private.h"
#include <string.h> // memcpy(), memset()
#include <dlfcn.h> // dlsym()
#include <sys/types.h> // kill(), SIGUSR1
#include <signal.h> // kill(), SIGUSR1
#include <unistd.h> // getpid()
#include <pthread.h> // PTHREAD_ONCE_INIT
#include "auxiliary_functions.private.h"
#include "basic_operations.private.h"

/* ****************************************************************** */
/* ************************ RESSOURCES GLOBALES ********************* */
/* ****************************************************************** */

size_t page_size = 0;
int logs_file_descriptor = -1;

struct struct_canary *data_pool = NULL;
struct meta_information *meta_information_pool_root = NULL;

size_t data_pool_size = 0;
size_t meta_information_pool_size = 0;

int dynamic_overflow_detection_activated;
pthread_once_t already_initialized = PTHREAD_ONCE_INIT;
pthread_mutex_t dynamic_overflow_detection_activated_mutex;

/* ****************************************************************** */
/* *********************** FONCTIONS PRINCIPALES ******************** */
/* ****************************************************************** */

/**
 * void    *my_malloc(size_t size)
 * La fonction my_malloc() alloue size octets et renvoie un pointeur vers la mémoire allouée
 * ou NULL en cas d'erreur. La mémoire n'est pas initialisée.
 */
void    *my_malloc(size_t size) {
	LOG("my_malloc(%lu) \n", size);
	pthread_init_once();

    // Si la taille est 0, alors my_malloc() renvoie NULL
	if (size == 0)
		return NULL;

	return alloc(size);
}

/**
 * void    my_free(void *ptr)
 * La fonction my_free() libère l'espace mémoire pointé par ptr, qui doit avoir été renvoyé par
 * un appel précédent à my_malloc(), my_calloc() ou my_realloc().
 * La fonction my_free() ne renvoie aucune valeur.
 */
void    my_free(void *ptr) {
	LOG("my_free(%p) \n", ptr);
	pthread_init_once();

    // Si ptr est NULL, aucune opération n’est effectuée.
	if (ptr == NULL)
		return;

	int clean_result = clean(ptr);

	// Si l'espace mémoire pointé par ptr, n'a pas été renvoyé par un appel précédent
    // à my_malloc(), my_calloc() ou my_realloc(), ou si free(ptr) a déjà été appelé auparavant
	if (clean_result == 0) {
		LOG_ERROR("my_free(%p) : Double free ou un pointeur qui ne provient pas d'un appel précédent "
				"à my_malloc(), my_calloc() ou my_realloc() \n", ptr);
		kill(getpid(), SIGUSR1);
	}
}

/**
 * void    *my_calloc(size_t nmemb, size_t size)
 * La fonction my_calloc() alloue de la mémoire pour un tableau d'éléments nmemb de taille size octets chacun et
 * renvoie un pointeur vers la mémoire allouée ou NULL en cas d'erreur. La mémoire est mise à zéro.
 */
void    *my_calloc(size_t nmemb, size_t size) {
	LOG("my_calloc(%lu, %lu) \n", nmemb, size);
	pthread_init_once();

    // Si nmemb ou size est 0, alors my_calloc() renvoie NULL
	if (nmemb == 0 || size == 0)
		return NULL;

	void *my_malloc_result = my_malloc(nmemb * size);
	if (my_malloc_result != NULL) {
		// La mémoire est mise à zéro.
		// void * memset(void * block, int value, size_t size);
		memset(my_malloc_result, 0, nmemb * size);
	}

	return my_malloc_result;
}

/**
 * void    *my_realloc(void *ptr, size_t size)
 * La fonction my_realloc() modifie la taille du bloc mémoire pointé par ptr en size octets.
 * Le contenu sera inchangé dans la plage depuis le début de la région jusqu'au minimum entre l'ancienne
 * et la nouvelle taille. Si la nouvelle taille est supérieure à l'ancienne taille, la mémoire ajoutée
 * ne sera pas initialisée.
 *
 * La fonction my_realloc() renvoie un pointeur vers la mémoire nouvellement allouée, qui est convenablement
 * alignée pour tout type built-in, ou NULL si la requête a échoué. Le pointeur renvoyé peut être le même
 * que ptr si l'allocation n'a pas été déplacée (par exemple, il y avait de la place pour étendre l'allocation
 * sur place), ou différent de ptr si l'allocation a été déplacée vers une nouvelle adresse.
 *
 * Si my_realloc() échoue, le bloc d'origine reste intact ; il n'est ni libéré ni déplacé.
 */
void    *my_realloc(void *ptr, size_t size) {
	LOG("my_realloc(%lx, %lu) \n", (size_t) ptr, size);
	pthread_init_once();

    // Si ptr est NULL, alors l'appel est équivalent à my_malloc(size),
	// pour toutes les valeurs de size
    if (ptr == NULL)
    	return my_malloc(size);

    // Si size est égal à zéro et que ptr n'est pas NULL,
    // alors l'appel équivaut à free(ptr).
    if (size == 0 && ptr != NULL) {
    	my_free(ptr);

        // Si size est égal à zéro, soit NULL,
    	// soit un pointeur pouvant être passé à free() est renvoyé.
    	return NULL;
    }

    // À moins que ptr soit NULL, il doit avoir été renvoyé par un appel antérieur
    // à my_malloc(), my_calloc() ou my_realloc().
	struct meta_information *metadata_of_ptr = metadata_linked_list_map(meta_information_pool_root, 1,
			is_meta_information_of_memory_ptr, ptr, 0);
	if (metadata_of_ptr == NULL) {
		LOG_ERROR("my_realloc(%p, %lu) : un pointeur qui ne provient pas d'un appel précédent à my_malloc(), "
				"my_calloc() ou my_realloc() \n", ptr, size);
		kill(getpid(), SIGUSR1);

		// La fonction realloc() renvoie NULL si la requête a échoué
		// Le bloc d'origine reste intact ; il n'est ni libéré ni déplacé.
	    return NULL;
	}

	// Si la taille reste inchangée
	if (metadata_of_ptr->size == size) {
		mutex_unlock(&(metadata_of_ptr->mutex));
		return ptr;
	}

	// Si la taille demandée est inférieure à la taille actuelle
	if (size < metadata_of_ptr->size) {

		if (memory_division(metadata_of_ptr, size) && metadata_of_ptr->next != NULL) {
			clean(metadata_of_ptr->next->data_ptr);

		} else if (metadata_of_ptr->next != NULL) {
			mutex_lock(&(metadata_of_ptr->next->mutex));

			if (metadata_of_ptr->next->status == FREE) {
				size_t diff = metadata_of_ptr->size - size;

				struct struct_canary *chunck = (struct struct_canary *) ((size_t) metadata_of_ptr->data_ptr + (metadata_of_ptr->size - diff));
				chunck->canary = get_canary();

				metadata_of_ptr->size -= diff;
				metadata_of_ptr->next->size += diff;
				metadata_of_ptr->next->data_ptr = (struct struct_canary *) (((size_t) metadata_of_ptr->next->data_ptr) - diff);
				LOG("metadata_of_ptr->next->data_ptr %p \n", metadata_of_ptr->next->data_ptr);
			}
			mutex_unlock(&(metadata_of_ptr->next->mutex));
		}

		mutex_unlock(&(metadata_of_ptr->mutex));
		return ptr;
	}

	/* Si la taille demandée est supérieure à la taille actuelle */

	// Si ce n'est pas le dernier bloc
	if (metadata_of_ptr->next != NULL) {

		mutex_lock(&(metadata_of_ptr->next->mutex));
		struct meta_information *next_meta_information_struct = metadata_of_ptr->next;
		struct meta_information *next_next_meta_information_struct = next_meta_information_struct->next;

		if (next_next_meta_information_struct != NULL)
			mutex_lock(&(next_next_meta_information_struct->mutex));

		if (metadata_of_ptr->next->status == FREE
			&& (metadata_of_ptr->size + sizeof(struct struct_canary) + metadata_of_ptr->next->size) >= size) {

			// Puisque nous fusionnons des espaces mémoire, le bloc de métadonnées suivant n'est plus nécessaire
			metadata_of_ptr->next->status = UNUSED;
			metadata_of_ptr->next->size = 0;
			metadata_of_ptr->next->data_ptr = NULL;
			metadata_of_ptr->next->next = NULL;
			metadata_of_ptr->next->prev = NULL;

			struct struct_canary *chunck = (struct struct_canary *) ((size_t) metadata_of_ptr->data_ptr + metadata_of_ptr->size + sizeof(struct struct_canary) + next_meta_information_struct->size);
			chunck->canary = get_canary();

			// Effectuer une fusion avec l'espace mémoire pointé par le prochain bloc de métadonnées
			metadata_of_ptr->size += sizeof(struct struct_canary) + next_meta_information_struct->size;
			// Le bloc suivant est maintenant le bloc suivant du bloc suivant
			metadata_of_ptr->next = next_next_meta_information_struct;
			// Le bloc précédent du bloc suivant du bloc suivant est maintenant ce bloc
			if (next_next_meta_information_struct != NULL)
				next_next_meta_information_struct->prev = metadata_of_ptr;

			memory_division(metadata_of_ptr, size);
			if (next_next_meta_information_struct != NULL) {
				mutex_unlock(&(next_next_meta_information_struct->mutex));
			}

			mutex_unlock(&(next_meta_information_struct->mutex));
			mutex_unlock(&(metadata_of_ptr->mutex));
			return ptr;
		}

		if (next_next_meta_information_struct != NULL) {
			mutex_unlock(&(next_next_meta_information_struct->mutex));
		}

		mutex_unlock(&(next_meta_information_struct->mutex));
	}

	mutex_unlock(&(metadata_of_ptr->mutex));
	struct meta_information *ptr_meta_information =
			metadata_linked_list_map(meta_information_pool_root, 1, is_meta_information_of_memory_ptr, ptr, 0);
	if (ptr_meta_information == NULL) {
		return NULL;
	}

	size_t prev_size = ptr_meta_information->size;

	void *new_ptr = alloc(size);
	// void * memcpy (void *restrict to, const void *restrict from, size_t size)
	memcpy(new_ptr, ptr ,prev_size);

	// Si la zone pointée a été déplacée, un my_free(ptr) est effectué.
	my_free(ptr);

	mutex_unlock(&(ptr_meta_information->mutex));
	return new_ptr;
}

#ifdef DYNAMIC
void    *malloc(size_t size) {
	/*
	LOG("Avant le vrai malloc %ld\n", size);
	void *(*real_malloc)(size_t) = dlsym(RTLD_NEXT, "malloc");
	LOG("Après le vrai malloc %p\n", real_malloc);
    return real_malloc(size);
	*/

    return my_malloc(size);
}
void    free(void *ptr) {
	/*
	LOG("Avant le vrai free %p\n", ptr);
	void (*real_free)(void *) = dlsym(RTLD_NEXT, "free");
	LOG("Après le vrai free %p\n", real_free);
	real_free(ptr);
	return;
	*/

	my_free(ptr);
}
void    *calloc(size_t nmemb, size_t size) {
	/*
	LOG("Avant le vrai calloc %ld %ld\n", nmemb, size);
	void *(*real_calloc)(size_t, size_t) = dlsym(RTLD_NEXT, "calloc");
	LOG("Après le vrai calloc %p\n", real_calloc);
    return real_calloc(nmemb, size);
	*/

    return my_calloc(nmemb, size);
}

void    *realloc(void *ptr, size_t size) {
	/*
	LOG("Avant le vrai realloc %p %ld\n", ptr, size);
	void *(*real_realloc)(void *, size_t) = dlsym(RTLD_NEXT, "realloc");
	LOG("Après le vrai realloc %p\n", real_realloc);
    return real_realloc(ptr, size);
	*/

    return my_realloc(ptr, size);
}

#endif
