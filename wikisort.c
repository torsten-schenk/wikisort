/***********************************************************
 WikiSort (public domain license)
 https://github.com/BonzaiThePenguin/WikiSort
 
 to run:
 clang -o WikiSort.x WikiSort.c -O3
 (or replace 'clang' with 'gcc')
 ./WikiSort.x
***********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>

#define ARRAY(IDX) (sort->array + (IDX) * sort->itemsz)

typedef struct sort sort_t;
typedef struct iter iter_t;
typedef struct range range_t;

struct sort {
	char *array;
	size_t itemsz;
	size_t size;
	int (*cmp)(const void *a, const void *b);

	size_t *map;
};

/* calculate how to scale the index value to the range within the array */
/* the bottom-up merge sort only operates on values that are powers of two, */
/* so scale down to that power of two, then use a fraction to scale back again */
struct iter {
	size_t size, power_of_two;
	size_t numerator, decimal;
	size_t denominator, decimal_step, numerator_step;
};

/* structure to represent ranges within the array */
struct range {
	size_t start;
	size_t end;
};

static inline size_t pow2_floor(
		size_t x) {
	for(size_t i = 0; i < sizeof(x) - 2; i++)
		x |= x >> (1 << i);
	return x - (x >> 1);
}

static inline size_t min(
		const size_t a,
		const size_t b)
{
	if(a < b)
		return a;
	else
		return b;
}

static inline size_t max(
		const size_t a,
		const size_t b)
{
	if(a > b)
		return a;
	else
		return b;
}

/* copy an element from within the array */
static inline void copy_aa(
		const sort_t *sort,
		char *a,
		char *b)
{
	if(sort->map) {
		size_t *map = sort->map;
		size_t aidx = (a - sort->array) / sort->itemsz;
		size_t bidx = (b - sort->array) / sort->itemsz;
		map[aidx] = map[bidx];
	}
	memcpy(a, b, sort->itemsz);
}

/* copy an element from the array to an external location */
static inline size_t copy_pa(
		const sort_t *sort,
		char *a,
		char *b)
{
	memcpy(a, b, sort->itemsz);
	if(sort->map) {
		size_t *map = sort->map;
		size_t bidx = (b - sort->array) / sort->itemsz;
		return map[bidx];
	}
	else
		return 0;
}

/* copy an element from an external location into the array. note that we need the original index of the external element */
static inline void copy_ap(
		const sort_t *sort,
		char *a,
		char *b,
		size_t idx)
{
	if(sort->map) {
		size_t *map = sort->map;
		size_t aidx = (a - sort->array) / sort->itemsz;
		map[aidx] = idx;
	}
	memcpy(a, b, sort->itemsz);
}

/* swap two elements in the array */
static inline void swap_aa(
		const sort_t *sort,
		char *a,
		char *b)
{
	register size_t itemsz = sort->itemsz;
	if(sort->map) {
		size_t *map = sort->map;
		size_t aidx = (a - sort->array) / itemsz;
		size_t bidx = (b - sort->array) / itemsz;
		size_t tmp = map[aidx];
		map[aidx] = map[bidx];
		map[bidx] = tmp;
	}
	for(size_t i = 0; i < itemsz; i++) {
		unsigned char tmp = *a;
		*a++ = *b;
		*b++ = tmp;
	}
}

/* swap a series of values in the array */
static inline void blockswap_aa(
		const sort_t *sort,
		char *a,
		char *b,
		size_t n)
{
	for(size_t i = 0; i < n; i++) {
		swap_aa(sort, a, b);
		a += sort->itemsz;
		b += sort->itemsz;
	}
}

/* this is from http://www.codecodex.com/wiki/Calculate_an_integer_square_root */
size_t isqrt(size_t x)
{
	register size_t op, res, one;
	op = x;
	res = 0;

	/* "one" starts at the highest power of four <= than the argument. */
	one = (size_t)1 << (sizeof(x) * 8 - 2);  /* second-to-top bit set */
	while(one > op)
		one >>= 2;

	while(one != 0) {
		if(op >= res + one) {
			op -= res + one;
			res += one << 1;  // <-- faster than 2 * one
		}
		res >>= 1;
		one >>= 2;
	}
	return res;
}

static inline size_t range_length(
		range_t range)
{
	return range.end - range.start;
}

static inline range_t range_new(
		const size_t start,
		const size_t end)
{
	range_t range;
	range.start = start;
	range.end = end;
	return range;
}

