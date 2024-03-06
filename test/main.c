#include <test.h>
#include <dbll.h>

int test_0() {
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

	return result;
}