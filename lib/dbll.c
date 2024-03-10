#include <dbll.h>

#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

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
		file->cursor >= 0 &&
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

	file->cursor = 0;
	file->mem = (uint8_t*)(mmap(
			NULL,
			file->size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			file->desc,
			0
	));

	if(file->mem == (uint8_t *)(-1)) {
		return DBLL_ERR;
	}

	return DBLL_OK;
}

int dbll_file_unload(dbll_file_t *file) {
	if(file == NULL) {
		return DBLL_ERR;
	}

	// if we can't close/unmap it, it was never open anyway
	// so don't check for error
	if(file->mem != NULL) {
		msync(file->mem, file->size, MS_SYNC);
		munmap(file->mem, file->size);
	}

	if(file->desc > 0) {
		close(file->desc);
	}

	file->mem = NULL;
	file->size = 0;
	file->cursor = 0;
	file->desc = 0;
	return DBLL_OK;
}

static const uint8_t file_boilerplate[] = {
	'd', 'b', 'l', 'l', 4, 4, 0
};

int dbll_file_make(dbll_file_t *file, const char *path) {
	if(!dbll_file_valid(file) || path == NULL) {
		return DBLL_ERR;
	}
	
	// can't make already existing file
	if(access(path, F_OK) < 0) {
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

	if(
		ftruncate(
			file->desc, 
			file->size + size
		) < 0
	) {
		return DBLL_ERR;
	}
	
	file->size += size;
	return DBLL_OK;
}

// the magic number spells out "dbll" but in decimal form
static const uint32_t dbll_header_magic = 1819042404;
void dbll_header_print(dbll_header_t *header) {
	if(header == NULL) {
		printf("header is null\n");
		return;
	}
	
	uint32_t magic_int = *((uint32_t *)(header->magic));
	printf(
		"header magic (%d) and dbll magic (%d)\n", 
		magic_int, 
		dbll_header_magic
	);
	
	printf("ptr size (%d)\n", header->ptr_size);
	printf("data size (%d)\n", header->data_size);
	printf("header size (%d)\n", header->header_size);
}

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

	uint8_t *list_file_ptr = dbll_ptr_to_mem(state, list_ptr);
	if(list_file_ptr == NULL) {
		return DBLL_ERR;
	}
	
	int ptr_size = state->header.ptr_size;
	if(
		dbll_mem_ptr_copy(state, list_file_ptr, &list->head_ptr) < 0 ||
		dbll_mem_ptr_copy(
			state, 
			&list_file_ptr[ptr_size], 
			&list->tail_ptr
		) < 0 ||
		
		dbll_mem_ptr_copy(
			state, 
			&list_file_ptr[ptr_size * 2], 
			&list->data_ptr
		) < 0
	) {
		dbll_list_unload(list);
		return DBLL_ERR;
	}

	memcpy(
		(char *)(&list->data_size),
		&list_file_ptr[ptr_size * 3],
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

uint8_t *dbll_list_data(dbll_list_t *list, dbll_state_t *state) {
	if(
		!dbll_list_valid(list) ||
		!dbll_state_valid(state)
	) {
		return NULL;
	}
	
	return dbll_ptr_to_mem(state, list->data_ptr);
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
		!dbll_empty_slot_valid(empty_slot)
	) {
		return DBLL_ERR;
	}

	uint8_t *list_file_ptr = dbll_ptr_to_mem(state, list_ptr);
	if(list_file_ptr == NULL) {
		return DBLL_ERR;
	}
	
	int ptr_size = state->header.ptr_size;

	// no error check because everything is valid
	if(
		dbll_mem_ptr_copy(
			state, 
			list_file_ptr, 
			&empty_slot->prev_ptr
		) < 0 ||

		dbll_mem_ptr_copy(
			state, 
			&list_file_ptr[ptr_size], 
			&empty_slot->next_ptr
		) < 0 ||

		dbll_mem_ptr_copy(
			state, 
			&list_file_ptr[ptr_size * 2], 
			&empty_slot->this_ptr
		) < 0
	) {
		return DBLL_ERR;
	}
	
	empty_slot->is_next_empty = list_file_ptr[(ptr_size * 2) + 1];
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

	uint8_t *mem = dbll_ptr_to_mem(state, slot->this_ptr);
	int ptr_size = state->header.ptr_size;
	if(
		mem == NULL ||
		dbll_ptr_mem_copy(
			state,
			slot->next_ptr,
			mem
		) < 0 ||

		dbll_ptr_mem_copy(
			state,
			slot->prev_ptr,
			mem + ptr_size
		) < 0 ||

		dbll_ptr_mem_copy(
			state,
			slot->this_ptr,
			mem + (ptr_size * 2)
		) < 0
	) {
		return DBLL_ERR;
	}

	mem[ptr_size * 3] = slot->is_next_empty;
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

	uint8_t *mem = dbll_ptr_to_mem(state, ptr);
	if(
		dbll_mem_ptr_copy(
			state, 
			mem, 
			&slot->next_ptr
		) < 0
	) {
		return DBLL_ERR;
	}
	
	slot->data = mem + state->header.ptr_size;
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
		if(slot->next_ptr != DBLL_ERR && !slot->is_marked) {
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

dbll_ptr_t dbll_state_empty_find(dbll_state_t *state) {
	if(
		!dbll_state_valid(state) ||
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
		return DBLL_NULL;
	}

	state->last_empty.next_ptr = DBLL_NULL;
	state->last_empty.is_next_empty = 1;
	if(
		dbll_empty_slot_write(
			&state->last_empty, 
			state
		) < 0
	) {
		return DBLL_NULL;
	}

	return current;
}

dbll_ptr_t dbll_state_alloc(dbll_state_t *state) {
	if(!dbll_state_valid(state)) {
		return DBLL_NULL;
	}

	dbll_ptr_t empty_slot = dbll_state_empty_find(state);
	if(empty_slot != DBLL_NULL) {
		return empty_slot;
	}

	uint8_t *grow_ptr = &state->file.mem[state->file.size - 1];
	if(
		dbll_file_change(
			&state->file, 
			state->header.header_size
		) < 0
	) {
		return DBLL_NULL;
	}
	
	return dbll_mem_to_ptr(state, grow_ptr);
}

int dbll_state_mark_free(dbll_state_t *state, dbll_ptr_t ptr) {
	if(!dbll_state_valid(state)) {
		return DBLL_ERR;
	}

	dbll_empty_slot_t slot = { 0 };
	if(dbll_empty_slot_load(&slot, state, ptr) < 0) {
		return DBLL_ERR;
	}

	dbll_empty_slot_t *prev_slot = &state->last_empty;
	slot.prev_ptr = prev_slot->this_ptr;
	slot.next_ptr = DBLL_NULL;
	slot.this_ptr = ptr;
	slot.is_next_empty = 1;
	prev_slot->next_ptr = slot.this_ptr;
	prev_slot->is_next_empty = 0;
	dbll_empty_slot_write(prev_slot, state);
	dbll_empty_slot_write(&slot, state);
	state->last_empty = slot;
	return DBLL_OK;
}

int dbll_mem_ptr_copy(
	dbll_state_t *state, 
	uint8_t *mem,
	dbll_ptr_t *ptr
) {
	if(
		!dbll_state_valid(state) || 
		mem == NULL || 
		ptr == NULL ||
		*ptr == DBLL_NULL
	) {
		return DBLL_ERR;
	}

	memcpy(
		(uint8_t *)(ptr),
		mem,
		state->header.ptr_size
	);

	return DBLL_OK;
}

int dbll_ptr_mem_copy(
	dbll_state_t *state,
	dbll_ptr_t ptr,
	uint8_t *mem
) {
	if(
		!dbll_state_valid(state) ||
		ptr == DBLL_NULL ||
		mem == NULL
	) {
		return DBLL_ERR;
	}

	memcpy(
		mem,
		(uint8_t *)(&ptr),
		state->header.header_size
	);
	
	return DBLL_OK;
}

dbll_ptr_t dbll_mem_to_ptr(dbll_state_t *state, uint8_t *mem) {
	if(!dbll_state_valid(state) || mem == NULL) {
		return DBLL_NULL;
	}

	size_t mem_int = (size_t)(mem);
	size_t file_mem_int = (size_t)(state->file.mem);
	mem_int -= file_mem_int - state->header.header_size;
	mem_int /= state->header.list_size;
	return (dbll_ptr_t)(mem_int);
}

uint8_t *dbll_ptr_to_mem(dbll_state_t *state, dbll_ptr_t ptr) {
	if(!dbll_state_valid(state) || ptr == DBLL_NULL) {
		return NULL;
	}

	ptr--;
	dbll_file_t *file = &state->file;
	dbll_header_t *header = &state->header;
	int offset = header->header_size;
	return &file->mem[offset + (ptr * header->list_size)];
}
