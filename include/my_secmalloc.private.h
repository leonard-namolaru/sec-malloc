#ifndef _SECMALLOC_PRIVATE_H /* garde d'inclusion pour éviter l'inclusion multiple */
#define _SECMALLOC_PRIVATE_H

#include "my_secmalloc.h"

#include <stddef.h> // size_t
#include <pthread.h> // pthread_mutex_t
#include <unistd.h> // STDOUT_FILENO

// Macros variadiques
// l'opérateur '##' a une signification particulière lorsqu'il est placé entre une virgule et un argument variable :
// si l'argument variable n'est pas utilisé lorsque la macro est utilisée, alors la virgule avant le '##' sera supprimée
// Source : https://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html
#define LOG(format, ...) add_log(format, get_logs_file_descriptor(), ##__VA_ARGS__) // ;add_log(format, STDOUT_FILENO, ##__VA_ARGS__)
#define DEBUG(format, ...) // add_log(format, get_logs_file_descriptor(), ##__VA_ARGS__) // ;add_log(format, STDOUT_FILENO, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) add_log(format, get_logs_file_descriptor(), ##__VA_ARGS__); add_log(format, STDOUT_FILENO, ##__VA_ARGS__)

typedef char byte;

enum status {
	FREE = 0,
	BUSY = 1,
	UNUSED = 2
};

struct struct_canary {
	long canary;
};

struct meta_information {
	struct struct_canary *data_ptr; // Pointeur vers le debut du bloc dans le pool de data
	enum status status;  // Etat du bloc allouée (occupée ou libre)
	size_t size;	// Taille du bloc

	struct meta_information* prev;
	struct meta_information* next;
	pthread_mutex_t mutex;
};

// RESSOURCES GLOBALES
extern size_t page_size;
extern int logs_file_descriptor;

extern struct struct_canary *data_pool;
extern struct meta_information *meta_information_pool_root;

extern size_t data_pool_size;
extern size_t meta_information_pool_size;

extern pthread_once_t already_initialized;
extern int dynamic_overflow_detection_activated;
extern pthread_mutex_t dynamic_overflow_detection_activated_mutex;

// FONCTIONS PRINCIPALES
void    my_free(void *ptr);
void    *my_malloc(size_t size);
void    *my_realloc(void *ptr, size_t size);
void    *my_calloc(size_t nmemb, size_t size);

#endif
