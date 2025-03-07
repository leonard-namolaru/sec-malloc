#include <criterion/criterion.h>
#include <unistd.h> // sleep()
#include <pthread.h> // pthread_create(), pthread_exit(), pthread_join()
#include <sys/types.h> // SIGUSR1
#include <signal.h> // SIGUSR1
#include "my_secmalloc.private.h"
#include <sys/mman.h>
#include "auxiliary_functions.private.h"

/* ****************************************************************** */
/* ******* PROPRIÉTÉS QU'UNE ALLOCATION MÉMOIRE DOIT RESPECTER ****** */
/* ****************************************************************** */

// Les allocations de mémoire doivent se suivre
void are_memory_allocations_consecutive(const char *test_name, void *ptr1, void *ptr2, size_t ptr1_size) {
	 cr_assert(((size_t) ptr2 == (size_t) ptr1 + ptr1_size + sizeof(struct struct_canary)),
			 "%s : Les allocations de mémoire ne sont pas placées les unes après les autres, %lx - %lx = %ld",
			 test_name, (size_t) ptr2, (size_t) ptr1 + ptr1_size + sizeof(struct struct_canary),
			 (size_t) ptr2 - ((size_t) ptr1 + ptr1_size + sizeof(struct struct_canary)));
}

// my_malloc ne doit pas retourner NULL
void my_malloc_should_not_return_null(const char *test_name, void *ptr, size_t ptr1_size) {
	 cr_assert(ptr != NULL, "%s : my_malloc(%lu) a échoué et a renvoyé NULL", test_name, ptr1_size);
}

void metadata_should_correctly_represent_memory_allocation(const char *test_name, void *data_ptr, struct meta_information *metadata, size_t data_size) {
	 cr_assert(metadata != NULL, "%s : le bloc de métadonnées représentant l'allocation de mémoire à l'adresse %p "
			 "(taille : %lu) est introuvable", test_name, data_ptr, data_size);
	 cr_assert(metadata->data_ptr == data_ptr, "%s : Le bloc de métadonnées à l'adresse %p ne pointe pas vers "
			 "l'allocation mémoire de l'adresse %p (il pointe vers %p)", test_name, metadata, data_ptr, metadata->data_ptr);

	 cr_assert(metadata->size == data_size || metadata->size - data_size <= sizeof(struct struct_canary),
			 "%s : la taille réellement allouée (%lu) doit être la même que celle demandée par l'utilisateur (%lu) "
			 "(sauf s'il y a un excédent qui n'est pas suffisant pour une allocation mémoire supplémentaire)", test_name, metadata->size, data_size);

	 cr_assert(metadata->next != NULL, "Un bloc de métadonnées d'une allocation mémoire nouvellement créé doit pointer vers un autre bloc de métadonnées");
}

void merge_blocks_when_release_before_or_after_free_block(const char *test_name, struct meta_information *released_block_metadata, size_t expected_new_size) {
	cr_assert(released_block_metadata->size == expected_new_size, "%s : après avoir libéré une allocation mémoire avant "
			"et / ou après un espace mémoire déjà vide, on s'attendait à ce que la taille après la fusion soit %lu, mais la taille obtenue est %lu.",
			test_name, expected_new_size, released_block_metadata->size);
}

/* ****************************************************************** */
/* ************************* MODÈLES DE TESTS *********************** */
/* ****************************************************************** */

byte* create_and_test_memory_allocation(const char *test_name, size_t size) {
	byte *ptr = my_malloc(size);
	my_malloc_should_not_return_null(test_name, ptr, size);
	return ptr;
}

void create_and_test_2_memory_allocations(const char *test_name, byte **ptr_to_ptr1,
		byte **ptr_to_ptr2, size_t malloc_size1, size_t malloc_size2) {
	*ptr_to_ptr1 = create_and_test_memory_allocation(test_name, malloc_size1);
	*ptr_to_ptr2 = create_and_test_memory_allocation(test_name, malloc_size2);

	are_memory_allocations_consecutive(test_name, (void *) *ptr_to_ptr1, (void *) *ptr_to_ptr2, malloc_size1);
}

