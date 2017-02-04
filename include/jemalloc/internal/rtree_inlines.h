#ifndef JEMALLOC_INTERNAL_RTREE_INLINES_H
#define JEMALLOC_INTERNAL_RTREE_INLINES_H

#ifndef JEMALLOC_ENABLE_INLINE
unsigned	rtree_start_level(const rtree_t *rtree, uintptr_t key);
uintptr_t	rtree_subkey(rtree_t *rtree, uintptr_t key, unsigned level);

bool	rtree_node_valid(rtree_elm_t *node);
rtree_elm_t	*rtree_child_tryread(rtree_elm_t *elm, bool dependent);
rtree_elm_t	*rtree_child_read(tsdn_t *tsdn, rtree_t *rtree, rtree_elm_t *elm,
    unsigned level, bool dependent);
extent_t	*rtree_elm_read(rtree_elm_t *elm, bool dependent);
void	rtree_elm_write(rtree_elm_t *elm, const extent_t *extent);
rtree_elm_t	*rtree_subtree_tryread(rtree_t *rtree, unsigned level,
    bool dependent);
rtree_elm_t	*rtree_subtree_read(tsdn_t *tsdn, rtree_t *rtree,
    unsigned level, bool dependent);
rtree_elm_t	*rtree_elm_lookup(tsdn_t *tsdn, rtree_t *rtree,
    rtree_ctx_t *rtree_ctx, uintptr_t key, bool dependent, bool init_missing);

bool	rtree_write(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, const extent_t *extent);
extent_t	*rtree_read(tsdn_t *tsdn, rtree_t *rtree,
    rtree_ctx_t *rtree_ctx, uintptr_t key, bool dependent);
rtree_elm_t	*rtree_elm_acquire(tsdn_t *tsdn, rtree_t *rtree,
    rtree_ctx_t *rtree_ctx, uintptr_t key, bool dependent, bool init_missing);
extent_t	*rtree_elm_read_acquired(tsdn_t *tsdn, const rtree_t *rtree,
    rtree_elm_t *elm);
void	rtree_elm_write_acquired(tsdn_t *tsdn, const rtree_t *rtree,
    rtree_elm_t *elm, const extent_t *extent);
void	rtree_elm_release(tsdn_t *tsdn, const rtree_t *rtree, rtree_elm_t *elm);
void	rtree_clear(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_RTREE_C_))
JEMALLOC_ALWAYS_INLINE unsigned
rtree_start_level(const rtree_t *rtree, uintptr_t key) {
	unsigned start_level;

	if (unlikely(key == 0)) {
		return rtree->height - 1;
	}

	start_level = rtree->start_level[(lg_floor(key) + 1) >>
	    LG_RTREE_BITS_PER_LEVEL];
	assert(start_level < rtree->height);
	return start_level;
}

JEMALLOC_ALWAYS_INLINE uintptr_t
rtree_subkey(rtree_t *rtree, uintptr_t key, unsigned level) {
	return ((key >> ((ZU(1) << (LG_SIZEOF_PTR+3)) -
	    rtree->levels[level].cumbits)) & ((ZU(1) <<
	    rtree->levels[level].bits) - 1));
}

JEMALLOC_ALWAYS_INLINE bool
rtree_node_valid(rtree_elm_t *node) {
	return ((uintptr_t)node != (uintptr_t)0);
}

JEMALLOC_ALWAYS_INLINE rtree_elm_t *
rtree_child_tryread(rtree_elm_t *elm, bool dependent) {
	rtree_elm_t *child;

	/* Double-checked read (first read may be stale). */
	child = elm->child;
	if (!dependent && !rtree_node_valid(child)) {
		child = (rtree_elm_t *)atomic_read_p(&elm->pun);
	}
	assert(!dependent || child != NULL);
	return child;
}

JEMALLOC_ALWAYS_INLINE rtree_elm_t *
rtree_child_read(tsdn_t *tsdn, rtree_t *rtree, rtree_elm_t *elm, unsigned level,
    bool dependent) {
	rtree_elm_t *child;

	child = rtree_child_tryread(elm, dependent);
	if (!dependent && unlikely(!rtree_node_valid(child))) {
		child = rtree_child_read_hard(tsdn, rtree, elm, level);
	}
	assert(!dependent || child != NULL);
	return child;
}

JEMALLOC_ALWAYS_INLINE extent_t *
rtree_elm_read(rtree_elm_t *elm, bool dependent) {
	extent_t *extent;

	if (dependent) {
		/*
		 * Reading a value on behalf of a pointer to a valid allocation
		 * is guaranteed to be a clean read even without
		 * synchronization, because the rtree update became visible in
		 * memory before the pointer came into existence.
		 */
		extent = elm->extent;
	} else {
		/*
		 * An arbitrary read, e.g. on behalf of ivsalloc(), may not be
		 * dependent on a previous rtree write, which means a stale read
		 * could result if synchronization were omitted here.
		 */
		extent = (extent_t *)atomic_read_p(&elm->pun);
	}

	/* Mask the lock bit. */
	extent = (extent_t *)((uintptr_t)extent & ~((uintptr_t)0x1));

	return extent;
}

JEMALLOC_INLINE void
rtree_elm_write(rtree_elm_t *elm, const extent_t *extent) {
	atomic_write_p(&elm->pun, extent);
}

