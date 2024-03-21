#ifndef DBLL_H
#define DBLL_H
	#include <stdint.h>
	#include <stddef.h>
	
	// DBLL_OK and DBLL_ERR are used in functions that return an int
	// for error handling, unless it's a function that uses int to return a bool
	#define DBLL_OK 0
	#define DBLL_ERR -1
	#define DBLL_MAGIC_SIZE 4
	#define DBLL_PTR_MAX 8
	#define DBLL_SIZE_MAX 4
	#define DBLL_NULL 0
	typedef uint64_t dbll_ptr_t;
	typedef uint32_t dbll_size_t;
	typedef struct {
		uint8_t *mem;
		size_t size;
		int desc;
	} dbll_file_t;

	int dbll_file_valid(dbll_file_t *);
	int dbll_file_load(dbll_file_t *, const char *);
	int dbll_file_unload(dbll_file_t *);
	int dbll_file_make(dbll_file_t *, const char *);
	int dbll_file_change(dbll_file_t *, size_t);
	typedef struct {
		char magic[DBLL_MAGIC_SIZE];
		uint8_t ptr_size;
		uint8_t data_size;
		dbll_ptr_t empty_slot_ptr;

		// not in file, computed in dbll_header_load
		int header_size;
		int list_size;
		int empty_slot_size;

		// the size of "free" data in data_slot_t
		int data_slot_size;
	} dbll_header_t;

	struct dbll_state_s;
	int dbll_header_valid(dbll_header_t *);
	int dbll_header_load(dbll_header_t *, dbll_file_t *);
	int dbll_header_write(
		dbll_header_t *, 
		struct dbll_state_s *
	);
	
	typedef struct {
	
		// to dbll_list_t
		dbll_ptr_t head_ptr;
		
		// to dbll_list_t
		dbll_ptr_t tail_ptr;

		// to int (file index), size is multiple of list structure size
		dbll_ptr_t data_ptr;

		// data_size is multiples of list size
		dbll_size_t data_size;

		// not in data, used by library
		dbll_ptr_t this_ptr;
	} dbll_list_t;

	typedef enum {
		DBLL_GO_HEAD,
		DBLL_GO_TAIL
	} list_go_e;

	struct dbll_data_slot_s;
	int dbll_list_valid(dbll_list_t *);
	int dbll_list_load(
		dbll_list_t *, 
		struct dbll_state_s *, 
		dbll_ptr_t
	);
	
	int dbll_list_unload(dbll_list_t *);
	int dbll_list_go(
		dbll_list_t *, 
		struct dbll_state_s *, 
		list_go_e
	);
	
	int dbll_list_data_index(
		dbll_list_t *, 
		struct dbll_state_s *
	);

	int dbll_list_data_alloc(
		dbll_list_t *,
		struct dbll_state_s *,
		int
	);
	
	int dbll_list_data_resize(
		dbll_list_t *,
		struct dbll_state_s *,
		int
	);
	
	typedef struct {

		// to dbll_list_t
		// this_ptr points to itself because empty slots
		// will take up freed slots so when a new slot needs
		// to be used we can juse use the current empty slot
		dbll_ptr_t this_ptr;
		
		// to dbll_empty_slot_t
		dbll_ptr_t prev_ptr;
	
		// to dbll_empty_slot_t
		dbll_ptr_t next_ptr; 
	} dbll_empty_slot_t;

	int dbll_empty_slot_valid(dbll_empty_slot_t *);
	int dbll_empty_slot_valid_ptr(
		struct dbll_state_s *, 
		dbll_ptr_t
	);
	
	int dbll_empty_slot_load(
		dbll_empty_slot_t *, 
		struct dbll_state_s *, 
		dbll_ptr_t
	);

	int dbll_empty_slot_unload(dbll_empty_slot_t *);
	int dbll_empty_slot_write(
		dbll_empty_slot_t *, 
		struct dbll_state_s *
	);

	int dbll_empty_slot_clip(
		dbll_empty_slot_t *, 
		struct dbll_state_s *
	);
	
	typedef struct dbll_data_slot_s {
		dbll_ptr_t next_ptr;

		// not in data, used in library for
		// converting a page index into a file index
		int data_index;
		
		// not in data, used in library for 
		// preventing cyclic loops
		uint8_t is_marked;

		// again, not in data, used in library
		// for freeing data slots
		dbll_ptr_t this_ptr;
	} dbll_data_slot_t;

	int dbll_data_slot_valid(dbll_data_slot_t *);
	int dbll_data_slot_load(
		dbll_data_slot_t *, 
		struct dbll_state_s *, 
		dbll_ptr_t
	);
	
	int dbll_data_slot_unload(dbll_data_slot_t *);
	int dbll_data_slot_next(
		dbll_data_slot_t *, 
		struct dbll_state_s *
	);
	
	int dbll_data_slot_free(
		dbll_data_slot_t *, 
		struct dbll_state_s *
	);
	
	int dbll_data_slot_page(
		dbll_data_slot_t *, 
		struct dbll_state_s *, 
		int
	);

	int dbll_data_slot_alloc(
		dbll_data_slot_t *,
		struct dbll_state_s *,
		int
	);

	int dbll_data_slot_write(
		dbll_data_slot_t *,
		struct dbll_state_s *
	);

	dbll_ptr_t dbll_data_slot_last(
		dbll_data_slot_t *,
		struct dbll_state_s *
	);

	int dbll_data_slot_cut_end(
		dbll_data_slot_t *,
		struct dbll_state_s *,
		int
	);
	
	typedef struct dbll_state_s {
		dbll_file_t file;
		dbll_header_t header;
		dbll_empty_slot_t last_empty;
		dbll_list_t root_list;
	} dbll_state_t;

	int dbll_state_valid(dbll_state_t *);
	int dbll_state_load(dbll_state_t *, const char *);
	int dbll_state_unload(dbll_state_t *);
	int dbll_state_make(dbll_state_t *, const char *);
	int dbll_state_make_replace(
		dbll_state_t *,
		const char *
	);
	
	dbll_ptr_t dbll_state_empty_find(dbll_state_t *);
	dbll_ptr_t dbll_state_alloc(dbll_state_t *);
	int dbll_state_mark_free(dbll_state_t *, dbll_ptr_t);
	int dbll_state_trim(dbll_state_t *);
	int dbll_state_compact(dbll_state_t *);
	int dbll_state_write(
		dbll_state_t *,
		int,
		uint8_t *,
		size_t
	);

	int dbll_state_read(
		dbll_state_t *,
		int,
		uint8_t *,
		size_t
	);
	
	int dbll_ptr_index_copy(
		dbll_state_t *, 
		dbll_ptr_t,
		int
	);

	int dbll_index_ptr_copy(
		dbll_state_t *,
		int,
		dbll_ptr_t *
	);
	
	dbll_ptr_t dbll_index_to_ptr(dbll_state_t *, int);
	int dbll_ptr_to_index(dbll_state_t *, dbll_ptr_t);
#endif
