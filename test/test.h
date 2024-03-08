#ifndef TEST_H
#define TEST_H
	#include <stdio.h>

	#define TEST_PASS 0
	#define TEST_FAIL -1
	#define ARRAY_SIZE(_array) \
		(sizeof(_array) / sizeof((_array)[0]))

	typedef int (*test_func_f)();
	int test_funcs(const test_func_f *, int);
#endif
