/*
 * Le code contient des commentaires dont la source est le projet de pages de manuel Linux ou du manuel du programmeur POSIX
 * (The Linux man-pages project / POSIX Programmer's Manual)
 */
#include <string.h> // memcpy(), memset()
#include <stdlib.h> // exit(), EXIT_FAILURE
#include "auxiliary_functions.private.h"
#include "my_secmalloc.private.h"
#include "basic_operations.private.h"

// Les opérations fondamentales concernant l'allocation de mémoire sont :
// l'allocation, la libération, merge et remap

// Allocation d'espace mémoire (allocation - découpage de la zone mémoire)
void	*alloc(size_t size) {
	LOG("alloc(%lu) \n", size);

	// Obtention d'un pointeur sur une structure des métadonnées d'une partie de la mémoire
	// qui est libre et qui peut contenir au moins size octets.
	struct meta_information *meta_information_struct = get_free_chunck(size);
	LOG("Adresse du bloc de metadonnees obtenu %p\n", meta_information_struct);

	// Un pointeur vers le début de la zone mémoire qui sera transmise à la fonction appelante
	// (la zone mémoire vers laquelle pointe les métadonnées)
	void *ptr = (void*) meta_information_struct->data_ptr;
	LOG("Adresse du bloc de data obtenu : %p (taille du bloc : %lu) \n", ptr, meta_information_struct->size);

	memory_division(meta_information_struct, size);
	mutex_unlock(&(meta_information_struct->mutex));
	return ptr;
}


/**
 * La fonction memory_division() prend un bloc de métadonnées qui pointe vers une zone mémoire
 * d’au moins size octets. Si le nombre d'octets libres dans la zone mémoire est supérieur à size
 * de telle sorte qu'après l'allocation de size octets, il reste encore suffisamment d'espace pour
 * un struct chunck et pour au moins 1 octet de data, alors la zone mémoire est divisée en 2.
 * Si une division a été effectuée, la fonction renvoie 1, sinon elle renvoie 0.
 */
int  memory_division(struct meta_information *meta_information_struct, size_t size) {
	LOG("memory_division(%p, %lu) \n", meta_information_struct, size);

	// Variable booléenne pour indiquer si une division est nécessaire
	int make_division = 0;

	// Si nous faisons une division, nous aurons besoin d'un bloc de métadonnées supplémentaire
	struct meta_information *next_meta_information_struct = NULL;

	// Si le nombre d'octets libres dans la zone mémoire est supérieur à size de telle sorte qu'après
	// l'allocation de size octets, il reste encore suffisamment d'espace pour un struct chunck et pour
	// au moins 1 octet de data
	if (meta_information_struct->size > size + sizeof(struct struct_canary)) {
		make_division = 1;
		LOG("Etant donne que la taille du bloc est %lu et que la taille demandee est %lu, nous divisons le bloc \n", meta_information_struct->size, size);

		next_meta_information_struct = get_empty_meta_information_struct(meta_information_struct);
		LOG("L'adresse du bloc de metadonnees supplementaire qui pointera vers la zone memoire qui ne sera pas utilisee pour cette allocation : %p\n", next_meta_information_struct);

		next_meta_information_struct->size = meta_information_struct->size - (size + sizeof(struct struct_canary));
		LOG("La taille de la zone memoire nouvellement creee apres la division (et qui n'est pas utilisee pour cette allocation) : %lu\n", next_meta_information_struct->size);
	}
	// Si l'espace mémoire n'est pas assez grand pour le couper en 2, mais qu'il s'agit du dernier espace mémoire,
	// le problème peut être résolu en élargissant le pool de données.
	else if (meta_information_struct->next == NULL) {
		make_division = 1;
		LOG("Le bloc de memoire n'est pas assez grand pour etre partitionne, mais comme il s'agit du dernier bloc, nous pouvons l'etendre afin que l'espace restant, "
				"le cas echeant, puisse etre utilise pour une allocation future. \n");

		next_meta_information_struct = get_empty_meta_information_struct(meta_information_struct);
		LOG("L'adresse du bloc de metadonnees supplementaire qui pointera vers la zone memoire qui ne sera pas utilisee pour cette allocation : %p\n", next_meta_information_struct);

		extend_data_pool(next_meta_information_struct, page_size, (meta_information_struct->size + page_size) - (size + sizeof(struct struct_canary)));
		LOG("La taille de la zone memoire nouvellement creee apres la division (et qui n'est pas utilisee pour cette allocation) : %lu\n", next_meta_information_struct->size);
	} else {
		LOG("La zone memoire n'est pas assez grande pour etre divisible, et de plus, ce n'est pas le dernier bloc donc elle ne peut pas etre etendue en augmentant la taille du pool de data.\n");
	}

	if (make_division) {
		meta_information_struct->size = size;

		// Initialiser les métadonnées du prochain morceau
		next_meta_information_struct->status = FREE;
		next_meta_information_struct->data_ptr = (void*) ((size_t) meta_information_struct->data_ptr + size + sizeof(struct struct_canary));
		LOG("L'adresse de la zone memoire vers laquelle pointe le prochain bloc de metadonnees : %p\n", meta_information_struct->data_ptr);

		struct struct_canary *chunck_ptr = (struct struct_canary *) ((size_t) next_meta_information_struct->data_ptr + next_meta_information_struct->size);
		chunck_ptr->canary = get_canary();

		mutex_unlock(&(next_meta_information_struct->mutex));
	}

	struct struct_canary *chunck = (struct struct_canary *) ((size_t) meta_information_struct->data_ptr + meta_information_struct->size);
	chunck->canary = get_canary();

	meta_information_struct->status = BUSY;
	return make_division;
}

