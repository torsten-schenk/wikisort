#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "wikisort.h"

typedef struct test test_t;

struct test {
	int v[2];
};

static int cmp_test(
		const void *a_,
		const void *b_)
{
	const test_t *a = a_;
	const test_t *b = b_;
	if(a->v[0] < b->v[0])
		return -1;
	else if(a->v[0] > b->v[0])
		return 1;
	else
		return 0;
}

#define N 1927 /* total number of different v[0] values */
#define M 9718187 /* array size */
static int test(
		size_t ntotal)
{
	test_t *array = malloc(ntotal * sizeof(*array));
	test_t *unsorted = malloc(ntotal * sizeof(*array));
	size_t *order = malloc(ntotal * sizeof(*order));
	size_t *expect = malloc(ntotal * sizeof(*order));
	size_t off[N]; /* offset in expected array where v[0]==index, v[1]==0 */
	int size[N]; /* counters for test_t.v[1] values */
	int prev;
	srand(1);

	for(int i = 0; i < N; i++)
		size[i] = 0;

	for(size_t i = 0; i < ntotal; i++) {
		size_t ridx = rand() % N;
		array[i].v[0] = ridx;
		array[i].v[1] = size[ridx]++;
		unsorted[i] = array[i];
	}

	off[0] = 0;
	for(int i = 1; i < N; i++)
		off[i] = off[i - 1] + size[i - 1];
	for(size_t i = 0; i < ntotal; i++)
		expect[i] = off[array[i].v[0]] + array[i].v[1];

	wikisort_trace(array, ntotal, sizeof(test_t), cmp_test, order);

	for(size_t i = 0; i < ntotal; i++) {
		assert(off[array[i].v[0]] + array[i].v[1] == i);
		assert(expect[order[i]] == i);
	}
	free(array);
	free(unsorted);
	free(order);
	free(expect);
}

int main()
{
	test(M);
}

