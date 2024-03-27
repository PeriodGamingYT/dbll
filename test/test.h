#ifndef TEST_H
#define TEST_H
	#include <stdio.h>

	#define TEST_PASS 0
	#define TEST_FAIL -1
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

	int test_funcs(const test_func_t *, int);
#endif