static inline void iter_begin(
		iter_t *me)
{
	me->numerator = me->decimal = 0;
}

static range_t iter_nextRange(
		iter_t *me)
{
	size_t start = me->decimal;
	
	me->decimal += me->decimal_step;
	me->numerator += me->numerator_step;
	if(me->numerator >= me->denominator) {
		me->numerator -= me->denominator;
		me->decimal++;
	}
	return range_new(start, me->decimal);
}

static inline bool iter_finished(
		iter_t *me)
{
	return me->decimal >= me->size;
}

static bool iter_nextLevel(
		iter_t *me)
{
	me->decimal_step += me->decimal_step;
	me->numerator_step += me->numerator_step;
	if(me->numerator_step >= me->denominator) {
		me->numerator_step -= me->denominator;
		me->decimal_step++;
	}
	
	return me->decimal_step < me->size;
}

static inline size_t iter_length(
		iter_t *me)
{
	return me->decimal_step;
}

static iter_t iter_new(
		size_t size2,
		size_t min_level)
{
	iter_t me;
	me.size = size2;
	me.power_of_two = pow2_floor(me.size);
	me.denominator = me.power_of_two / min_level;
	me.numerator_step = me.size % me.denominator;
	me.decimal_step = me.size / me.denominator;
	return me;
}

/* toolbox functions used by the sorter */

/* find the index of the first value within the range that is equal to array[index] */
static size_t BinaryFirst(
		const sort_t *sort,
		const void *value,
		range_t range)
{
	size_t start = range.start, end = range.end - 1;
	if(range.start >= range.end)
		return range.start;
	while(start < end) {
		size_t mid = start + (end - start) / 2;
		if(sort->cmp(ARRAY(mid), value) < 0)
			start = mid + 1;
		else
			end = mid;
	}
	if(start == range.end - 1 && sort->cmp(ARRAY(start), value) < 0)
		start++;
	return start;
}

/* find the index of the last value within the range that is equal to array[index], plus 1 */
static size_t BinaryLast(
		const sort_t *sort,
		const void *value,
		range_t range)
{
	size_t start = range.start, end = range.end - 1;
	if(range.start >= range.end)
		return range.end;
	while(start < end) {
		size_t mid = start + (end - start) / 2;
		if(sort->cmp(value, ARRAY(mid)) >= 0)
			start = mid + 1;
		else
			end = mid;
	}
	if(start == range.end - 1 && sort->cmp(value, ARRAY(start)) >= 0)
		start++;
	return start;
}

/* combine a linear search with a binary search to reduce the number of comparisons in situations */
/* where have some idea as to how many unique values there are and where the next value might be */
static size_t FindFirstForward(
		const sort_t *sort,
		const void *value,
		range_t range,
		size_t unique)
{
	size_t skip, index;
	if(range_length(range) == 0)
		return range.start;
	skip = max(range_length(range) / unique, 1);
	
	for(index = range.start + skip; sort->cmp(ARRAY(index - 1), value) < 0; index += skip)
		if(index >= range.end - skip)
			return BinaryFirst(sort, value, range_new(index, range.end));
	
	return BinaryFirst(sort, value, range_new(index - skip, index));
}

static size_t FindLastForward(
		const sort_t *sort,
		const void *value,
		range_t range,
		size_t unique)
{
	size_t skip, index;
	if(range_length(range) == 0)
		return range.start;
	skip = max(range_length(range) / unique, 1);
	
	for(index = range.start + skip; sort->cmp(value, ARRAY(index - 1)) >= 0; index += skip)
		if(index >= range.end - skip)
			return BinaryLast(sort, value, range_new(index, range.end));
	
	return BinaryLast(sort, value, range_new(index - skip, index));
}

static size_t FindFirstBackward(
		const sort_t *sort,
		const void *value,
		range_t range,
		size_t unique)
{
	size_t skip, index;
	if(range_length(range) == 0)
		return range.start;
	skip = max(range_length(range) / unique, 1);
	
	for(index = range.end - skip; index > range.start && sort->cmp(ARRAY(index - 1), value) >= 0; index -= skip)
		if(index < range.start + skip)
			return BinaryFirst(sort, value, range_new(range.start, index));
	
	return BinaryFirst(sort, value, range_new(index, index + skip));
}