struct meta_information  *get_and_test_meta_info_of_memory_allocation(const char *test_name, void *ptr, size_t data_size) {
	struct meta_information *metadata_of_ptr = metadata_linked_list_map(meta_information_pool_root, 1, is_meta_information_of_memory_ptr, ptr, 0);
	metadata_should_correctly_represent_memory_allocation(test_name, ptr, metadata_of_ptr, data_size);

	mutex_unlock(&(metadata_of_ptr->mutex));
	return metadata_of_ptr;
}

void *free_and_test(const char *test_name, void *ptr, size_t prev_free_block_size, size_t next_free_block_size, size_t data_size) {
	struct meta_information *metadata_of_ptr = get_and_test_meta_info_of_memory_allocation(test_name, ptr, data_size);

	struct meta_information *prev = metadata_of_ptr->prev;

	enum status prev_status = (prev != NULL) ? prev->status : UNUSED;
	enum status next_status = (metadata_of_ptr->next != NULL) ? metadata_of_ptr->next->status : UNUSED;

	my_free(ptr);

	if (prev_status == FREE) {
		if (next_status == FREE)
			merge_blocks_when_release_before_or_after_free_block(test_name, prev, prev_free_block_size + sizeof(struct struct_canary)
					+ data_size + sizeof(struct struct_canary) + next_free_block_size);
		else
			merge_blocks_when_release_before_or_after_free_block(test_name, prev, prev_free_block_size + sizeof(struct struct_canary)
					+ data_size);
		return prev->data_ptr;
	} else if (next_status == FREE) {
		merge_blocks_when_release_before_or_after_free_block(test_name, metadata_of_ptr, data_size + sizeof(struct struct_canary)
				+ next_free_block_size);
		return ptr;
	} else {
		metadata_should_correctly_represent_memory_allocation(test_name, ptr, metadata_of_ptr, data_size);
		return ptr;
	}
}

/* ****************************************************************** */
/* ************************* TESTS GÉNÉRAUX ************************* */
/* ****************************************************************** */

// Utilisation simple d'un mmap() et de munmap()
Test(my_secmalloc, simple_test_01) {
    void *ptr = NULL;
    ptr = init_memeory(ptr, NULL);
	cr_assert(ptr != NULL, "Failed to mmap");

    int res = munmap(ptr, get_page_size());
    cr_expect(res == 0);
}

/* ****************************************************************** */
/* ********************* TESTS POUR MY_MALLOC *********************** */
/* ****************************************************************** */

// Les allocations de mémoire doivent se suivre
Test(my_secmalloc, test_my_malloc_01) {
	const char *test_name = "test_my_malloc_01";
	size_t malloc_size1 = 12;
	size_t malloc_size2 = 25;
	size_t malloc_size3 = 55;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);
}

Test(my_secmalloc, test_my_malloc_02) {
	const char *test_name = "test_my_malloc_02";
	size_t malloc_size1 = 8192;
	size_t malloc_size2 = 80192;
	size_t malloc_size3 = 80192;
	size_t malloc_size4 = 80192;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	byte *ptr4 = create_and_test_memory_allocation(test_name, malloc_size4);
	are_memory_allocations_consecutive(test_name, (void *) ptr3, (void *) ptr4, malloc_size3);
}

// Allocation de toute la mémoire restante de telle sorte qu'il
// ne reste plus de mémoire pour la prochaine allocation
Test(my_secmalloc, test_my_malloc_03) {
	const char *test_name = "test_my_malloc_03";
	size_t malloc_size1 = get_page_size() - sizeof(struct struct_canary) - 1 - sizeof(struct struct_canary);
	size_t malloc_size2 = 1;
	size_t malloc_size3 = 1;
	size_t malloc_size4 = 1;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	byte *ptr4 = create_and_test_memory_allocation(test_name, malloc_size4);
	are_memory_allocations_consecutive(test_name, (void *) ptr3, (void *) ptr4, malloc_size3);
}

