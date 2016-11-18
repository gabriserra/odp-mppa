#ifndef __TEST_HELPER__
#define __TEST_HELPER__

#include <stdio.h>
#include <stdlib.h>

#define test_assert_ret(EX)	if (!(EX)) {test_assert_print(#EX, __FILE__, __LINE__); return 1;}

static inline void test_assert_print (const char *msg, const char *file, int line)
{
	fprintf(stderr, "Assertion %s failed, file %s, line %d\n", msg, file, line);
	exit(1);
}

#endif
