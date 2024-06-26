#include "debug.h"
#include <dbll.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

// because we are debugging, only use DBLL_ERR for
// "return DBLL_ERR;" because it's not intended for anything otherwise.
// to see if something errored, do "if(function(...) < 0)", as that
// pattern is used in this library
// DBLL_NULL_ERR is like DBLL_ERR except you only use DBLL_NULL_ERR
// for returning a dbll null and DBLL_NULL for everything else, also
// DBLL_NULL_ERR can only be used inside the library
// DBLL_VALID is to print when something is invalid inside of a 
// ..._valid() function
#define DBLL_NULL_ERR DBLL_NULL
#define DBLL_LOG(...)

// wrapped in parenthesis because "||" and "&&" need to have
// explicit order-of-operation. "||" and "&&" are used where
// DBLL_VALID is used, so DBLL_VALID needs to enforce correct order
// of operations
#define DBLL_VALID(...) (__VA_ARGS__)
#ifdef DBLL_DEBUG
	static int err_log(int line) {
		printf("returned error at line %d!\n", line);
		return DBLL_ERR;
	}

	static dbll_ptr_t null_log(int line) {
		printf("returned null at line %d\n", line);
		return DBLL_NULL;
	}

	static int valid_log(
		const char *expr, 
		int is_valid, 
		int line
	) {
		if(!is_valid) {
			printf("%s is not valid on line %d\n", expr, line);
		}

		return is_valid;
	}

	#undef DBLL_NULL_ERR
	#undef DBLL_ERR
	#undef DBLL_LOG
	#undef DBLL_VALID
	#define DBLL_VALID(expr) \
		valid_log(#expr, expr, __LINE__)
	
	#define DBLL_LOG(...) \
		printf("\"" __VA_ARGS__); \
		printf("\" on line %d\n", __LINE__)
	
	#define DBLL_ERR err_log(__LINE__)
	#define DBLL_NULL_ERR null_log(__LINE__)
#endif

static size_t file_size(int desc) {
	struct stat file_stat = { 0 };
	if(fstat(desc, &file_stat) < 0) {
		return DBLL_ERR;
	}

	return file_stat.st_size;
}

int dbll_file_valid(dbll_file_t *file) {
	return (
		DBLL_VALID(file != NULL) &&
		DBLL_VALID(file->mem != NULL) && 
		DBLL_VALID(file->size > 0) &&
		DBLL_VALID(file->desc > 0)
	);
}

int dbll_file_load(dbll_file_t *file, const char *path) {
	if(file == NULL || path == NULL) {
		return DBLL_ERR;
	}

	file->desc = open(path, O_RDWR | O_APPEND);
	if(file->desc < 0) {
		return DBLL_ERR;
	}

	file->size = file_size(file->desc);
	if(file->size < 0) {
		dbll_file_unload(file);
		return DBLL_ERR;
	}

	file->mem = (uint8_t*)(
		mmap(
			NULL,
			file->size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			file->desc,
			0
		)
	);

	if(file->mem == (uint8_t *)(-1)) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_file_unload(dbll_file_t *file) {
	if(file == NULL) {
		return DBLL_ERR;
	}

	if(
		file->mem != NULL &&
		msync(
			file->mem, 
			file->size, 
			MS_SYNC
		) < 0 &&
		
		munmap(file->mem, file->size) < 0
	) {
		return DBLL_ERR;
	}

	if(
		file->desc > 0 &&
		close(file->desc) < 0
	) {
		return DBLL_ERR;
	}

	file->mem = NULL;
	file->size = 0;
	file->desc = 0;
	return DBLL_OK;
}

static const uint8_t file_boilerplate[] = {

	// header
	'd', 'b', 'l', 'l', 
	4, 
	4, 
	0, 0, 0, 0,

	// empty root list
	0, 0, 0, 0, 
	0, 0, 0, 0, 
	0, 0, 0, 0, 
	0, 0, 0, 0
};

int dbll_file_make(dbll_file_t *file, const char *path) {
	if(file == NULL || path == NULL) {
		return DBLL_ERR;
	}
	
	// can't make already existing file
	if(access(path, F_OK) >= 0) {
		return DBLL_ERR;
	}
	
	int desc = open(path, O_RDWR | O_CREAT);
	if(desc < 0) {
		return DBLL_ERR;
	}
	
	if(
		write(
			desc, 
			file_boilerplate, 
			sizeof(file_boilerplate)
		) < 0
	) {

		// don't care for errors on this close
		// because something screwed up anyway
		close(desc);
		return DBLL_ERR;
	}
	
	if(close(desc) < 0) {
		return DBLL_ERR;
	}
	
	if(
		dbll_file_load(
			file,
			path
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_file_resize(dbll_file_t *file, int size) {
	if(
		!dbll_file_valid(file) ||
		file->size + size < 0
	) {
		return DBLL_ERR;
	}

	msync(file->mem, file->size, MS_SYNC);
	munmap(file->mem, file->size);
	if(
		ftruncate(
			file->desc, 
			file->size + size
		) < 0
	) {
		return DBLL_ERR;
	}
	
	file->size += size;
	file->mem = (uint8_t*)(
		mmap(
			NULL,
			file->size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			file->desc,
			0
		)
	);

	if(file->mem == (uint8_t *)(-1)) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

// the magic number spells out "dbll" but in decimal form
static const uint32_t dbll_header_magic = 1819042404;
int dbll_header_valid(dbll_header_t *header) {
	uint32_t magic_int = *((uint32_t *)(header->magic));
	return (
		DBLL_VALID(header != NULL) &&
		DBLL_VALID(magic_int == dbll_header_magic) && DBLL_VALID(
			header->ptr_size == 1 ||
			header->ptr_size == 2 ||
			header->ptr_size == 4 ||
			header->ptr_size == 8
		) &&

		DBLL_VALID(header->data_size > 0) &&
		DBLL_VALID(header->data_size <= DBLL_SIZE_MAX) &&
		DBLL_VALID(header->list_size > 0) &&
		DBLL_VALID(header->header_size > 0)
	);
}

int dbll_header_load(dbll_header_t *header, dbll_file_t *file) {
	if(!dbll_file_valid(file) || header == NULL) {
		return DBLL_ERR;
	}

	memcpy(header->magic, &file->mem[0], DBLL_MAGIC_SIZE);
	header->ptr_size = file->mem[DBLL_MAGIC_SIZE];
	header->data_size = file->mem[DBLL_MAGIC_SIZE + 1];

	// done manually and not with memcpy in order to enforce endianness
	int index = DBLL_MAGIC_SIZE + 2 + header->ptr_size;
	header->empty_slot_ptr = 0;
	for(int i = 0; i < header->ptr_size && index >= 0; i++) {
		header->empty_slot_ptr |= (
			(file->mem[index] << (i * 8)) &
			(0xff << (i * 8))
		);

		index--;
	}

	// 3 because that's how many pointers are in dbll_list_t
	// and empty_slot_size
	header->header_size = DBLL_MAGIC_SIZE + 2 + header->ptr_size;
	header->list_size = (header->ptr_size * 3) + header->data_size;
	header->empty_slot_size = (header->ptr_size * 3) + 1;
	header->data_slot_size = header->list_size - header->ptr_size;
	return DBLL_OK;
}

int dbll_header_unload(dbll_header_t *header) {
	if(!dbll_header_valid(header)) {
		return DBLL_ERR;
	}

	memset(header->magic, 0, DBLL_MAGIC_SIZE);
	header->ptr_size = 0;
	header->data_size = 0;
	header->empty_slot_ptr = 0;
	header->list_size = 0;
	header->header_size = 0;
	return DBLL_OK;
}

int dbll_header_write(
	dbll_header_t *header, 
	dbll_state_t *state
) {
	if(
		!dbll_header_valid(header) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	// calculated in base one, but we need to go over
	// one anyway so we get that addition for free.
	// two for the two sizes in the head, one
	// size for pointers, the other to describe the
	// size of data sizes
	int index = DBLL_MAGIC_SIZE + 2;
	if(
		dbll_ptr_index_copy(
			state,
			header->empty_slot_ptr,
			index
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_list_valid(dbll_list_t *list) {
	return (
		DBLL_VALID(list != NULL) &&
		DBLL_VALID(list->data_size >= 0) &&
		DBLL_VALID(
			(
				list->head_ptr != list->this_ptr &&
				list->tail_ptr != list->this_ptr &&
				list->data_ptr != list->this_ptr
			) || list->this_ptr == DBLL_NULL
		)
	);
}

int dbll_list_load(
	dbll_list_t *list, 
	dbll_state_t *state, 
	dbll_ptr_t ptr
) {
	if(!dbll_state_valid(state) || !dbll_list_valid(list)) {
		return DBLL_ERR;
	}

	int index = dbll_ptr_to_index(state, ptr);
	if(index == -1) {
		return DBLL_ERR;
	}
	
	int ptr_size = state->header.ptr_size;
	if(
		dbll_index_ptr_copy(
			state, 
			index,
			&list->head_ptr
		) < 0 ||
		
		dbll_index_ptr_copy(
			state, 
			index + ptr_size,
			&list->tail_ptr
		) < 0 ||
		
		dbll_index_ptr_copy(
			state, 
			index + (ptr_size * 2),
			&list->data_ptr
		) < 0 ||

		dbll_index_size_copy(
			state,
			index + (ptr_size * 3),
			&list->data_size
		) < 0
	) {
		dbll_list_unload(list);
		return DBLL_ERR;
	}

	list->this_ptr = ptr;
	return DBLL_OK;
}

int dbll_list_unload(dbll_list_t *list) {
	if(list == NULL) {
		return DBLL_ERR;
	}
	
	list->head_ptr = DBLL_NULL;
	list->tail_ptr = DBLL_NULL;
	list->data_ptr = DBLL_NULL;
	list->data_size = 0;
	return DBLL_OK;
}

int dbll_list_go(
	dbll_list_t *list, 
	dbll_state_t *state, 
	list_go_e go
) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}
	
	dbll_ptr_t go_ptr = 0;
	switch(go) {
		case DBLL_GO_HEAD: {
			go_ptr = list->head_ptr;
			break;
		}
		
		case DBLL_GO_TAIL: {
			go_ptr = list->tail_ptr;
			break;
		}
		
		default: {
			return DBLL_ERR;
		}
	}
	
	if(
		dbll_list_load(
			list,
			state,
			go_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_list_data_index(
	dbll_list_t *list, 
	dbll_state_t *state
) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state)
	) {
		return -1;
	}
	
	return dbll_ptr_to_index(state, list->data_ptr);
}

int dbll_list_data_alloc(
	dbll_list_t *list,
	dbll_state_t *state,
	int size
) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state) ||
		list->data_ptr != DBLL_NULL ||
		size < 0
	) {
		return DBLL_ERR;
	}

	dbll_data_slot_t slot = { 0 };
	dbll_ptr_t tail_ptr = dbll_state_alloc(state);
	if(tail_ptr == DBLL_NULL) {
		return DBLL_ERR;
	}

	if(
		dbll_data_slot_load(
			&slot,
			state,
			tail_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	if(dbll_data_slot_alloc(&slot, state, size) < 0) {
		return DBLL_ERR;
	}

	list->data_ptr = slot.this_ptr;
	list->data_size = size;
	if(
		dbll_list_write(
			list,
			state
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_list_data_resize(
	dbll_list_t *list, 
	dbll_state_t *state, 
	int size
) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	if(list->data_ptr == DBLL_NULL) {
		if(size < 0) {
			return DBLL_ERR;
		}

		if(size == 0) {
			return DBLL_OK;
		}
		
		if(
			dbll_list_data_alloc(
				list,
				state,
				size
			) < 0
		) {
			return DBLL_ERR;
		}

		return DBLL_OK;
	}

	// need first slot to feed in dbll_data_slot_last in order
	// to get last slot
	dbll_data_slot_t slot = { 0 };
	if(
		dbll_data_slot_load(
			&slot, 
			state, 
			list->data_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	if(
		dbll_data_slot_resize(
			&slot,
			state,
			size
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_list_write(
	dbll_list_t *list,
	dbll_state_t *state
) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	int index = dbll_ptr_to_index(state, list->this_ptr);
	int ptr_size = state->header.ptr_size;
	if(
		index == -1 ||
		ptr_size <= 0 ||
		dbll_ptr_index_copy(
			state,
			list->head_ptr,
			index
		) < 0 ||

		dbll_ptr_index_copy(
			state,
			list->tail_ptr,
			index + ptr_size
		) < 0 ||

		dbll_ptr_index_copy(
			state,
			list->data_ptr,
			index + (ptr_size * 2)
		) < 0 ||

		dbll_size_index_copy(
			state,
			list->data_size,
			index + (ptr_size * 3)
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_empty_slot_valid(dbll_empty_slot_t *empty_slot) {
	return (
		DBLL_VALID(empty_slot != NULL) && (
			DBLL_VALID(
				(
					empty_slot->prev_ptr != empty_slot->this_ptr &&
					empty_slot->next_ptr != empty_slot->this_ptr
				) || empty_slot->this_ptr == DBLL_NULL
			)
		)
	);
}

int dbll_empty_slot_valid_ptr(
	dbll_state_t *state,
	dbll_ptr_t ptr
) {
	if(!dbll_state_valid(state)) {

		// return 0 and not DBLL_ERR since an empty slot can't
		// exist if a state for it doesn't exist either
		return 0;
	}

	int index = dbll_ptr_to_index(state, ptr);
	if(index == -1) {

		// see comment above
		return 0;
	}

	dbll_ptr_t maybe_this_ptr = DBLL_NULL;
	if(
		dbll_index_ptr_copy(
			state,
			index,
			&maybe_this_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	// the reason that this works is my intentionally designing
	// constraints around referencing one's self in data, nothing
	// can do this except an empty slot. this is done so that dbll_state_trim
	// doesn't have to run through the entire empty slot linked list in order
	// to find what it wants. basically an optimization technique
	return maybe_this_ptr == ptr;
}

int dbll_empty_slot_load(
	dbll_empty_slot_t *empty_slot,
	dbll_state_t *state,
	dbll_ptr_t list_ptr
) {
	if(
		!dbll_state_valid(state) || 
		empty_slot == NULL ||
		list_ptr == DBLL_NULL
	) {
		return DBLL_ERR;
	}

	int index = dbll_ptr_to_index(state, list_ptr);
	int ptr_size = state->header.ptr_size;
	if(
		index == -1 ||
		ptr_size <= 0 ||
		dbll_index_ptr_copy(
			state, 
			index,
			&empty_slot->prev_ptr
		) < 0 ||

		dbll_index_ptr_copy(
			state, 
			index + ptr_size,
			&empty_slot->next_ptr
		) < 0 ||

		dbll_index_ptr_copy(
			state, 
			index + (ptr_size * 2),
			&empty_slot->this_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_empty_slot_unload(dbll_empty_slot_t *slot) {
	if(slot == NULL) {
		return DBLL_ERR;
	}

	slot->this_ptr = DBLL_NULL;
	slot->prev_ptr = DBLL_NULL;
	slot->next_ptr = DBLL_NULL;
	return DBLL_OK;
}

int dbll_empty_slot_write(
	dbll_empty_slot_t *slot, 
	dbll_state_t *state
) {
	if(
		!dbll_empty_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	int index = dbll_ptr_to_index(state, slot->this_ptr);
	int ptr_size = state->header.ptr_size;
	if(
		index == -1 ||
		ptr_size <= 0 ||
		dbll_ptr_index_copy(
			state,
			slot->this_ptr,
			index
		) < 0 ||

		dbll_ptr_index_copy(
			state,
			slot->prev_ptr,
			index + ptr_size
		) < 0 ||

		dbll_ptr_index_copy(
			state,
			slot->next_ptr,
			index + (ptr_size * 2)
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_empty_slot_clip(
	dbll_empty_slot_t *slot,
	dbll_state_t *state
) {
	if(
		!dbll_empty_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	if(
		slot->prev_ptr == DBLL_NULL &&
		slot->next_ptr == DBLL_NULL &&
		state->last_empty.this_ptr == slot->this_ptr &&
		dbll_empty_slot_unload(&state->last_empty) < 0
	) {
		return DBLL_ERR;
	}

	if(
		slot->prev_ptr != DBLL_NULL &&
		slot->next_ptr == DBLL_NULL &&
		state->last_empty.this_ptr == slot->this_ptr &&
		dbll_empty_slot_load(
			&state->last_empty,
			state,
			slot->prev_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	if(slot->prev_ptr != DBLL_NULL) {
		dbll_empty_slot_t prev_slot = { 0 };
		if(
			dbll_empty_slot_load(
				&prev_slot, 
				state,
				slot->prev_ptr
			) < 0
		) {
			return DBLL_ERR;
		}

		prev_slot.next_ptr = slot->next_ptr;
		dbll_empty_slot_write(&prev_slot, state);
	}

	if(slot->next_ptr != DBLL_NULL) {
		dbll_empty_slot_t next_slot = { 0 };
		if(
			dbll_empty_slot_load(
				&next_slot,
				state,
				slot->next_ptr
			) < 0
		) {
			return DBLL_ERR;
		}

		next_slot.prev_ptr = slot->prev_ptr;
		dbll_empty_slot_write(&next_slot, state);
	}

	if(dbll_empty_slot_unload(slot) < 0) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_data_slot_valid(dbll_data_slot_t *slot) {
	return (
		DBLL_VALID(slot != NULL) &&
		DBLL_VALID(slot->data_index >= 0) &&
		DBLL_VALID(slot->this_ptr != DBLL_NULL) && DBLL_VALID(
			slot->next_ptr != slot->this_ptr || (
				slot->next_ptr == DBLL_NULL &&
				slot->this_ptr == DBLL_NULL
			)
		)
	);
}

int dbll_data_slot_load(
	dbll_data_slot_t *slot, 
	dbll_state_t *state, 
	dbll_ptr_t ptr
) {
	if(!dbll_state_valid(state) || slot == NULL) {
		return DBLL_ERR;
	}

	int index = dbll_ptr_to_index(state, ptr);
	int ptr_size = state->header.ptr_size;
	if(
		index == -1 ||
		ptr_size <= 0 ||
		dbll_index_ptr_copy(
			state, 
			index, 
			&slot->next_ptr
		) < 0
	) {
		return DBLL_ERR;
	}
	
	slot->data_index = index + state->header.ptr_size;
	slot->this_ptr = ptr;
	return DBLL_OK;
}

int dbll_data_slot_unload(dbll_data_slot_t *slot) {
	if(slot == NULL) {
		return DBLL_ERR;
	}

	slot->next_ptr = DBLL_NULL;
	slot->data_index = 0;
	slot->is_marked = 0;
	slot->this_ptr = DBLL_NULL;
	return DBLL_OK;
}

int dbll_data_slot_next(
	dbll_data_slot_t *slot, 
	dbll_state_t *state
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state) ||
		slot->next_ptr == DBLL_NULL
	) {
		return DBLL_ERR;
	}

	if(
		dbll_data_slot_load(
			slot,
			state,
			slot->next_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_data_slot_free(
	dbll_data_slot_t *slot,
	dbll_state_t *state
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	slot->is_marked = 1;
		if(slot->next_ptr != DBLL_NULL && !slot->is_marked) {
			dbll_data_slot_t next_slot = *slot;
			if(dbll_data_slot_next(&next_slot, state) < 0) {
				return DBLL_ERR;
			}
			
			if(dbll_data_slot_free(&next_slot, state) < 0) {
				return DBLL_ERR;
			}
		}

		if(slot->is_marked) {
			return DBLL_OK;
		}
	slot->is_marked = 0;

	dbll_state_mark_free(state, slot->this_ptr);
	dbll_data_slot_unload(slot);
	return DBLL_OK;
}

// doesn't prevent cyclic data slot access
// in order to make circular memory possible
int dbll_data_slot_page(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int user_index
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return -1;
	}
	
	int page_number = user_index / state->header.data_slot_size;
	int page_offset = user_index % state->header.data_slot_size;
	dbll_data_slot_t fetch_slot = *slot;
	for(
		int i = 0; 
		i < page_number && 
		fetch_slot.next_ptr != DBLL_NULL; 
		i++
	) {
		if(dbll_data_slot_next(&fetch_slot, state) < 0) {

			// dont unload it because we would leak
			// memory in the database otherwise, 
			// rather, get every element one by one
			// and clean them up
			dbll_data_slot_free(slot, state);
			return -1;
		}
	}

	return fetch_slot.data_index + page_offset;
}

int dbll_data_slot_resize(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int size
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	if(size == 0) {

		// nothing to allocate but shouldn't error
		return DBLL_OK;
	}

	if(size < 0) {
		if(
			dbll_data_slot_cut_end(
				slot,
				state,
				-size
			) < 0
		) {
			return DBLL_ERR;
		}

		return DBLL_OK;
	}

	dbll_ptr_t last_ptr = dbll_data_slot_last(
		slot,
		state,
		NULL
	);

	if(last_ptr == DBLL_NULL) {
		return DBLL_ERR;
	}

	dbll_data_slot_t last_slot = { 0 };
	if(
		dbll_data_slot_load(
			&last_slot,
			state,
			last_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	if(
		dbll_data_slot_alloc(
			&last_slot,
			state,
			size
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_ERR;
}

int dbll_data_slot_alloc(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int size
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state) ||
		size < 0
	) {
		return DBLL_ERR;
	}

	dbll_data_slot_t *prev_slot = NULL;
	dbll_data_slot_t temp_slot = { 0 };
	dbll_data_slot_t prev_temp_slot = { 0 };
	for(int i = 0; i < size; i++) {
		dbll_ptr_t new_slot_ptr = dbll_state_alloc(state);
		if(new_slot_ptr == DBLL_NULL) {
			return DBLL_ERR;
		}

		dbll_data_slot_t *current_slot = i == 0
			? slot
			: &temp_slot;

		if(
			dbll_data_slot_load(
				current_slot, 
				state, 
				new_slot_ptr
			) < 0
		) {
			return DBLL_ERR;
		}

		current_slot->next_ptr = DBLL_NULL;
		if(prev_slot != NULL) {
			prev_slot->next_ptr = current_slot->this_ptr;
			if(
				dbll_data_slot_write(
					prev_slot,
					state
				) < 0
			) {
				return DBLL_ERR;
			}
		}

		if(i != 0) {
			memcpy(
				&prev_temp_slot,
				current_slot,
				sizeof(dbll_data_slot_t)
			);
		}

		prev_slot = i == 0
			? slot
			: &prev_temp_slot;
	}
	
	return DBLL_OK;
}

int dbll_data_slot_write(
	dbll_data_slot_t *slot,
	dbll_state_t *state
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	int index = dbll_ptr_to_index(
		state,
		slot->this_ptr
	);

	if(index == -1) {
		return DBLL_ERR;
	}

	if(
		dbll_ptr_index_copy(
			state,
			slot->next_ptr,
			index
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

// recurse in here to get size, that way when the
// recursion is unwinded we can see when to cut
// with the size pointer, but the user shouldn't
// have to do that, so it's done in dbll_data_slot_cut_end
// instead 
static int data_slot_cut_end_inner(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int *size,
	int cut_size
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_ERR;
	}

	dbll_data_slot_t current_slot = *slot;
	int current_size = *size;
	slot->is_marked = 1;
		if(slot->next_ptr != DBLL_NULL && !slot->is_marked) {
			dbll_data_slot_t next_slot = *slot;
			if(dbll_data_slot_next(&next_slot, state) < 0) {
				return DBLL_ERR;
			}

			(*size)++;
			if(
				data_slot_cut_end_inner(
					&next_slot, 
					state, 
					size, 
					cut_size
				) < 0
			) {
				return DBLL_ERR;
			}
		}

		if(slot->is_marked) {
			return DBLL_OK;
		}
	slot->is_marked = 0;

	// sets the next_ptr to null so that dbll_data_slot_free
	// doesn't free the whole thing due to how dbll_data_slot_last works
	if(current_size == *size) {
		slot->next_ptr = DBLL_NULL;
		if(
			dbll_data_slot_write(
				slot,
				state
			) < 0
		) {
			return DBLL_ERR;
		}

		return DBLL_OK;
	}
	
	if(current_size == (*size) - cut_size) {
		if(
			dbll_data_slot_free(
				&current_slot,
				state
			) < 0
		) {
			return DBLL_ERR;
		}
	}

	return DBLL_OK;
}

int dbll_data_slot_cut_end(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int size
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state) ||
		size < 0
	) {
		return DBLL_ERR;
	}

	if(size == 0) {
		return DBLL_OK;
	}
	
	int total_size = 0;
	return data_slot_cut_end_inner(
		slot,
		state,
		&total_size,
		size
	);
}

dbll_ptr_t dbll_data_slot_last(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int *size
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state)
	) {
		return DBLL_NULL_ERR;
	}

	slot->is_marked = 1;
		if(slot->next_ptr != DBLL_NULL && !slot->is_marked) {
			dbll_data_slot_t next_slot = *slot;
			if(dbll_data_slot_next(&next_slot, state) < 0) {
				return DBLL_NULL_ERR;
			}

			if(size != NULL) {
				(*size)++;
			}

			if(dbll_data_slot_last(&next_slot, state, size) == DBLL_NULL) {
				return DBLL_NULL_ERR;
			}
		}

		if(slot->is_marked) {
			return DBLL_OK;
		}
	slot->is_marked = 0;
	return slot->this_ptr;
}

static int data_slot_write_read(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int offset,
	uint8_t *mem,
	int mem_size,
	int is_write
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state) ||
		offset < 0 ||
		mem == NULL ||
		mem_size < 0
	) {
		return DBLL_ERR;
	}

	int page_size = state->header.data_slot_size;
	dbll_data_slot_t temp_slot = { 0 };
	temp_slot = *slot;
	while(offset > page_size) {
		if(
			dbll_data_slot_load(
				&temp_slot,
				state,
				temp_slot.next_ptr
			) < 0
		) {
			return DBLL_ERR;
		}

		offset -= page_size;
	}

	int mem_index = 0;
	int write_index = offset;
	while(mem_size >= 0) {
		if(write_index >= page_size) {
			write_index = 0;
			if(
				dbll_data_slot_load(
					&temp_slot,
					state,
					temp_slot.next_ptr
				) < 0
			) {
				return DBLL_ERR;
			}
		}

		if(is_write) {
			state->file.mem[
				write_index + temp_slot.data_index
			] = mem[mem_index];
		} else {
			mem[mem_index] = state->file.mem[
				write_index + temp_slot.data_index
			];
		}

		write_index++;
		mem_index++;
		mem_size--;
	}

	return DBLL_OK;
}

int dbll_data_slot_write_mem(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int offset,
	uint8_t *mem,
	int mem_size
) {
	if(
		data_slot_write_read(
			slot,
			state,
			offset,
			mem,
			mem_size,
			1
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_data_slot_read_mem(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	int offset,
	uint8_t *mem,
	int mem_size
) {
	if(
		data_slot_write_read(
			slot,
			state,
			offset,
			mem,
			mem_size,
			0
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_state_valid(dbll_state_t *state) {
	return (
		DBLL_VALID(state != NULL) &&
		DBLL_VALID(dbll_file_valid(&state->file)) &&
		DBLL_VALID(dbll_header_valid(&state->header)) &&
		DBLL_VALID(dbll_empty_slot_valid(&state->last_empty)) &&
		DBLL_VALID(dbll_list_valid(&state->root_list))
	);
	
	// gcc gives incorrect warning
	return 1;
}

int dbll_state_load(dbll_state_t *state, const char *path) {
	if(state == NULL || path == NULL) {
		return DBLL_ERR;
	}

	state->last_empty = (dbll_empty_slot_t) { 0 };
	state->root_list = (dbll_list_t) { 0 };
	if(
		dbll_file_load(&state->file, path) < 0 ||
		dbll_header_load(&state->header, &state->file) < 0 ||
		dbll_list_load(&state->root_list, state, 1) < 0
	) {
		dbll_state_unload(state);
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_state_unload(dbll_state_t *state) {
	if(state == NULL) {
		return DBLL_ERR;
	}

	dbll_file_unload(&state->file);
	dbll_header_unload(&state->header);
	dbll_empty_slot_unload(&state->last_empty);
	dbll_list_unload(&state->root_list);
	return DBLL_OK;
}

int dbll_state_make(dbll_state_t *state, const char *path) {
	if(state == NULL || path == NULL) {
		return DBLL_ERR;
	}

	dbll_file_t temp_file = { 0 };
	if(dbll_file_make(&temp_file, path) < 0) {
		return DBLL_ERR;
	}

	if(dbll_state_load(state, path) < 0) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_state_make_replace(
	dbll_state_t *state,
	const char *path
) {
	if(
		access(path, F_OK) >= 0 &&
		unlink(path) < 0
	) {
		return DBLL_ERR;
	}

	if(dbll_state_make(state, path) < 0) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

dbll_ptr_t dbll_state_empty_find(dbll_state_t *state) {
	if(
		!dbll_state_valid(state) ||

		// this is an error since last empty should
		// always point to the last empty slot in the
		// empty slot linked list
		state->last_empty.next_ptr != DBLL_NULL
	) {
		return DBLL_NULL_ERR;
	}

	if(
		state->last_empty.this_ptr == DBLL_NULL
	) {
		return DBLL_NULL;
	}

	dbll_ptr_t new_empty = state->last_empty.prev_ptr;
	if(new_empty == DBLL_NULL) {
		dbll_empty_slot_unload(&state->last_empty);
		return new_empty;
	}

	dbll_ptr_t current = state->last_empty.this_ptr;
	if(
		dbll_empty_slot_load(
			&state->last_empty,
			state,
			new_empty
		) < 0
	) {
		return DBLL_NULL_ERR;
	}

	state->last_empty.next_ptr = DBLL_NULL;
	if(
		dbll_empty_slot_write(
			&state->last_empty, 
			state
		) < 0
	) {
		return DBLL_NULL_ERR;
	}

	return current;
}

dbll_ptr_t dbll_state_alloc(dbll_state_t *state) {
	if(!dbll_state_valid(state)) {
		return DBLL_NULL_ERR;
	}

	dbll_ptr_t empty_slot = dbll_state_empty_find(state);
	if(empty_slot != DBLL_NULL) {
		return empty_slot;
	}

	if(
		dbll_file_resize(
			&state->file, 
			state->header.list_size
		) < 0
	) {
		return DBLL_NULL_ERR;
	}

	int total_size = 0;
	if(
		dbll_state_total_size(
			state,
			&total_size
		) < 0
	) {
		return DBLL_NULL_ERR;
	}

	return (dbll_ptr_t)(total_size);
}

int dbll_state_mark_free(dbll_state_t *state, dbll_ptr_t ptr) {
	if(!dbll_state_valid(state)) {
		return DBLL_ERR;
	}

	dbll_empty_slot_t slot = { 0 };
	if(dbll_empty_slot_load(&slot, state, ptr) < 0) {
		return DBLL_ERR;
	}
	
	slot.prev_ptr = DBLL_NULL;
	if(state->last_empty.this_ptr != DBLL_NULL) {
		dbll_empty_slot_t *prev_slot = &state->last_empty;
		slot.prev_ptr = prev_slot->this_ptr;
		prev_slot->next_ptr = ptr;
		if(dbll_empty_slot_write(prev_slot, state) < 0) {
			return DBLL_ERR;
		}
	}

	slot.next_ptr = DBLL_NULL;
	slot.this_ptr = ptr;
	if(dbll_empty_slot_write(&slot, state) < 0) {
		return DBLL_ERR;
	}

	state->last_empty = slot;
	state->header.empty_slot_ptr = ptr;
	if(
		dbll_header_write(
			&state->header,
			state
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_state_total_size(
	dbll_state_t *state,
	int *size
) {
	if(
		!dbll_state_valid(state) ||
		size == NULL
	) {
		return DBLL_ERR;
	}

	*size = (
		state->file.size -
		state->header.header_size
	) / state->header.list_size;

	return DBLL_OK;
}

int dbll_state_trim(dbll_state_t *state) {
	if(!dbll_state_valid(state)) {
		return DBLL_ERR;
	}

	int current_ptr = 0;
	if(
		dbll_state_total_size(
			state,
			&current_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	int trim_size = 0;
	dbll_empty_slot_t slot = { 0 };

	// total pointer/block size of file is computed in one-based indices as all
	// sizes are, but not zero, this operation converts it to
	// a zero-based index
	current_ptr--;
	while(
		current_ptr >= 0 &&
		dbll_empty_slot_valid_ptr(
			state,
			current_ptr
		)
	) {
		if(
			dbll_empty_slot_load(
				&slot,
				state,
				current_ptr
			) < 0 ||

			dbll_empty_slot_clip(
				&slot,
				state
			) < 0
		) {
			return DBLL_ERR;
		}

		trim_size++;
		current_ptr--;
	}

	if(trim_size > 0) {
		if(
			dbll_file_resize(
				&state->file,
				-(trim_size * state->header.list_size)
			) < 0
		) {
			return DBLL_ERR;
		}

		return DBLL_OK;
	}

	return DBLL_OK;
}

int dbll_state_compact(dbll_state_t *state) {
	if(!dbll_state_valid(state)) {
		return DBLL_ERR;
	}

	int total_size = 0;
	if(
		dbll_state_total_size(
			state,
			&total_size
		) < 0
	) {
		return DBLL_ERR;
	}

	int decrease_size = 0;
	dbll_ptr_t current_empty_ptr = state->last_empty.this_ptr;
	while(
		current_empty_ptr != DBLL_NULL &&
		dbll_empty_slot_valid_ptr(
			state,
			current_empty_ptr
		)
	) {
		dbll_empty_slot_t slot = { 0 };
		if(
			dbll_empty_slot_load(
				&slot,
				state,
				current_empty_ptr
			) < 0
		) {
			return DBLL_ERR;
		}

		for(
			int i = current_empty_ptr + 1;
			i < total_size;
			i++
		) {
			int past_index = dbll_ptr_to_index(state, i - 1);
			int current_index = dbll_ptr_to_index(state, i);
			if(
				past_index == -1 ||
				current_index == -1
			) {
				return DBLL_ERR;
			}

			memcpy(
				&state->file.mem[past_index],
				&state->file.mem[current_index],
				state->header.list_size
			);
		}

		total_size--;
		decrease_size++;
		current_empty_ptr = slot.next_ptr;
	}

	if(
		dbll_file_resize(
			&state->file,
			-(
				decrease_size *
				state->header.list_size
			)
		) < 0
	) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_index_ptr_copy(
	dbll_state_t *state, 
	int index,
	dbll_ptr_t *ptr
) {
	if(
		!dbll_state_valid(state) ||
		index < 0 ||
		index >= state->file.size || 
		ptr == NULL
	) {
		return DBLL_ERR;
	}

	int ptr_size = state->header.ptr_size;
	*ptr = 0;
	index += ptr_size - 1;

	// done manually and not with memcpy in order to enforce endianness
	for(int i = 0; i < ptr_size && index >= 0; i++) {
		*ptr |= (
			(state->file.mem[index] << (i * 8)) &
			(0xff << (i * 8))
		);

		index--;
	}

	return DBLL_OK;
}

int dbll_index_size_copy(
	dbll_state_t *state,
	int index,
	dbll_size_t *size
) {
	if(
		!dbll_state_valid(state) ||
		index < 0 ||
		index >= state->file.size ||
		size == NULL
	) {
		return DBLL_ERR;
	}

	int data_size = state->header.data_size;
	*size = 0;
	index += data_size - 1;

	// done manually and not with memcpy in order to enforce endianness
	for(int i = 0; i < data_size && index >= 0; i++) {
		*size |= (
			(state->file.mem[index] << (i * 8)) &
			(0xff << (i * 8))
		);

		index--;
	}

	return DBLL_OK;
}

int dbll_ptr_index_copy(
	dbll_state_t *state,
	dbll_ptr_t ptr,
	int index
) {
	if(
		!dbll_state_valid(state) ||
		index < 0 ||
		index >= state->file.size
	) {
		return DBLL_ERR;
	}

	int ptr_size = state->header.ptr_size;
	index += ptr_size - 1;

	// done manually and not with memcpy in order to enforce endianness
	for(int i = 0; i < ptr_size && index >= 0; i++) {
		state->file.mem[index] = (ptr >> (i * 8)) & 0xff;
		index--;
	}
	
	return DBLL_OK;
}

int dbll_size_index_copy(
	dbll_state_t *state,
	dbll_size_t size,
	int index
) {
	if(
		!dbll_state_valid(state) ||
		index < 0 ||
		index >= state->file.size
	) {
		return DBLL_ERR;
	}

	int data_size = state->header.data_size;
	index += data_size - 1;

	// done manually and not with memcpy in order to enforce endianness
	for(int i = 0; i < data_size && index >= 0; i++) {
		state->file.mem[index] = (size >> (i * 8)) & 0xff;
		index--;
	}

	return DBLL_OK;
}

dbll_ptr_t dbll_index_to_ptr(dbll_state_t *state, int index) {
	if(
		!dbll_state_valid(state) ||
		index < 0 || 
		index >= state->file.size
	) {
		return DBLL_NULL_ERR;
	}

	// convert to one-based indices because 0 is reserved
	// and that dbll_ptr_t a one-based index system, so
	// adjust accordingly
	index++;
	dbll_ptr_t result = (dbll_ptr_t)(index);
	result -= state->header.header_size;
	if(result < 0) {
		return DBLL_NULL_ERR;
	}

	result /= state->header.list_size;
	return result;
}

int dbll_ptr_to_index(dbll_state_t *state, dbll_ptr_t ptr) {
	if(
		!dbll_state_valid(state) ||
		ptr == DBLL_NULL
	) {
		return -1;
	}

	// because 0 is reserved, indices are one-based
	// so ptr is subtracted to convert it back to
	// zero-base in order for conversion to happen
	ptr--;
	int offset = state->header.header_size;
	int index = offset + (ptr * state->header.list_size);
	if(index < 0 || index >= state->file.size) {
		return -1;
	}

	return index;
}