// Effectuer un grand nombre d'allocations de mémoire qui nécessite
// d'étendre l'espace mémoire des métadonnées
Test(my_secmalloc, test_my_malloc_04) {
	const char *test_name = "test_my_malloc_04";
	size_t malloc_size = get_page_size();

	byte *prev_ptr = NULL;
	for (size_t i = 0; i < get_page_size(); i++) {
		byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
		if (prev_ptr != NULL) {
			are_memory_allocations_consecutive(test_name, (void *) prev_ptr, (void *) ptr, malloc_size);
		}

		prev_ptr = ptr;
	}
}


Test(my_secmalloc, test_my_malloc_05) {
	const char *test_name = "test_my_malloc_05";
	size_t malloc_size = 1;

	byte *prev_ptr = NULL;
	for (size_t i = 0; i < get_page_size(); i++) {
		byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
		if (prev_ptr != NULL) {
			are_memory_allocations_consecutive(test_name, (void *) prev_ptr, (void *) ptr, malloc_size);
		}
		prev_ptr = ptr;
	}
}


/* ****************************************************************** */
/* ********************* TESTS POUR MY_FREE ************************* */
/* ****************************************************************** */

// Vérifier la fusion des espaces libres après la libération d'une allocation # 1
Test(my_secmalloc, test_my_free_01) {
	const char *test_name = "test_my_free_01";
	size_t malloc_size1 = 12;
	size_t malloc_size2 = 25;
	size_t malloc_size3 = 55;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	size_t next_free_block_size = get_page_size() - (malloc_size1 + sizeof(struct struct_canary) + malloc_size2
			+ sizeof(struct struct_canary) + malloc_size3 + sizeof(struct struct_canary) + sizeof(struct struct_canary));

	free_and_test(test_name, ptr2, 0, 0, malloc_size2);
	free_and_test(test_name, ptr3, malloc_size2, next_free_block_size, malloc_size3);
}


// Vérifier la fusion des espaces libres après la libération d'une allocation # 2
Test(my_secmalloc, test_my_free_02) {
	const char *test_name = "test_my_free_02";
	size_t malloc_size1 = 12;
	size_t malloc_size2 = 25;
	size_t malloc_size3 = 55;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	free_and_test(test_name, ptr1, 0, 0, malloc_size1);
	free_and_test(test_name, ptr2, malloc_size1, 0, malloc_size2);
}

// Réallocation d'un espace mémoire libéré
Test(my_secmalloc, test_my_free_03) {
	const char *test_name = "test_my_free_03";
	size_t malloc_size1 = 12;
	size_t malloc_size2 = 28;
	size_t malloc_size3 = 55;
	size_t malloc_size4 = 10;
	size_t malloc_size5 = 2;
	size_t malloc_size6 = 200;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	free_and_test(test_name, ptr2, 0, 0, malloc_size2);

	byte *ptr4 = create_and_test_memory_allocation(test_name, malloc_size4);
	cr_assert(ptr4 == ptr2, "Cette allocation de mémoire était censée remplacer l'allocation "
			"précédemment libérée : ptr4 %lx != ptr2 %lx", (size_t)ptr4, (size_t)ptr2);

	byte *ptr5 = create_and_test_memory_allocation(test_name, malloc_size5);
	are_memory_allocations_consecutive(test_name, (void *) ptr4, (void *) ptr5, malloc_size4);

	byte *ptr6 = create_and_test_memory_allocation(test_name, malloc_size6);
	are_memory_allocations_consecutive(test_name, (void *) ptr3, (void *) ptr6, malloc_size3);
}


/* ****************************************************************** */
/* ********************* TESTS POUR MY_REALLOC ********************** */
/* ****************************************************************** */

// Faire un appel à my_realloc avec la même taille ne devrait rien changer
Test(my_secmalloc, test_my_realloc_01) {
	const char *test_name = "test_my_realloc_01";
	size_t malloc_size = 5;

	byte *ptr1 = create_and_test_memory_allocation(test_name, malloc_size);
	struct meta_information *metadata_of_ptr1 = get_and_test_meta_info_of_memory_allocation(test_name, ptr1, malloc_size);

	byte *ptr2 = my_realloc(ptr1, malloc_size);
	cr_assert(ptr2 == ptr1, "Un appel à my_realloc avec la même taille de mémoire devrait renvoyer un pointeur vers la même "
			"zone mémoire ptr2 %lx != ptr1 %lx", (size_t) ptr2, (size_t) ptr1);

	struct meta_information *metadata_of_ptr2 = get_and_test_meta_info_of_memory_allocation(test_name, ptr2, malloc_size);
	cr_assert(metadata_of_ptr1 == metadata_of_ptr2, "Un appel à my_realloc ne devrait pas modifier l'adresse du bloc de métadonnées"
			" metadata_of_ptr1 %lx != metadata_of_ptr2 %lx", (size_t) metadata_of_ptr1, (size_t) metadata_of_ptr2);
}

