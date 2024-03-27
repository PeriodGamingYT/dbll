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

// XXX: the implementation need to write this function is not done yet,
// as such, do not use the results of the test as of yet until everything
// in here is implemented
int test_data_write() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-data-write.dbll") < 0) {
		return TEST_FAIL;
	}
		if(dbll_list_data_resize(&state.root_list, &state, 3) < 0) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}

		dbll_data_slot_t slot = { 0 };
		if(
			dbll_data_slot_load(
				&slot,
				&state,
				state.root_list.data_ptr
			) < 0
		) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}

		int data[] = { 1, 2, 3, 4 };
		if(
			dbll_data_slot_write_mem(
				&slot,
				&state,
				0,
				(uint8_t *)(data),
				ARRAY_SIZE(data)
			) < 0
		) {
			dbll_state_unload(&state);
			return TEST_FAIL;
		}
	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

const test_func_t dbll_test_funcs[] = {
	TEST_FUNC(test_load_unload),
	TEST_FUNC(test_make_replace),
	TEST_FUNC(test_alloc),
	TEST_FUNC(test_mark_free),
	TEST_FUNC(test_data_write)
};

int main() {
	int result = test_funcs(
		dbll_test_funcs, 
		ARRAY_SIZE(dbll_test_funcs)
	);

	return result == TEST_FAIL;
}
