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

const test_func_f dbll_test_funcs[] = {
	test_0
};

int main() {
	int result = test_funcs(
		dbll_test_funcs, 
		ARRAY_SIZE(dbll_test_funcs)
	);

	return result == TEST_FAIL;
}
