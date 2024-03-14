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
#define DBLL_NULL_ERR DBLL_NULL
#define DBLL_LOG(...)
#ifdef DBLL_DEBUG
	static int err_log(int line) {
		printf("returned error at line %d!\n", line);
		return DBLL_ERR;
	}

	static dbll_ptr_t null_log(int line) {
		printf("returned null at line %d\n", line);
		return DBLL_NULL;
	}

	#undef DBLL_NULL_ERR
	#undef DBLL_ERR
	#undef DBLL_LOG
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
		file != NULL &&
		file->mem != NULL && 
		file->size > 0 &&
		file->desc > 0
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
	'd', 'b', 'l', 'l', 4, 4, 0, 0, 0, 1
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
	
	return dbll_file_load(file, path);
}

int dbll_file_change(dbll_file_t *file, size_t size) {
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
		header != NULL &&
		magic_int == dbll_header_magic && (
			header->ptr_size == 1 ||
			header->ptr_size == 2 ||
			header->ptr_size == 4 ||
			header->ptr_size == 8
		) &&

		header->data_size > 0 &&
		header->data_size <= DBLL_SIZE_MAX &&
		header->list_size > 0 &&
		header->header_size > 0
	);
}

int dbll_header_load(dbll_header_t *header, dbll_file_t *file) {
	if(!dbll_file_valid(file) || header == NULL) {
		return DBLL_ERR;
	}

	memcpy(header->magic, &file->mem[0], DBLL_MAGIC_SIZE);
	header->ptr_size = file->mem[DBLL_MAGIC_SIZE];
	header->data_size = file->mem[DBLL_MAGIC_SIZE + 1];
	memcpy(
		&header->empty_slot_ptr, 
		&file->mem[DBLL_MAGIC_SIZE + 2],
		header->ptr_size
	);

	// 3 because that's how many pointers are in dbll_list_t
	// and empty_slot_size
	header->header_size = DBLL_MAGIC_SIZE + 1 + header->ptr_size;
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

	
	return DBLL_OK;
}

int dbll_list_valid(dbll_list_t *list) {
	return (
		list != NULL &&
		list->data_size >= 0
	);
}

int dbll_list_load(
	dbll_list_t *list, 
	dbll_state_t *state, 
	dbll_ptr_t list_ptr
) {
	if(!dbll_state_valid(state) || !dbll_list_valid(list)) {
		return DBLL_ERR;
	}

	int list_file_index = dbll_ptr_to_index(state, list_ptr);
	if(list_file_index < 0) {
		return DBLL_ERR;
	}
	
	int ptr_size = state->header.ptr_size;
	if(
		dbll_ptr_index_copy(
			state, 
			list->head_ptr,
			list_file_index 
		) < 0 ||
		
		dbll_ptr_index_copy(
			state, 
			list->tail_ptr,
			list_file_index + ptr_size
		) < 0 ||
		
		dbll_ptr_index_copy(
			state, 
			list->data_ptr,
			list_file_index + (ptr_size * 2)
		) < 0
	) {
		dbll_list_unload(list);
		return DBLL_ERR;
	}

	memcpy(
		(char *)(&list->data_size),
		&state->file.mem[list_file_index + (ptr_size * 3)],
		state->header.data_size
	);
	
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
	
	return dbll_list_load(list, state, go_ptr);
}

int dbll_list_data_index(dbll_list_t *list, dbll_state_t *state) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state)
	) {
		return -1;
	}
	
	return dbll_ptr_to_index(state, list->data_ptr);
}

int dbll_empty_slot_valid(dbll_empty_slot_t *empty_slot) {
	return (
		empty_slot != NULL &&
		empty_slot->is_next_empty >= 0 &&
		empty_slot->is_next_empty <= 1
	);
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

	int list_file_index = dbll_ptr_to_index(state, list_ptr);
	if(list_file_index < 0) {
		return DBLL_ERR;
	}

	int ptr_size = state->header.ptr_size;
	if(
		dbll_ptr_index_copy(
			state, 
			empty_slot->prev_ptr,
			list_file_index
		) < 0 ||

		dbll_ptr_index_copy(
			state, 
			empty_slot->next_ptr,
			list_file_index + ptr_size
		) < 0 ||

		dbll_ptr_index_copy(
			state, 
			empty_slot->this_ptr,
			list_file_index + (ptr_size * 2)
		) < 0
	) {
		return DBLL_ERR;
	}

	empty_slot->is_next_empty = state->file.mem[
		list_file_index + 1 + (ptr_size * 2)
	];

	return DBLL_OK;
}

int dbll_empty_slot_unload(dbll_empty_slot_t *slot) {
	if(slot == NULL) {
		return DBLL_ERR;
	}
	
	slot->prev_ptr = DBLL_NULL;
	slot->next_ptr = DBLL_NULL;
	slot->this_ptr = DBLL_NULL;
	slot->is_next_empty = 0;
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
		index < 0 ||
		dbll_ptr_index_copy(
			state,
			index,
			slot->next_ptr
		) < 0 ||

		dbll_ptr_index_copy(
			state,
			index + ptr_size,
			slot->prev_ptr
		) < 0 ||

		dbll_ptr_index_copy(
			state,
			index + (ptr_size * 2),
			slot->this_ptr
		) < 0
	) {
		return DBLL_ERR;
	}

	state->file.mem[index + (ptr_size * 3)] = (
		slot->is_next_empty
	);
	
	return DBLL_OK;
}