// Une réduction de l'espace mémoire de telle manière qu'il reste suffisamment d'espace pour une allocation supplémentaire
Test(my_secmalloc, test_my_realloc_02) {
	const char *test_name = "test_my_realloc_02";
	size_t malloc_size = 15;
	size_t realloc_size = 1;

	byte *ptr1 = create_and_test_memory_allocation(test_name, malloc_size);
	struct meta_information *metadata_of_ptr1 = get_and_test_meta_info_of_memory_allocation(test_name, ptr1, malloc_size);

	byte *ptr2 = my_realloc(ptr1, realloc_size);
	cr_assert(ptr2 == ptr1, "Un appel à my_realloc avec une taille de mémoire plus petite devrait renvoyer un "
			"pointeur vers la même zone mémoire ptr2 %lx != ptr1 %lx", (size_t) ptr2, (size_t) ptr1);

	// Ce test a pour but de s'assurer que metadata_of_ptr2->size == realloc_size
	struct meta_information *metadata_of_ptr2 = get_and_test_meta_info_of_memory_allocation(test_name, ptr2, realloc_size);

	cr_assert(metadata_of_ptr1 == metadata_of_ptr2, "Un appel à my_realloc ne devrait pas modifier l'adresse du bloc de métadonnées"
			" metadata_of_ptr1 %lx != metadata_of_ptr2 %lx", (size_t) metadata_of_ptr1, (size_t) metadata_of_ptr2);
}

// Réduire l'espace mémoire de telle manière qu'il ne reste pas de place pour une allocation supplémentaire
// La surface restante est inférieure ou égale à sizeof(struct chunck)
Test(my_secmalloc, test_my_realloc_03) {
	const char *test_name = "test_my_realloc_03";
	size_t malloc_size = 5;
	size_t realloc_size = 1;

	byte *ptr1 = create_and_test_memory_allocation(test_name, malloc_size);
	struct meta_information *metadata_of_ptr1 = get_and_test_meta_info_of_memory_allocation(test_name, ptr1, malloc_size);

	size_t next_data_ptr_before = (size_t) metadata_of_ptr1->next->data_ptr;

	byte *ptr2 = my_realloc(ptr1, realloc_size);
	cr_assert(ptr2 == ptr1, "Un appel à my_realloc avec une taille de mémoire plus petite devrait renvoyer un "
			"pointeur vers la même zone mémoire ptr2 %lx != ptr1 %lx", (size_t) ptr2, (size_t) ptr1);

	// Ce test a pour but de s'assurer que metadata_of_ptr2->size == realloc_size
	struct meta_information *metadata_of_ptr2 = get_and_test_meta_info_of_memory_allocation(test_name, ptr2, realloc_size);

	cr_assert(metadata_of_ptr1 == metadata_of_ptr2, "Un appel à my_realloc ne devrait pas modifier l'adresse du bloc de métadonnées"
			" metadata_of_ptr1 %lx != metadata_of_ptr2 %lx", (size_t) metadata_of_ptr1, (size_t) metadata_of_ptr2);

	cr_assert(next_data_ptr_before - (size_t) metadata_of_ptr2->next->data_ptr == (malloc_size - realloc_size),
			"Après avoir appelé my_realloc(), l'adresse de la prochaine allocation de mémoire était censée reculer de %lu octets : %lx != %lx",
			malloc_size - realloc_size, next_data_ptr_before, (size_t) metadata_of_ptr2->next->data_ptr);
}

