#include <dbll.h>
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
		goto error;
	}

	file->desc = open(path, O_RDWR);
	if(file->desc < 0) {
		goto error;
	}

	file->size = file_size(file->desc);
	if(file->size < 0) {
		goto error;
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
		goto error;
	}

	return DBLL_OK;
	error: {
		if(file->desc > 0) {
			close(file->desc);
		}

		file->desc = 0;
		file->size = 0;
		file->cursor = 0;
		file->mem = NULL;
		return DBLL_ERR;
	}
}

int dbll_file_unload(dbll_file_t **ref_file) {
	if(ref_file == NULL || *ref_file == NULL) {
		return DBLL_ERR;
	}

	dbll_file_t *file = *ref_file;

	// if we can't close/unmap it, it was never open anyway
	// so don't check for error
	if(file->mem != NULL) {
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
	header->list_size = (header->ptr_size * 3) + header->data_size;
	header->header_size = DBLL_MAGIC_SIZE + 1 + header->ptr_size;
	return DBLL_OK;
}

int dbll_mem_to_ptr(
	dbll_state_t *state, 
	uint8_t *mem_start,
	dbll_ptr_t ptr
) {
	if(!dbll_state_valid(state) || mem == NULL) {
		return DBLL_ERR;
	}

	memcpy(
		ptr,
		mem_start,
		state->header.ptr_size
	);

	return DBLL_OK;
}

uint8_t *dbll_ptr_to_mem(dbll_state_t *state, dbll_ptr_t ptr) {
	if(!dbll_state_valid(state)) {
			return NULL;
	}

	dbll_header_t *header = &state->header;
	int offset = header->header_size - 1;
	return &file->mem[offset + (ptr * header->list_size)];
}

int dbll_list_load(
	dbll_list_t *list, 
	dbll_state_t *state, 
	dbll_ptr_t list_ptr
) {
	if(!dbll_state_valid(state) || list == NULL) {
		return DBLL_ERR;
	}

	uint8_t *list_file_ptr = dbll_ptr_to_mem(state, list_ptr);
	if(list_file_ptr == NULL) {
		return DBLL_ERR;
	}

	// no error check because everythings valid
	dbll_mem_to_ptr(state, list_file_ptr, list.head_ptr);
	dbll_mem_to_ptr(state, &list_file_ptr[ptr_size], list.tail_ptr);
	dbll_mem_to_ptr(
		state, 
		&list_file_ptr[ptr_size * 2], 
		list.data_ptr
	);

	memcpy(
		list->data_size,
		&list_file_ptr[ptr_size * 3],
		state->header.data_size
	);
}

int dbll_empty_slot_load(
	dbll_empty_slot_t *empty_slot,
	dbll_state_t *state,
	dbll_ptr_t list_ptr
) {
	if(!dbll_state_valid(state) || empty_slot == NULL) {
		return DBLL_ERR;
	}

	uint8_t *list_file_ptr = dbll_ptr_to_mem(state, list_ptr);
	if(list_file_ptr == NULL) {
		return DBLL_ERR;
	}

	// no error check because everythings valid
	dbll_mem_to_ptr(state, list_file_ptr, empty_slot->prev_ptr);
	dbll_mem_to_ptr(state, &list_file_ptr[ptr_size], empty_slot->next_ptr);
	dbll_mem_to_ptr(state, &list_file_ptr[ptr_size * 2], empty_slot->empty_ptr);
	empty_slot->is_next_empty = list_file_ptr[(ptr_size * 2) + 1];
	return DBLL_OK;
}

int dbll_state_load(dbll_state_t *state, const char *path) {
	if(
		dbll_file_load(&state->file, path) < 0 && 
		dbll_file_make(&state->file, path)
	) {
		goto error;
	}

	if(dbll_header_load(&state->header, &state->file) < 0) {
		goto error;
	}

	return DBLL_OK;
	error: {
		dbll_file_unload(&state->file);
		memset(state, 0, sizeof(dbll_state_t));
		return DBLL_ERR;
	}
}