int dbll_data_slot_valid(dbll_data_slot_t *slot) {
	return (
		slot != NULL &&
		slot->data != NULL &&
		slot->this_ptr != DBLL_NULL
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
	if(
		dbll_index_ptr_copy(
			state, 
			index, 
			&slot->next_ptr
		) < 0
	) {
		return DBLL_ERR;
	}
	
	slot->data = &state->file.mem[
		index + state->header.ptr_size
	];
	
	return DBLL_OK;
}

int dbll_data_slot_unload(dbll_data_slot_t *slot) {
	if(slot == NULL) {
		return DBLL_ERR;
	}

	slot->next_ptr = DBLL_NULL;
	slot->data = NULL;
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

	return dbll_data_slot_load(slot, state, slot->next_ptr);
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

uint8_t *dbll_data_slot_page(
	dbll_data_slot_t *slot,
	dbll_state_t *state,
	dbll_ptr_t addr
) {
	if(
		!dbll_data_slot_valid(slot) ||
		!dbll_state_valid(state) || 
		addr == DBLL_NULL
	) {
		return NULL;
	}
	
	int page_number = addr / state->header.data_slot_size;
	int page_offset = addr % state->header.data_slot_size;
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
			return NULL;
		}
	}

	return &fetch_slot.data[page_offset];
}

int dbll_state_valid(dbll_state_t *state) {
	return (
		state != NULL &&
		dbll_file_valid(&state->file) &&
		dbll_header_valid(&state->header) &&
		dbll_empty_slot_valid(&state->last_empty) &&
		dbll_list_valid(&state->root_list)
	);
	
	// gcc gives incorrect warning
	return 1;
}

int dbll_state_load(dbll_state_t *state, const char *path) {
	if(state == NULL || path == NULL) {
		return DBLL_ERR;
	}
	
	if(
		(
			dbll_file_load(&state->file, path) < 0 && 
			dbll_file_make(&state->file, path) < 0
		) ||
		
		dbll_header_load(&state->header, &state->file) < 0
	) {
		dbll_state_unload(state);
		return DBLL_ERR;
	}

	state->last_empty = (dbll_empty_slot_t) { 0 };
	state->root_list = (dbll_list_t) { 0 };
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

	return dbll_state_load(state, path);
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

	return dbll_state_make(state, path);
}

dbll_ptr_t dbll_state_empty_find(dbll_state_t *state) {
	if(!dbll_state_valid(state)) {
		return DBLL_NULL_ERR;
	}

	if(
		state->last_empty.this_ptr == DBLL_NULL || 
		state->last_empty.next_ptr != DBLL_NULL
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
	state->last_empty.is_next_empty = 1;
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

	int grow_index = state->file.size - 1;
	if(
		dbll_file_change(
			&state->file, 
			state->header.header_size
		) < 0
	) {
		return DBLL_NULL_ERR;
	}

	return dbll_index_to_ptr(state, grow_index);
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
		prev_slot->is_next_empty = 0;
		if(dbll_empty_slot_write(prev_slot, state) < 0) {
			return DBLL_ERR;
		}
	}

	slot.next_ptr = DBLL_NULL;
	slot.this_ptr = ptr;
	slot.is_next_empty = 1;
	if(dbll_empty_slot_write(&slot, state) < 0) {
		return DBLL_ERR;
	}

	state->last_empty = slot;

	// calculated in base one, but we need to go over
	// one anyway so we get that addition for free.
	// two for the two sizes in the head, one
	// size for pointers, the other to describe the 
	// size of data sizes
	int index = DBLL_MAGIC_SIZE + 2;
	if(
		dbll_ptr_index_copy(
			state, 
			ptr, 
			index
		) < 0
	) {
		return DBLL_ERR;
	}
	
	state->header.empty_slot_ptr = ptr;
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

	memcpy(
		(uint8_t *)(ptr),
		&state->file.mem[index],
		state->header.ptr_size
	);

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
	for(int i = 0; i < state->header.ptr_size; i++) {
		state->file.mem[index] = (ptr >> (8 * i)) & 0xff;
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

	dbll_ptr_t result = (dbll_ptr_t)(index);
	result -= state->header.header_size;
	result /= state->header.list_size - 1;

	// increment result by one because 0 is reserved
	// for DBLL_NULL only
	result++;
	return result;
}

int dbll_ptr_to_index(dbll_state_t *state, dbll_ptr_t ptr) {
	if(
		!dbll_state_valid(state) || 
		ptr == DBLL_NULL
	) {

		// returns -1 and DBLL_ERR because DBLL_ERR
		// is meaning for an error but here -1
		// carries the same connocation as DBLL_NULL
		return -1;
	}

	// because 0 is reserved, indices are one-based
	// so ptr is subtracted to convert it back to
	// zero-base in order for conversion to happen
	ptr--;
	dbll_header_t *header = &state->header;
	int offset = header->header_size;
	return offset + (ptr * (header->list_size - 1));
}
