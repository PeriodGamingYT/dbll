#ifndef DBLL_H
#define DBLL_H
	#include <stdint.h>
	
	// DBLL_OK and DBLL_ERR are used in functions that return an int
	// for error handling, unless it's a function that uses int to return a bool
	#define DBLL_OK 0
	#define DBLL_ERR 1
	#define DBLL_MAGIC_SIZE 4
	#define DBLL_PTR_MAX 8
	#define DBLL_SIZE_MAX 4
	typedef uint64_t dbll_ptr_t;
	typedef uint32_t dbll_size_t;
	typedef struct {
		uint8_t *mem;
		size_t size;
		size_t cursor;
		int desc;
	} dbll_file_t;

	int dbll_file_valid(dbll_file_t *);
	int dbll_file_load(dbll_file_t *, const char *);
	int dbll_file_unload(dbll_file_t **);
	typedef struct {
		char magic[DBLL_MAGIC_SIZE];
		uint8_t ptr_size;
		uint8_t data_size;
		dbll_ptr_t empty_slot_ptr;

		// not in file, computed in dbll_header_load
		int list_size;
		int header_size;
	} dbll_header_t;

	int dbll_header_valid(dbll_header_t *);
	int dbll_header_load(dbll_header_t *, dbll_file_t *);
	typedef struct {
		dbll_ptr_t head_ptr; // to dbll_list_t
		dbll_ptr_t tail_ptr; // to dbll_list_t
		dbll_ptr_t data_ptr; // to uint8_t* (size is multiple of list structure size)
		dbll_size_t data_size;
	} dbll_list_t;

	typedef struct {
		dbll_ptr_t prev_ptr; // to dbll_empty_slot_t
		dbll_ptr_t next_ptr; // to dbll_empty_slot_t
		dbll_ptr_t empty_ptr; // to dbll_list_t
		uint8_t is_next_empty;
	} dbll_empty_slot_t;

	typedef struct {
		dbll_file_t file;
		dbll_header_t header;
		dbll_empty_slot_t last_empty;
		dbll_list_t root_list;
	} dbll_state_t;
	int dbll_state_valid(dbll_state_t *);
	int dbll_mem_to_ptr(dbll_state_t *, uint8_t *, dbll_ptr_t);
	uint8_t *dbll_ptr_to_mem(dbll_state_t *, dbll_ptr_t);
	int dbll_list_load(dbll_list_t *, dbll_state_t *, dbll_ptr_t);
	int dbll_empty_slot_load(dbll_empty_slot_t *, dbll_state_t *, dbll_ptr_t);
	int dbll_state_load(dbll_state_t *, const char *);
#endif