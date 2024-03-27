#include <test.h>

int test_fail(int line) {
	printf("test failed at line %d!\n", line);
	return TEST_FAIL;
}

int test_funcs(const test_func_t *funcs, int size) {
	if(funcs == NULL || size < 0) {
		return TEST_FAIL;
	}

	// make it print everything, even on crash
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	int result = TEST_PASS;
	for(int i = 0; i < size; i++) {
		int current_test = funcs[i].test();
		printf(
			"%s test \"%s\"!\n",
			current_test == TEST_FAIL 
				? "failed" 
				: "passed",

			funcs[i].name
		);

		if(current_test == TEST_FAIL) {
			result = TEST_FAIL;
		}
	}

	return result;
}