// Augmentation de la taille d'une allocation mémoire
Test(my_secmalloc, test_my_realloc_04) {
	const char *test_name = "test_my_realloc_04";
	size_t malloc_size = 50;
	size_t realloc_size = 100;

	byte *ptr1 = create_and_test_memory_allocation(test_name, malloc_size);

	byte *ptr2 = my_realloc(ptr1, realloc_size);
	cr_assert(ptr2 == ptr1, "L'augmentation de la taille d'une allocation mémoire lorsqu'il y a suffisamment d'espace libre après"
			" l'allocation ne devrait pas modifier l'adresse de l'espace mémoire ptr2 %lx != ptr1 %lx", (size_t) ptr2, (size_t) ptr1);
}


// Augmentation d'une allocation mémoire sur une zone d'allocation libérée
Test(my_secmalloc, test_my_realloc_05) {
	const char *test_name = "test_my_realloc_05";
	size_t malloc_size1 = 50;
	size_t malloc_size2 = 100;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size2);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	free_and_test(test_name, ptr2, malloc_size1, malloc_size2, malloc_size2);

	byte *ptr4 = my_realloc(ptr1, malloc_size2);
	cr_assert(ptr4 == ptr1, "L'augmentation de la taille d'une allocation mémoire lorsqu'il y a suffisamment d'espace libre après"
			" l'allocation ne devrait pas modifier l'adresse de l'espace mémoire ptr4 %lx != ptr1 %lx", (size_t) ptr4, (size_t) ptr1);
}


// Augmentation d'une allocation mémoire sur une zone d'allocation libérée
// Cas où l'espace mémoire libre correspond tout juste a l'espace nécessaire
// pour augmenter l'allocation mémoire
Test(my_secmalloc, test_my_realloc_06) {
	const char *test_name = "test_my_realloc_06";
	size_t malloc_size1 = 50;
	size_t malloc_size2 = 50 - sizeof(struct struct_canary);
	size_t malloc_size3 = 100;
	size_t remalloc_size = 100;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size1, malloc_size2);

	byte *ptr3 = create_and_test_memory_allocation(test_name, malloc_size3);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size2);

	free_and_test(test_name, ptr2, malloc_size1, malloc_size2, malloc_size2);

	byte *ptr4 = my_realloc(ptr1, remalloc_size);
	cr_assert(ptr4 == ptr1, "L'augmentation de la taille d'une allocation mémoire lorsqu'il y a suffisamment d'espace libre après"
			" l'allocation ne devrait pas modifier l'adresse de l'espace mémoire ptr4 %lx != ptr1 %lx", (size_t) ptr4, (size_t) ptr1);
}


Test(my_secmalloc, test_my_realloc_07) {
	const char *test_name = "test_my_realloc_07";
	size_t malloc_size = 50;
	size_t realloc_size = 10000;

	byte *ptr1;
	byte *ptr2;
	create_and_test_2_memory_allocations(test_name, &ptr1, &ptr2, malloc_size, malloc_size);

	byte *ptr3 = my_realloc(ptr1, realloc_size);
	are_memory_allocations_consecutive(test_name, (void *) ptr2, (void *) ptr3, malloc_size);
}


/* ****************************************************************** */
/* **************** DÉTECTION DE MALVEILLANCE *********************** */
/* ****************************************************************** */

Test(my_secmalloc, test_overflow_01, .exit_code = EXIT_SUCCESS) {
	const char *test_name = "test_overflow_01";
	size_t malloc_size = 12;

	byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
	ptr[malloc_size - 1] = 't';
}

// Détection dynamique de l’overflow
Test(my_secmalloc, test_overflow_02, .exit_code = EXIT_FAILURE) {
	const char *test_name = "test_overflow_02";
	size_t malloc_size = 12;

	byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
	ptr[malloc_size] = 't';

	// Pour éviter les messages de type
	// "Warning! The test `my_secmalloc::test_overflow_02` exited during its setup or teardown."
	sleep(2);
}

Test(my_secmalloc, test_overflow_03, .exit_code = EXIT_SUCCESS) {
	const char *test_name = "test_overflow_03";
	size_t malloc_size = 12;

	byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
	*(ptr + malloc_size) = get_canary();
}

Test(my_secmalloc, test_overflow_04, .signal = SIGUSR1) {
	const char *test_name = "test_overflow_04";
	size_t malloc_size = 12;

	byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
	my_free(ptr);
	my_free(ptr);
}