// Libération d'une allocation mémoire
int	clean(void *ptr) {
	LOG("clean(%p) \n", ptr);

	struct meta_information *metadata_of_ptr = metadata_linked_list_map(meta_information_pool_root, 1, is_meta_information_of_memory_ptr, ptr, 0);
	LOG("Le bloc de metadonnees qui pointe vers le bloc de donnees %p est %p \n", ptr, metadata_of_ptr);

	if (metadata_of_ptr == NULL || metadata_of_ptr->status == FREE)
		return 0;

	// Marquer le morceau comme libre
	metadata_of_ptr->status = FREE;

	// Nettoyage de l’espace mémoire
	// void * memset(void * block, int value, size_t size);
	memset(ptr, 0, metadata_of_ptr->size);

	if (overflow_detection(metadata_of_ptr, NULL)) {
		mutex_unlock(&(metadata_of_ptr->mutex));
		LOG_ERROR("Detection d'overflow : bloc mémoire commençant à l'adresse %p (l'adresse du bloc de metadonnees concerne est %p) \n",
				metadata_of_ptr->data_ptr, metadata_of_ptr);
		exit(EXIT_FAILURE);
	}

	mutex_unlock(&(metadata_of_ptr->mutex));

	// Merge les blocs consécutifs
	// L'idée est que si la libération du fragment actuel a lieu avant ou après d'autres fragments libres, ils peuvent être fusionnés.
	// A cette occasion on parcourt toute la zone mémoire et on fusionne tous les blocs libres consécutifs
	metadata_linked_list_map(meta_information_pool_root, 0, merge_if_free, NULL, 1);
	return 1;
}

