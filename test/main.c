#include <test.h>
#include <dbll.h>

int test_load_unload() {
	dbll_state_t state = { 0 };
	if(dbll_state_load(&state, "db/test-load-unload.dbll") < 0) {
		return TEST_FAIL;
	}

	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

int test_make_replace() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-make-replace.dbll") < 0) {
		return TEST_FAIL;
	}

	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

int test_alloc() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-alloc.dbll") < 0) {
		return TEST_FAIL;
	}
		dbll_ptr_t new_list = dbll_state_alloc(&state);
		if(new_list == DBLL_NULL) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}
	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

int test_mark_free() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-mark-free.dbll") < 0) {
		return TEST_FAIL;
	}
		dbll_ptr_t new_list = dbll_state_alloc(&state);
		if(new_list == DBLL_NULL) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}

		if(dbll_state_mark_free(&state, new_list) < 0) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}
	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

int test_mark_data_write() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-mark-data-write.dbll") < 0) {
		return TEST_FAIL;
	}
		if(dbll_list_data_resize(&state->root_list, &state, 3) < 0) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}

		int data_index = dbll_list_data_index(&state->root_list, &state);
		if(data_index == -1) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}

		int data[] = { 1, 2, 3, 4 };
		if(
			dbll_state_write(
				&state, 
				data_index, 
				data, 
				ARRAY_SIZE(data)
			) < 0
		) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}
	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_OK;
}

const test_func_f dbll_test_funcs[] = {
	test_load_unload,
	test_make_replace,
	test_alloc,
	test_mark_free
};

int main() {
	int result = test_funcs(
		dbll_test_funcs, 
		ARRAY_SIZE(dbll_test_funcs)
	);

	return result == TEST_FAIL;
}