static size_t FindLastBackward(
		const sort_t *sort,
		const void *value,
		range_t range,
		size_t unique)
{
	size_t skip, index;
	if(range_length(range) == 0)
		return range.start;
	skip = max(range_length(range) / unique, 1);
	
	for(index = range.end - skip; index > range.start && sort->cmp(value, ARRAY(index - 1)) < 0; index -= skip)
		if(index < range.start + skip)
			return BinaryLast(sort, value, range_new(range.start, index));
	
	return BinaryLast(sort, value, range_new(index, index + skip));
}

/* n^2 sorting algorithm used to sort tiny chunks of the full array */
static void InsertionSort(
		sort_t *sort,
		range_t range)
{
	char *tmp = alloca(sort->itemsz);
	size_t i, j;
	for(i = range.start + 1; i < range.end; i++) {
		size_t tmpidx = copy_pa(sort, tmp, ARRAY(i));
		for(j = i; j > range.start && sort->cmp(tmp, ARRAY(j - 1)) < 0; j--)
			copy_aa(sort, ARRAY(j), ARRAY(j - 1));
		copy_ap(sort, ARRAY(j), tmp, tmpidx);
	}
}

/* reverse a range of values within the array */
static void reverse(
		const sort_t *sort,
		range_t range)
{
	size_t index;
	for(index = range_length(range) / 2; index > 0; index--)
		swap_aa(sort, ARRAY(range.start + index - 1), ARRAY(range.end - index));
}

/* rotate the values in an array ([0 1 2 3] becomes [1 2 3 0] if we rotate by 1) */
/* this assumes that 0 <= amount <= range.length() */
static void rotate(
		const sort_t *sort,
		size_t amount,
		range_t range)
{
	size_t split;
	range_t range1, range2;
	if(range_length(range) == 0)
		return;
	
	split = range.start + amount;
	range1 = range_new(range.start, split);
	range2 = range_new(split, range.end);
	
	reverse(sort, range1);
	reverse(sort, range2);
	reverse(sort, range);
}

/* merge operation using an internal buffer */
static void MergeInternal(
		const sort_t *sort,
		range_t A,
		range_t B,
		range_t buffer)
{
	/* whenever we find a value to add to the final array, swap it with the value that's already in that spot */
	/* when this algorithm is finished, 'buffer' will contain its original contents, but in a different order */
	register size_t itemsz = sort->itemsz;
	size_t A_count = 0, B_count = 0;
	size_t A_len = range_length(A);
	size_t B_len = range_length(B);
	char *pa = sort->array + A.start * itemsz;
	char *pbuf = sort->array + buffer.start * itemsz;
	
	if(B_len > 0 && A_len > 0) {
		char *pb = sort->array + B.start * itemsz;
		for(;;) {
			if(sort->cmp(pb, pbuf) >= 0) {
				swap_aa(sort, pa, pbuf);
				pa += itemsz;
				pbuf += itemsz;
				A_count++;
				if(A_count >= A_len)
					break;
			}
			else {
				swap_aa(sort, pa, pb);
				pa += itemsz;
				pb += itemsz;
				B_count++;
				if(B_count >= B_len)
					break;
			}
		}
	}
	
	/* swap the remainder of A into the final array */
	blockswap_aa(sort, pbuf, pa, A_len - A_count);
}

/* merge operation without a buffer */
static void MergeInPlace(
		sort_t *sort,
		range_t A,
		range_t B)
{
	if(range_length(A) == 0 || range_length(B) == 0)
		return;
	
	/*
	 this just repeatedly binary searches into B and rotates A into position.
	 the paper suggests using the 'rotation-based Hwang and Lin algorithm' here,
	 but I decided to stick with this because it had better situational performance
	 
	 (Hwang and Lin is designed for merging subarrays of very different sizes,
	 but WikiSort almost always uses subarrays that are roughly the same size)
	 
	 normally this is incredibly suboptimal, but this function is only called
	 when none of the A or B blocks in any subarray contained 2√A unique values,
	 which places a hard limit on the number of times this will ACTUALLY need
	 to binary search and rotate.
	 
	 according to my analysis the worst case is √A rotations performed on √A items
	 once the constant factors are removed, which ends up being O(n)
	 
	 again, this is NOT a general-purpose solution – it only works well in this case!
	 kind of like how the O(n^2) insertion sort is used in some places
	 */
	
	for(;;) {
		/* find the first place in B where the first item in A needs to be inserted */
		size_t mid = BinaryFirst(sort, ARRAY(A.start), B);
		
		/* rotate A into place */
		size_t amount = mid - A.end;
		rotate(sort, range_length(A), range_new(A.start, mid));
		if(B.end == mid)
			break;
		
		/* calculate the new A and B ranges */
		B.start = mid;
		A = range_new(A.start + amount, B.start);
		A.start = BinaryLast(sort, ARRAY(A.start), A);
		if(range_length(A) == 0) break;
	}
}