Test(my_secmalloc, test_overflow_05, .signal = SIGUSR1) {
	my_free((void*) 45678);
}

Test(my_secmalloc, test_overflow_06, .signal = SIGUSR1) {
	my_realloc((void*) 45678, 20);
}

// Détection de l’overflow après une demande de libération de l'espace mémoire
Test(my_secmalloc, test_overflow_07, .exit_code = EXIT_FAILURE) {
	const char *test_name = "test_overflow_07";
	size_t malloc_size = 12;

	byte *ptr = create_and_test_memory_allocation(test_name, malloc_size);
	ptr[malloc_size] = 't';
	my_free(ptr);
}

/* ****************************************************************** */
/* ********************* MULTITHREADING ***************************** */
/* ****************************************************************** */

void *allocation_thread(void *arg) {
	size_t malloc_size = *((size_t*) arg);
	byte *ptr = my_malloc(malloc_size);

	// void pthread_exit(void *retval);
	pthread_exit((void *) ptr);
}

void *free_thread(void *arg) {
	my_free(arg);
	pthread_exit((void *) NULL);
}

Test(my_secmalloc, test_multithreading_01) {
	const char *test_name = "test_multithreading_01";
	size_t malloc_size = 12;
	size_t threads_nb = 4;

	pthread_t threads[threads_nb];
	for(size_t i = 0; i < threads_nb; i++) {
		// int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
		int pthread_create_result = pthread_create(&threads[i], NULL, allocation_thread, (void*) &malloc_size);
		if (pthread_create_result != 0)
			cr_assert(0, "%s : Echec de la fonction pthread_create()", test_name);
	}

	for(size_t i = 0; i < threads_nb; i++) {
		byte *ptr = NULL;

		// int pthread_join(pthread_t thread, void **value_ptr);
		int pthread_join_result = pthread_join(threads[i], (void **) &ptr);
		if (pthread_join_result != 0)
			cr_assert(0, "%s : Echec de la fonction pthread_join()", test_name);

		my_malloc_should_not_return_null(test_name, ptr, malloc_size);
		get_and_test_meta_info_of_memory_allocation(test_name, ptr, malloc_size);
	}
}

Test(my_secmalloc, test_multithreading_02) {
	const char *test_name = "test_multithreading_02";
	size_t malloc_size = 12;
	size_t threads_nb = 4;

	pthread_t threads[threads_nb];
	for(size_t i = 0; i < threads_nb; i++) {
		// int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
		int pthread_create_result = pthread_create(&threads[i], NULL, allocation_thread, (void*) &malloc_size);
		if (pthread_create_result != 0)
			cr_assert(0, "%s : Echec de la fonction pthread_create()", test_name);
	}

	for(size_t i = 0; i < threads_nb; i++) {
		byte *ptr = NULL;

		// int pthread_join(pthread_t thread, void **value_ptr);
		int pthread_join_result = pthread_join(threads[i], (void **) &ptr);
		if (pthread_join_result != 0)
			cr_assert(0, "%s : Echec de la fonction pthread_join()", test_name);

		my_malloc_should_not_return_null(test_name, ptr, malloc_size);
		get_and_test_meta_info_of_memory_allocation(test_name, ptr, malloc_size);

		int pthread_create_result = pthread_create(&threads[i], NULL, free_thread, (void*) ptr);
		if (pthread_create_result != 0)
			cr_assert(0, "%s : Echec de la fonction pthread_create()", test_name);
	}

	for(size_t i = 0; i < threads_nb; i++) {
		int pthread_join_result = pthread_join(threads[i], NULL);
		if (pthread_join_result != 0)
			cr_assert(0, "%s : Echec de la fonction pthread_join()", test_name);
	}

	size_t size_after = get_page_size() - sizeof(struct struct_canary);
	struct meta_information* item = metadata_linked_list_map(meta_information_pool_root, 1, is_meta_information_of_free_memory, (void*) &size_after, 1);
	cr_assert(item != NULL, "Une fois toutes les allocations de memoire liberees, il devrait y avoir un bloc de taille %lu", size_after);
}
