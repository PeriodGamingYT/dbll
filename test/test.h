#ifndef TEST_H
#define TEST_H
	#include <stdio.h>

	#define TEST_PASS 0
	#define TEST_FAIL -1

	// if you are wanting to return a TEST_FAIL,
	// do TEST_FAIL_ERR instead to print which line
	// the test failed
	#define TEST_FAIL_ERR test_fail(__LINE__)
	#define ARRAY_SIZE(_array) \
		(sizeof(_array) / sizeof((_array)[0]))

	typedef int (*test_func_f)();

	// this wrapper struct is here to give nicer
	// names to functions
	typedef struct {
		test_func_f test;
		const char *name;
	} test_func_t;

	// macro is here to generate boilterplate for
	// test_func_t
	#define TEST_FUNC(_name) \
		{ \
			_name, \
			#_name \
		}

	int test_fail(int);
	int test_funcs(const test_func_t *, int);
#endif
