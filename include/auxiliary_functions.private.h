#ifndef _AUXILIARY_FUNCTIONS_PRIVATE_H_ /* garde d'inclusion pour éviter l'inclusion multiple */
#define _AUXILIARY_FUNCTIONS_PRIVATE_H_
#include <stddef.h> // size_t
#include <pthread.h> // pthread_mutex_t
#include "auxiliary_functions.private.h"

// GESTION DES RESSOURCES GLOBALES
void init_page_size();
void init_logs_file_descriptor();

long get_canary();
size_t 	get_page_size();
size_t get_data_pool_size();
int get_logs_file_descriptor();
struct struct_canary *get_data_pool();
size_t get_meta_information_pool_size();
struct meta_information *get_meta_information_pool_root();

// GESTION DES ERREURS
void handle_error(const char *error_message);
void handle_errnum(const char *function_name, int errnum);

// GESTION DES MUTEX
void mutex_lock(pthread_mutex_t *mutex_ptr);
void mutex_unlock(pthread_mutex_t *mutex_ptr);
int mutex_trylock(pthread_mutex_t *mutex_ptr);
void mutex_destroy(pthread_mutex_t *mutex_ptr);
void mutex_init(pthread_mutex_t *mutex_ptr, int recursive);

// DÉTECTION D'OVERFLOW
void *dynamic_overflow_detection(void *arg);

// FONCTIONS AUXILIAIRES GÉNÉRALES
size_t get_delta_size(size_t additional_memory_size);
void add_log(const char *string_format, int file_descriptor, ...);

// CRÉATION ET ÉLARGISSEMENT DE MAPPAGE DE MÉMOIRE
void exit_handler();
void	*init_memeory(void *memeory_to_init, void *address);
void	*remap_memeory(void *memeory_to_realloc, size_t memeory_old_size, size_t delta_size);

// INITIALISATION
void init();
void pthread_init_once();
struct struct_canary *init_data_pool();
struct meta_information *init_meta_information_pool();

// EXTENSION DES ZONES MÉMOIRE
void extend_meta_information_pool();
void extend_data_pool(struct meta_information* last_meta_information_item, size_t data_pool_delta_size, size_t last_meta_information_item_new_size);

// FONCTIONS POUVANT ÊTRE PASSÉES EN PARAMÈTRE À METADATA_LINKED_LIST_MAP OU METADATA_ARRAY_MAP
int clean_data(struct meta_information *meta_information_element, void *arg2);
int overflow_detection(struct meta_information *meta_information_element, void *arg2);
int is_last_meta_information_struct(struct meta_information *meta_information_element, void *arg2);
int init_empty_meta_information_struct(struct meta_information * meta_information_element, void *arg2);
int init_if_empty_meta_information_struct(struct meta_information * meta_information_element, void *arg2);
int is_meta_information_of_memory_ptr(struct meta_information * meta_information_element, void *memory_ptr);
int is_meta_information_of_free_memory(struct meta_information * meta_information_element, void *memory_size);

// GESTION DE LA LISTE CHAÎNÉE DES MÉTADONNÉES
struct meta_information *get_empty_meta_information_struct(struct meta_information *prev_meta_information_struct);
struct meta_information *metadata_linked_list_map(struct meta_information * meta_information_root, int return_if_func_true,
		int (*func) (struct meta_information *, void *), void *func_arg2, int unlock_mutex_before_return);
struct meta_information *metadata_array_map(struct meta_information * meta_information_root, int return_if_func_true,
		int (*func) (struct meta_information *, void *), void *func_arg2, size_t start_index, int unlock_mutex_before_return);

#endif