int merge_if_free(struct meta_information * meta_information_element, void *arg2) {
	(void) arg2;
	int flag = 0;

	// Si le morceau est libre
	if (meta_information_element-> status == FREE) {
		size_t new_size = meta_information_element->size;

		// Puisque que metadata_element est libre,
		// nous vérifions les morceaux de mémoire suivants, tant qu'ils sont libres

		struct meta_information *prev_prev_metadata_element = NULL;
		struct meta_information *prev_metadata_element = meta_information_element;
		struct meta_information *curr_metadata_element = prev_metadata_element->next;
		struct meta_information *next_metadata_element = NULL;

		if (curr_metadata_element != NULL)
			mutex_lock(&(curr_metadata_element->mutex));

		while (curr_metadata_element != NULL) {
			next_metadata_element = curr_metadata_element->next;
			if (next_metadata_element != NULL)
				mutex_lock(&(next_metadata_element->mutex));

			if (prev_prev_metadata_element != NULL && prev_prev_metadata_element != meta_information_element) {
				mutex_unlock(&(prev_prev_metadata_element->mutex));
			}

			if (curr_metadata_element->status == FREE) {
				// Mise à jour de la taille de l'espace mémoire libre attendu après la fusion
				new_size += curr_metadata_element->size + sizeof(struct struct_canary);

				// Puisque nous fusionnons les espaces mémoire, ce bloc de métadonnées n'est plus nécessaire
				curr_metadata_element->status = UNUSED;
				curr_metadata_element->size = 0;
				curr_metadata_element->data_ptr = NULL;
				curr_metadata_element->prev = NULL;
				curr_metadata_element->next = NULL;

				// Lier l'élément précédent avec l'élément suivant
				meta_information_element->next = next_metadata_element;
				// Définir l'élément précédent de cet élément comme l'élément précédent de l'élément suivant
				if (next_metadata_element != NULL) {
					next_metadata_element->prev = meta_information_element;
				}

				DEBUG("new size : %lu consecutive check %p size %lu - status %u\n", new_size, curr_metadata_element,
						curr_metadata_element->size, curr_metadata_element->status);

				prev_prev_metadata_element = prev_metadata_element;
				prev_metadata_element = curr_metadata_element;
				curr_metadata_element = next_metadata_element;
			} else {
				if (next_metadata_element != NULL) {
					mutex_unlock(&(next_metadata_element->mutex));
				}

				mutex_unlock(&(curr_metadata_element->mutex));

				if (prev_metadata_element != meta_information_element) {
					mutex_unlock(&(prev_metadata_element->mutex));
				}

				flag = 1;
				break;
			}
		}

		if (!flag) {
			if (prev_metadata_element != meta_information_element) {
				mutex_unlock(&(prev_metadata_element->mutex));
			}

			if (prev_prev_metadata_element != NULL && prev_prev_metadata_element != meta_information_element)
				mutex_unlock(&(prev_prev_metadata_element->mutex));
		}

		// void * memcpy (void *restrict to, const void *restrict from, size_t size)
		memcpy((void*) ((size_t) meta_information_element->data_ptr + new_size),
				(void*) ((size_t) meta_information_element->data_ptr + meta_information_element->size), sizeof(struct struct_canary));

		if (meta_information_element->size != new_size) {
			LOG("Apres la tentative de fusion de blocs vides consécutifs, la nouvelle taille est %lu (taille précédente : %lu) \n", new_size, meta_information_element->size);
			meta_information_element->size = new_size;
		}
	}

	return 0;
}

/**
 * La fonction get_last_chunck_raw() renvoie un pointeur sur la structure des métadonnées
 * de la dernière partie de la mémoire.
 */
struct meta_information	*get_last_chunck_raw() {
	LOG("get_last_chunck_raw() \n");

	struct meta_information	* last_meta_information_struct = NULL;
	last_meta_information_struct = metadata_linked_list_map(meta_information_pool_root, 1,
			is_last_meta_information_struct, NULL, 0);

	if (last_meta_information_struct->status == BUSY) {
		return get_empty_meta_information_struct(last_meta_information_struct);
	}

	return last_meta_information_struct;
}

struct meta_information	*get_free_chunck(size_t size) {
	LOG("get_free_chunck(%lu) \n", size);

	// Si le tas n'a pas encore été initialisé
	if (data_pool == NULL || meta_information_pool_root == NULL) {
		pthread_init_once();
	}

	// Une tentative d'obtenir un pointeur sur une structure des métadonnées
	// d'une partie de la mémoire qui est libre et qui peut contenir au moins size octets.
	struct meta_information* item = metadata_linked_list_map(meta_information_pool_root, 1, is_meta_information_of_free_memory, (void*) &size, 0);
	DEBUG("item : %p \n", item);

	// Si aucun morceau de mémoire libre de la taille appropriée n'est trouvé
	if (item == NULL) {
		LOG("Aucun bloc libre de taille %lu n'a pu etre trouve \n", size);

		// tok_chunck : la taille de l'espace mémoire supplémentaire dont nous avons besoin
		size_t tok_chunck = size + sizeof(struct struct_canary);
		size_t delta_size =  get_delta_size(tok_chunck);

		// Obtenir un pointeur vers le dernier morceau
		struct meta_information* last_meta_information_item = get_last_chunck_raw();
		extend_data_pool(last_meta_information_item, delta_size, last_meta_information_item->size + delta_size);
		mutex_unlock(&(last_meta_information_item->mutex));

		item = metadata_linked_list_map(meta_information_pool_root, 1, is_meta_information_of_free_memory, (void*) &size, 0);
		DEBUG("last chunk %p\n", item);
	}
	return item;
}
