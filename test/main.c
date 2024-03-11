#include <test.h>
#include <dbll.h>

int test_0() {
	dbll_state_t state = { 0 };
	if(dbll_state_load(&state, "db/test-0.dbll") < 0) {
		return TEST_FAIL;
	}

	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

int test_1() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-1.dbll") < 0) {
		return TEST_FAIL;
	}

	if(dbll_state_unload(&state) < 0) {
		return TEST_FAIL;
	}

	return TEST_PASS;
}

int test_2() {
	dbll_state_t state = { 0 };
	if(dbll_state_make_replace(&state, "db/test-2.dbll") < 0) {
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

const test_func_f dbll_test_funcs[] = {
	test_0,
	test_1,
	test_2
};

int main() {
	int result = test_funcs(
		dbll_test_funcs, 
		ARRAY_SIZE(dbll_test_funcs)
	);

	return result == TEST_FAIL;
}