static void runsort(
		sort_t *sort)
{
#define CMP(A, B) \
	sort->cmp(ARRAY(A), ARRAY(B))

	iter_t iter;

	/* if the array is of size 0, 1, 2, or 3, just sort them like so: */
	if(sort->size < 4) {
		if(sort->size == 3) {
			/* hard-coded insertion sort */
			if(CMP(1, 0) < 0)
				swap_aa(sort, ARRAY(0), ARRAY(1));
			if(CMP(2, 1) < 0) {
				swap_aa(sort, ARRAY(1), ARRAY(2));
				if(CMP(1, 0) < 0)
					swap_aa(sort, ARRAY(0), ARRAY(1));
			}
		}
		else if(sort->size == 2) {
			/* swap the items if they're out of order */
			if(CMP(1, 0) < 0)
				swap_aa(sort, ARRAY(0), ARRAY(1));
		}
		return;
	}

	/* sort groups of 4-8 items at a time using an unstable sorting network, */
	/* but keep track of the original item orders to force it to be stable */
	/* http://pages.ripco.net/~jgamble/nw.html */
#define SWAPIF(X, Y) \
		do { \
			int cmp = CMP(range.start + X, range.start + Y); \
			if(cmp > 0 || (order[X] > order[Y] && cmp >= 0)) { \
				uint8_t tmp = order[X]; \
				order[X] = order[Y]; \
				order[Y] = tmp; \
				swap_aa(sort, ARRAY(range.start + X), ARRAY(range.start + Y)); \
			} \
		} while(0)
	iter = iter_new(sort->size, 4);
	for(iter_begin(&iter); !iter_finished(&iter);) {
		uint8_t order[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
		range_t range = iter_nextRange(&iter);
	
		if(range_length(range) == 8) {
			SWAPIF(0, 1); SWAPIF(2, 3); SWAPIF(4, 5); SWAPIF(6, 7);
			SWAPIF(0, 2); SWAPIF(1, 3); SWAPIF(4, 6); SWAPIF(5, 7);
			SWAPIF(1, 2); SWAPIF(5, 6); SWAPIF(0, 4); SWAPIF(3, 7);
			SWAPIF(1, 5); SWAPIF(2, 6);
			SWAPIF(1, 4); SWAPIF(3, 6);
			SWAPIF(2, 4); SWAPIF(3, 5);
			SWAPIF(3, 4);
			
		}
		else if(range_length(range) == 7) {
			SWAPIF(1, 2); SWAPIF(3, 4); SWAPIF(5, 6);
			SWAPIF(0, 2); SWAPIF(3, 5); SWAPIF(4, 6);
			SWAPIF(0, 1); SWAPIF(4, 5); SWAPIF(2, 6);
			SWAPIF(0, 4); SWAPIF(1, 5);
			SWAPIF(0, 3); SWAPIF(2, 5);
			SWAPIF(1, 3); SWAPIF(2, 4);
			SWAPIF(2, 3);
			
		}
		else if(range_length(range) == 6) {
			SWAPIF(1, 2); SWAPIF(4, 5);
			SWAPIF(0, 2); SWAPIF(3, 5);
			SWAPIF(0, 1); SWAPIF(3, 4); SWAPIF(2, 5);
			SWAPIF(0, 3); SWAPIF(1, 4);
			SWAPIF(2, 4); SWAPIF(1, 3);
			SWAPIF(2, 3);
			
		}
		else if(range_length(range) == 5) {
			SWAPIF(0, 1); SWAPIF(3, 4);
			SWAPIF(2, 4);
			SWAPIF(2, 3); SWAPIF(1, 4);
			SWAPIF(0, 3);
			SWAPIF(0, 2); SWAPIF(1, 3);
			SWAPIF(1, 2);
			
		}
		else if(range_length(range) == 4) {
			SWAPIF(0, 1); SWAPIF(2, 3);
			SWAPIF(0, 2); SWAPIF(1, 3);
			SWAPIF(1, 2);
		}
	}
	if(sort->size < 8)
		return;

	for(;;) {
		/* this is where the in-place merge logic starts!
		 1. pull out two internal buffers each containing √A unique values
			1a. adjust block_size and buffer_size if we couldn't find enough unique values
		 2. loop over the A and B subarrays within this level of the merge sort
		 3. break A and B into blocks of size 'block_size'
		 4. "tag" each of the A blocks with values from the first internal buffer
		 5. roll the A blocks through the B blocks and drop/rotate them where they belong
		 6. merge each A block with any B values that follow, using the cache or the second internal buffer
		 7. sort the second internal buffer if it exists
		 8. redistribute the two internal buffers back into the array */
		
		size_t block_size = isqrt(iter_length(&iter));
		size_t buffer_size = iter_length(&iter)/block_size + 1;
		
		/* as an optimization, we really only need to pull out the internal buffers once for each level of merges */
		/* after that we can reuse the same buffers over and over, then redistribute it when we're finished with this level */
		range_t buffer1, buffer2, A, B;
		bool find_separately;
		size_t index, last, count, find, start, pull_index = 0;
		struct {
			size_t from, to, count;
			range_t range;
		} pull[2];

		pull[0].from = pull[0].to = pull[0].count = 0; pull[0].range = range_new(0, 0);
		pull[1].from = pull[1].to = pull[1].count = 0; pull[1].range = range_new(0, 0);
		
		buffer1 = range_new(0, 0);
		buffer2 = range_new(0, 0);
		
		/* find two internal buffers of size 'buffer_size' each */
		find = buffer_size + buffer_size;
		find_separately = false;
		
		if(find > iter_length(&iter)) {
			/* we can't fit both buffers into the same A or B subarray, so find two buffers separately */
			find = buffer_size;
			find_separately = true;
		}
		
		/* we need to find either a single contiguous space containing 2√A unique values (which will be split up into two buffers of size √A each), */
		/* or we need to find one buffer of < 2√A unique values, and a second buffer of √A unique values, */
		/* OR if we couldn't find that many unique values, we need the largest possible buffer we can get */
		
		/* in the case where it couldn't find a single buffer of at least √A unique values, */
		/* all of the Merge steps must be replaced by a different merge algorithm (MergeInPlace) */
		for(iter_begin(&iter); !iter_finished(&iter);) {
			A = iter_nextRange(&iter);
			B = iter_nextRange(&iter);
			
			/* just store information about where the values will be pulled from and to, */
			/* as well as how many values there are, to create the two internal buffers */
#define PULL(_to) \
		pull[pull_index].range = range_new(A.start, B.end); \
		pull[pull_index].count = count; \
		pull[pull_index].from = index; \
		pull[pull_index].to = _to
			
			/* check A for the number of unique values we need to fill an internal buffer */
			/* these values will be pulled out to the start of A */
			for(last = A.start, count = 1; count < find; last = index, count++) {
				index = FindLastForward(sort, ARRAY(last), range_new(last + 1, A.end), find - count);
				if(index == A.end)
					break;
			}
			index = last;

			
			if(count >= buffer_size) {
				/* keep track of the range within the array where we'll need to "pull out" these values to create the internal buffer */
				PULL(A.start);
				pull_index = 1;
				
				if(count == buffer_size + buffer_size) {
					/* we were able to find a single contiguous section containing 2√A unique values, */
					/* so this section can be used to contain both of the internal buffers we'll need */
					buffer1 = range_new(A.start, A.start + buffer_size);
					buffer2 = range_new(A.start + buffer_size, A.start + count);
					break;
				}
				else if(find == buffer_size + buffer_size) {
					/* we found a buffer that contains at least √A unique values, but did not contain the full 2√A unique values, */
					/* so we still need to find a second separate buffer of at least √A unique values */
					buffer1 = range_new(A.start, A.start + count);
					find = buffer_size;
				}
				else if(find_separately) {
					/* found one buffer, but now find the other one */
					buffer1 = range_new(A.start, A.start + count);
					find_separately = false;
				}
				else {
					/* we found a second buffer in an 'A' subarray containing √A unique values, so we're done! */
					buffer2 = range_new(A.start, A.start + count);
					break;
				}
			}
			else if(pull_index == 0 && count > range_length(buffer1)) {
				/* keep track of the largest buffer we were able to find */
				buffer1 = range_new(A.start, A.start + count);
				PULL(A.start);
			}
			
			/* check B for the number of unique values we need to fill an internal buffer */
			/* these values will be pulled out to the end of B */
			for(last = B.end - 1, count = 1; count < find; last = index - 1, count++) {
				index = FindFirstBackward(sort, ARRAY(last), range_new(B.start, last), find - count);
				if(index == B.start) break;
			}
			index = last;
			
			if(count >= buffer_size) {
				/* keep track of the range within the array where we'll need to "pull out" these values to create the internal buffer */
				PULL(B.end);
				pull_index = 1;
				
				if(count == buffer_size + buffer_size) {
					/* we were able to find a single contiguous section containing 2√A unique values, */
					/* so this section can be used to contain both of the internal buffers we'll need */
					buffer1 = range_new(B.end - count, B.end - buffer_size);
					buffer2 = range_new(B.end - buffer_size, B.end);
					break;
				}
				else if(find == buffer_size + buffer_size) {
					/* we found a buffer that contains at least √A unique values, but did not contain the full 2√A unique values, */
					/* so we still need to find a second separate buffer of at least √A unique values */
					buffer1 = range_new(B.end - count, B.end);
					find = buffer_size;
				}
				else if(find_separately) {
					/* found one buffer, but now find the other one */
					buffer1 = range_new(B.end - count, B.end);
					find_separately = false;
				}
				else {
					/* buffer2 will be pulled out from a 'B' subarray, so if the first buffer was pulled out from the corresponding 'A' subarray, */
					/* we need to adjust the end point for that A subarray so it knows to stop redistributing its values before reaching buffer2 */
					if(pull[0].range.start == A.start) pull[0].range.end -= pull[1].count;
					
					/* we found a second buffer in an 'B' subarray containing √A unique values, so we're done! */
					buffer2 = range_new(B.end - count, B.end);
					break;
				}
			}
			else if(pull_index == 0 && count > range_length(buffer1)) {
				/* keep track of the largest buffer we were able to find */
				buffer1 = range_new(B.end - count, B.end);
				PULL(B.end);
			}
		}
		
		/* pull out the two ranges so we can use them as internal buffers */
		for(pull_index = 0; pull_index < 2; pull_index++) {
			range_t range;
			size_t length = pull[pull_index].count;
			
			if(pull[pull_index].to < pull[pull_index].from) {
				/* we're pulling the values out to the left, which means the start of an A subarray */
				index = pull[pull_index].from;
				for(count = 1; count < length; count++) {
					index = FindFirstBackward(sort, ARRAY(index - 1), range_new(pull[pull_index].to, pull[pull_index].from - (count - 1)), length - count);
					range = range_new(index + 1, pull[pull_index].from + 1);
					rotate(sort, range_length(range) - count, range);
					pull[pull_index].from = index + count;
				}
			} else if(pull[pull_index].to > pull[pull_index].from) {
				/* we're pulling values out to the right, which means the end of a B subarray */
				index = pull[pull_index].from + 1;
				for(count = 1; count < length; count++) {
					index = FindLastForward(sort, ARRAY(index), range_new(index, pull[pull_index].to), length - count);
					range = range_new(pull[pull_index].from, index - 1);
					rotate(sort, count, range);
					pull[pull_index].from = index - 1 - count;
				}
			}
		}
		
		/* adjust block_size and buffer_size based on the values we were able to pull out */
		buffer_size = range_length(buffer1);
		block_size = iter_length(&iter) / buffer_size + 1;
		
		/* the first buffer NEEDS to be large enough to tag each of the evenly sized A blocks, */
		/* so this was originally here to test the math for adjusting block_size above */
		/* assert((iter_length(&iterator) + 1)/block_size <= buffer_size); */
		
		/* now that the two internal buffers have been created, it's time to merge each A+B combination at this level of the merge sort! */
		for(iter_begin(&iter); !iter_finished(&iter);) {
			A = iter_nextRange(&iter);
			B = iter_nextRange(&iter);
			
			/* remove any parts of A or B that are being used by the internal buffers */
			start = A.start;
			if(start == pull[0].range.start) {
				if(pull[0].from > pull[0].to) {
					A.start += pull[0].count;
					
					/* if the internal buffer takes up the entire A or B subarray, then there's nothing to merge */
					/* this only happens for very small subarrays, like √4 = 2, 2 * (2 internal buffers) = 4, */
					/* which also only happens when cache_size is small or 0 since it'd otherwise use MergeExternal */
					if(range_length(A) == 0) continue;
				} else if(pull[0].from < pull[0].to) {
					B.end -= pull[0].count;
					if(range_length(B) == 0) continue;
				}
			}
			if(start == pull[1].range.start) {
				if(pull[1].from > pull[1].to) {
					A.start += pull[1].count;
					if(range_length(A) == 0) continue;
				} else if(pull[1].from < pull[1].to) {
					B.end -= pull[1].count;
					if(range_length(B) == 0) continue;
				}
			}
			
			if(CMP(B.end - 1, A.start) < 0) {
				/* the two ranges are in reverse order, so a simple rotation should fix it */
				rotate(sort, range_length(A), range_new(A.start, B.end));
			}
			else if(CMP(A.end, A.end - 1) < 0) {
				/* these two ranges weren't already in order, so we'll need to merge them! */
				range_t blockA, firstA, lastA, lastB, blockB;
				size_t indexA, findA;
				
				/* break the remainder of A into blocks. firstA is the uneven-sized first A block */
				blockA = range_new(A.start, A.end);
				firstA = range_new(A.start, A.start + range_length(blockA) % block_size);
				
				/* swap the first value of each A block with the value in buffer1 */
				for(indexA = buffer1.start, index = firstA.end; index < blockA.end; indexA++, index += block_size) 
					swap_aa(sort, ARRAY(indexA), ARRAY(index));
				
				/* start rolling the A blocks through the B blocks! */
				/* whenever we leave an A block behind, we'll need to merge the previous A block with any B blocks that follow it, so track that information as well */
				lastA = firstA;
				lastB = range_new(0, 0);
				blockB = range_new(B.start, B.start + min(block_size, range_length(B)));
				blockA.start += range_length(firstA);
				indexA = buffer1.start;
				
				/* if the first unevenly sized A block fits into the cache, copy it there for when we go to Merge it */
				/* otherwise, if the second buffer is available, block swap the contents into that */
				if(range_length(buffer2) > 0)
					blockswap_aa(sort, ARRAY(lastA.start), ARRAY(buffer2.start), range_length(lastA));
				
				if(range_length(blockA) > 0) {
					for(;;) {
						/* if there's a previous B block and the first value of the minimum A block is <= the last value of the previous B block, */
						/* then drop that minimum A block behind. or if there are no B blocks left then keep dropping the remaining A blocks. */
						if((range_length(lastB) > 0 && CMP(lastB.end - 1, indexA) >= 0) || range_length(blockB) == 0) {
							/* figure out where to split the previous B block, and rotate it at the split */
							size_t B_split = BinaryFirst(sort, ARRAY(indexA), lastB);
							size_t B_remaining = lastB.end - B_split;
							
							/* swap the minimum A block to the beginning of the rolling A blocks */
							size_t minA = blockA.start;
							for(findA = minA + block_size; findA < blockA.end; findA += block_size)
								if(CMP(findA, minA) < 0)
									minA = findA;
							blockswap_aa(sort, ARRAY(blockA.start), ARRAY(minA), block_size);
							
							/* swap the first item of the previous A block back with its original value, which is stored in buffer1 */
							swap_aa(sort, ARRAY(blockA.start), ARRAY(indexA));
							indexA++;
							
							/*
							 locally merge the previous A block with the B values that follow it
							 if lastA fits into the external cache we'll use that (with MergeExternal),
							 or if the second internal buffer exists we'll use that (with MergeInternal),
							 or failing that we'll use a strictly in-place merge algorithm (MergeInPlace)
							 */
							if(range_length(buffer2) > 0)
								MergeInternal(sort, lastA, range_new(lastA.end, B_split), buffer2);
							else
								MergeInPlace(sort, lastA, range_new(lastA.end, B_split));
							
							if(range_length(buffer2) > 0) {
								/* copy the previous A block into the cache or buffer2, since that's where we need it to be when we go to merge it anyway */
								blockswap_aa(sort, ARRAY(blockA.start), ARRAY(buffer2.start), block_size);
								
								/* this is equivalent to rotating, but faster */
								/* the area normally taken up by the A block is either the contents of buffer2, or data we don't need anymore since we memcopied it */
								/* either way, we don't need to retain the order of those items, so instead of rotating we can just block swap B to where it belongs */
								blockswap_aa(sort, ARRAY(B_split), ARRAY(blockA.start + block_size - B_remaining), B_remaining);
							} else {
								/* we are unable to use the 'buffer2' trick to speed up the rotation operation since buffer2 doesn't exist, so perform a normal rotation */
								rotate(sort, blockA.start - B_split, range_new(B_split, blockA.start + block_size));
							}
							
							/* update the range for the remaining A blocks, and the range remaining from the B block after it was split */
							lastA = range_new(blockA.start - B_remaining, blockA.start - B_remaining + block_size);
							lastB = range_new(lastA.end, lastA.end + B_remaining);
							
							/* if there are no more A blocks remaining, this step is finished! */
							blockA.start += block_size;
							if(range_length(blockA) == 0)
								break;
							
						} else if(range_length(blockB) < block_size) {
							/* move the last B block, which is unevenly sized, to before the remaining A blocks, by using a rotation */
							/* the cache is disabled here since it might contain the contents of the previous A block */
							rotate(sort, blockB.start - blockA.start, range_new(blockA.start, blockB.end));
							
							lastB = range_new(blockA.start, blockA.start + range_length(blockB));
							blockA.start += range_length(blockB);
							blockA.end += range_length(blockB);
							blockB.end = blockB.start;
						} else {
							/* roll the leftmost A block to the end by swapping it with the next B block */
							blockswap_aa(sort, ARRAY(blockA.start), ARRAY(blockB.start), block_size);
							lastB = range_new(blockA.start, blockA.start + block_size);
							
							blockA.start += block_size;
							blockA.end += block_size;
							blockB.start += block_size;
							
							if(blockB.end > B.end - block_size) blockB.end = B.end;
							else blockB.end += block_size;
						}
					}
				}
				
				/* merge the last A block with the remaining B values */
				if(range_length(buffer2) > 0)
					MergeInternal(sort, lastA, range_new(lastA.end, B.end), buffer2);
				else
					MergeInPlace(sort, lastA, range_new(lastA.end, B.end));
			}
		}
		
		/* when we're finished with this merge step we should have the one or two internal buffers left over, where the second buffer is all jumbled up */
		/* insertion sort the second buffer, then redistribute the buffers back into the array using the opposite process used for creating the buffer */
		
		/* while an unstable sort like quicksort could be applied here, in benchmarks it was consistently slightly slower than a simple insertion sort, */
		/* even for tens of millions of items. this may be because insertion sort is quite fast when the data is already somewhat sorted, like it is here */
		InsertionSort(sort, buffer2);
		
		for(pull_index = 0; pull_index < 2; pull_index++) {
			size_t amount, unique = pull[pull_index].count * 2;
			if(pull[pull_index].from > pull[pull_index].to) {
				/* the values were pulled out to the left, so redistribute them back to the right */
				range_t buffer = range_new(pull[pull_index].range.start, pull[pull_index].range.start + pull[pull_index].count);
				while(range_length(buffer) > 0) {
					index = FindFirstForward(sort, ARRAY(buffer.start), range_new(buffer.end, pull[pull_index].range.end), unique);
					amount = index - buffer.end;
					rotate(sort, range_length(buffer), range_new(buffer.start, index));
					buffer.start += (amount + 1);
					buffer.end += amount;
					unique -= 2;
				}
			}
			else if(pull[pull_index].from < pull[pull_index].to) {
				/* the values were pulled out to the right, so redistribute them back to the left */
				range_t buffer = range_new(pull[pull_index].range.end - pull[pull_index].count, pull[pull_index].range.end);
				while(range_length(buffer) > 0) {
					index = FindLastBackward(sort, ARRAY(buffer.end - 1), range_new(pull[pull_index].range.start, buffer.start), unique);
					amount = buffer.start - index;
					rotate(sort, amount, range_new(index, buffer.end));
					buffer.start -= amount;
					buffer.end -= (amount + 1);
					unique -= 2;
				}
			}
		}
		
		/* double the size of each A and B subarray that will be merged in the next level */
		if(!iter_nextLevel(&iter))
			break;
	}
}

void wikisort_trace(
		void *base,
		size_t size,
		size_t itemsz,
		int (*cmp)(const void *a, const void *b),
		size_t *map) /* size: 'size' */
{
	sort_t sort;
	sort.array = base;
	sort.itemsz = itemsz;
	sort.size = size;
	sort.cmp = cmp;
	sort.map = map;
	for(size_t i = 0; i < size; i++)
		map[i] = i;
	runsort(&sort);
}

void wikisort(
		void *base,
		size_t size,
		size_t itemsz,
		int (*cmp)(const void *a, const void *b))
{
	sort_t sort;
	sort.array = base;
	sort.itemsz = itemsz;
	sort.size = size;
	sort.cmp = cmp;
	sort.map = NULL;
	runsort(&sort);
}

