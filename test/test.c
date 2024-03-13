#include <test.h>

int test_funcs(const test_func_f *funcs, int size) {
	if(funcs == NULL || size < 0) {
		return TEST_FAIL;
	}

	// make it print everything, even on crash
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	int result = TEST_PASS;
	for(int i = 0; i < size; i++) {
		int current_test = funcs[i]();
		printf(
			"%s test %d!\n", 
			current_test == TEST_FAIL 
				? "failed" 
				: "passed",

			i
		);

		if(current_test == TEST_FAIL) {
			result = TEST_FAIL;
		}
	}

	return result;
}
