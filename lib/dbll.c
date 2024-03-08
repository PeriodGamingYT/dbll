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
		printf("file_size couldn't get stat!\n");
		perror("errno");
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
		printf("couldn't open file!\n");
		perror("errno");
		goto error;
	}

	file->size = file_size(file->desc);
	if(file->size < 0) {
		printf("couldn't get file stat!\n");
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
		printf("couldn't mmap file!\n");
		perror("errno");
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

int dbll_file_unload(dbll_file_t *file) {
	if(file == NULL) {
		return DBLL_ERR;
	}

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

static const uint8_t file_boilerplate[] = {
	'd', 'b', 'l', 'l', 1, 1, 1
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
		printf("couldn't open or create file!\n");
		perror("errno");
		return DBLL_ERR;
	}
	
	if(
		write(
			desc, 
			file_boilerplate, 
			sizeof(file_boilerplate)
		) < 0
	) {
		printf("couldn't write to file!\n");
		perror("errno");
		return DBLL_ERR;
	}
	
	if(close(desc) < 0) {
		printf("coudn't close file!\n");
		perror("errno");
		return DBLL_ERR;
	}
	
	return dbll_file_load(file, path);
}

// the magic number spells out "dbll" but in decimal form
static const uint32_t dbll_header_magic = 1819042404;
void dbll_header_print(dbll_header_t *header) {
	if(header == NULL) {
		printf("header is null\n");
		return;
	}
	
	uint32_t magic_int = *((uint32_t *)(header->magic));
	printf("header magic (%d) and dbll magic (%d)\n", magic_int, dbll_header_magic);
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
	header->list_size = (header->ptr_size * 3) + header->data_size;
	header->header_size = DBLL_MAGIC_SIZE + 1 + header->ptr_size;
	return DBLL_OK;
}

int dbll_header_unload(dbll_header_t *header) {
	if(!dbll_header_valid(header)) {
		printf("header couldn't be validated\n");
		dbll_header_print(header);
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
		printf("state and/or list isn't valid\n");
		return DBLL_ERR;
	}

	uint8_t *list_file_ptr = dbll_ptr_to_mem(state, list_ptr);
	if(list_file_ptr == NULL) {
		printf("list memory couldn't be accessed!\n");
		return DBLL_ERR;
	}
	
	int ptr_size = state->header.ptr_size;

	// no error check because everything is valid
	dbll_mem_to_ptr(state, list_file_ptr, &list->head_ptr);
	dbll_mem_to_ptr(state, &list_file_ptr[ptr_size], &list->tail_ptr);
	dbll_mem_to_ptr(
		state, 
		&list_file_ptr[ptr_size * 2], 
		&list->data_ptr
	);

	memcpy(
		(char *)(&list->data_size),
		&list_file_ptr[ptr_size * 3],
		state->header.data_size
	);
	
	return DBLL_OK;
}

int dbll_list_unload(dbll_list_t *list) {
	if(list == NULL) {
		printf("list was null!\n");
		return DBLL_ERR;
	}
	
	list->head_ptr = DBLL_NULL;
	list->tail_ptr = DBLL_NULL;
	list->data_ptr = DBLL_NULL;
	list->data_size = 0;
	return DBLL_OK;
}

int dbll_list_go(dbll_list_t *list, dbll_state_t *state, list_go_e go) {
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
	dbll_mem_to_ptr(
		state, 
		list_file_ptr, 
		&empty_slot->prev_ptr
	);
	
	dbll_mem_to_ptr(
		state, 
		&list_file_ptr[ptr_size], 
		&empty_slot->next_ptr
	);
	
	dbll_mem_to_ptr(
		state, 
		&list_file_ptr[ptr_size * 2], 
		&empty_slot->empty_ptr
	);
	
	empty_slot->is_next_empty = list_file_ptr[(ptr_size * 2) + 1];
	return DBLL_OK;
}

int dbll_empty_slot_unload(dbll_empty_slot_t *slot) {
	if(slot == NULL) {
		return DBLL_ERR;
	}
	
	slot->prev_ptr = DBLL_NULL;
	slot->next_ptr = DBLL_NULL;
	slot->empty_ptr = DBLL_NULL;
	slot->is_next_empty = 0;
	return DBLL_OK;
}

int dbll_state_valid(dbll_state_t *state) {
	return (
		state != NULL &&
		dbll_file_valid(&state->file) &&
		dbll_header_valid(&state->header) &&
		dbll_empty_slot_valid(&state->last_empty) &&
		dbll_list_valid(&state->root_list)
	);
	
	// gcc gives bad warning
	return 1;
}

int dbll_state_load(dbll_state_t *state, const char *path) {
	if(state == NULL || path == NULL) {
		return DBLL_ERR;
	}
	
	if(
		dbll_file_load(&state->file, path) < 0 && 
		dbll_file_make(&state->file, path) < 0
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

int dbll_mem_to_ptr(
	dbll_state_t *state, 
	uint8_t *mem_start,
	dbll_ptr_t *ptr
) {
	if(!dbll_state_valid(state) || mem_start == NULL) {
		return DBLL_ERR;
	}

	memcpy(
		(uint8_t *)(ptr),
		mem_start,
		state->header.ptr_size
	);

	return DBLL_OK;
}

uint8_t *dbll_ptr_to_mem(dbll_state_t *state, dbll_ptr_t ptr) {
	if(!dbll_state_valid(state)) {
		return NULL;
	}

	dbll_file_t *file = &state->file;
	dbll_header_t *header = &state->header;
	int offset = header->header_size - 1;
	return &file->mem[offset + (ptr * header->list_size)];
}