JEMALLOC_ALWAYS_INLINE rtree_elm_t *
rtree_subtree_tryread(rtree_t *rtree, unsigned level, bool dependent) {
	rtree_elm_t *subtree;

	/* Double-checked read (first read may be stale). */
	subtree = rtree->levels[level].subtree;
	if (!dependent && unlikely(!rtree_node_valid(subtree))) {
		subtree = (rtree_elm_t *)atomic_read_p(
		    &rtree->levels[level].subtree_pun);
	}
	assert(!dependent || subtree != NULL);
	return subtree;
}

JEMALLOC_ALWAYS_INLINE rtree_elm_t *
rtree_subtree_read(tsdn_t *tsdn, rtree_t *rtree, unsigned level,
    bool dependent) {
	rtree_elm_t *subtree;

	subtree = rtree_subtree_tryread(rtree, level, dependent);
	if (!dependent && unlikely(!rtree_node_valid(subtree))) {
		subtree = rtree_subtree_read_hard(tsdn, rtree, level);
	}
	assert(!dependent || subtree != NULL);
	return subtree;
}

JEMALLOC_ALWAYS_INLINE rtree_elm_t *
rtree_elm_lookup(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent, bool init_missing) {
	assert(!dependent || !init_missing);

	if (likely(key != 0)) {
		if (likely(rtree_ctx->cache[0].key == key)) {
			return rtree_ctx->cache[0].elm;
		}
	}

	return rtree_elm_lookup_hard(tsdn, rtree, rtree_ctx, key, dependent,
	    init_missing);
}

JEMALLOC_INLINE bool
rtree_write(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx, uintptr_t key,
    const extent_t *extent) {
	rtree_elm_t *elm;

	assert(extent != NULL); /* Use rtree_clear() for this case. */
	assert(((uintptr_t)extent & (uintptr_t)0x1) == (uintptr_t)0x0);

	elm = rtree_elm_lookup(tsdn, rtree, rtree_ctx, key, false, true);
	if (elm == NULL) {
		return true;
	}
	assert(rtree_elm_read(elm, false) == NULL);
	rtree_elm_write(elm, extent);

	return false;
}

JEMALLOC_ALWAYS_INLINE extent_t *
rtree_read(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx, uintptr_t key,
    bool dependent) {
	rtree_elm_t *elm;

	elm = rtree_elm_lookup(tsdn, rtree, rtree_ctx, key, dependent, false);
	if (!dependent && elm == NULL) {
		return NULL;
	}

	return rtree_elm_read(elm, dependent);
}

JEMALLOC_INLINE rtree_elm_t *
rtree_elm_acquire(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent, bool init_missing) {
	rtree_elm_t *elm;

	elm = rtree_elm_lookup(tsdn, rtree, rtree_ctx, key, dependent,
	    init_missing);
	if (!dependent && elm == NULL) {
		return NULL;
	}

	extent_t *extent;
	void *s;
	do {
		extent = rtree_elm_read(elm, false);
		/* The least significant bit serves as a lock. */
		s = (void *)((uintptr_t)extent | (uintptr_t)0x1);
	} while (atomic_cas_p(&elm->pun, (void *)extent, s));

	if (config_debug) {
		rtree_elm_witness_acquire(tsdn, rtree, key, elm);
	}

	return elm;
}

JEMALLOC_INLINE extent_t *
rtree_elm_read_acquired(tsdn_t *tsdn, const rtree_t *rtree, rtree_elm_t *elm) {
	extent_t *extent;

	assert(((uintptr_t)elm->pun & (uintptr_t)0x1) == (uintptr_t)0x1);
	extent = (extent_t *)((uintptr_t)elm->pun & ~((uintptr_t)0x1));
	assert(((uintptr_t)extent & (uintptr_t)0x1) == (uintptr_t)0x0);

	if (config_debug) {
		rtree_elm_witness_access(tsdn, rtree, elm);
	}

	return extent;
}

JEMALLOC_INLINE void
rtree_elm_write_acquired(tsdn_t *tsdn, const rtree_t *rtree, rtree_elm_t *elm,
    const extent_t *extent) {
	assert(((uintptr_t)extent & (uintptr_t)0x1) == (uintptr_t)0x0);
	assert(((uintptr_t)elm->pun & (uintptr_t)0x1) == (uintptr_t)0x1);

	if (config_debug) {
		rtree_elm_witness_access(tsdn, rtree, elm);
	}

	elm->pun = (void *)((uintptr_t)extent | (uintptr_t)0x1);
	assert(rtree_elm_read_acquired(tsdn, rtree, elm) == extent);
}

JEMALLOC_INLINE void
rtree_elm_release(tsdn_t *tsdn, const rtree_t *rtree, rtree_elm_t *elm) {
	rtree_elm_write(elm, rtree_elm_read_acquired(tsdn, rtree, elm));
	if (config_debug) {
		rtree_elm_witness_release(tsdn, rtree, elm);
	}
}

JEMALLOC_INLINE void
rtree_clear(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key) {
	rtree_elm_t *elm;

	elm = rtree_elm_acquire(tsdn, rtree, rtree_ctx, key, true, false);
	rtree_elm_write_acquired(tsdn, rtree, elm, NULL);
	rtree_elm_release(tsdn, rtree, elm);
}
#endif

#endif /* JEMALLOC_INTERNAL_RTREE_INLINES_H */
