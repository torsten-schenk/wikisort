void wikisort_trace(
		void *base,
		size_t size,
		size_t itemsz,
		int (*cmp)(const void *a, const void *b),
		size_t *map); /* size: 'size' */

void wikisort(
		void *base,
		size_t size,
		size_t itemsz,
		int (*cmp)(const void *a, const void *b));

