#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/emap.h"

emap_t emap_global;

enum emap_lock_result_e {
	emap_lock_result_success,
	emap_lock_result_failure,
	emap_lock_result_no_extent
};
typedef enum emap_lock_result_e emap_lock_result_t;

bool
emap_init(emap_t *emap) {
	bool err;
	err = rtree_new(&emap->rtree, true);
	if (err) {
		return true;
	}
	err = mutex_pool_init(&emap->mtx_pool, "emap_mutex_pool",
	    WITNESS_RANK_EMAP);
	if (err) {
		return true;
	}
	return false;
}

void
emap_lock_edata(tsdn_t *tsdn, emap_t *emap, edata_t *edata) {
	assert(edata != NULL);
	mutex_pool_lock(tsdn, &emap->mtx_pool, (uintptr_t)edata);
}

void
emap_unlock_edata(tsdn_t *tsdn, emap_t *emap, edata_t *edata) {
	assert(edata != NULL);
	mutex_pool_unlock(tsdn, &emap->mtx_pool, (uintptr_t)edata);
}

void
emap_lock_edata2(tsdn_t *tsdn, emap_t *emap, edata_t *edata1,
    edata_t *edata2) {
	assert(edata1 != NULL && edata2 != NULL);
	mutex_pool_lock2(tsdn, &emap->mtx_pool, (uintptr_t)edata1,
	    (uintptr_t)edata2);
}

void
emap_unlock_edata2(tsdn_t *tsdn, emap_t *emap, edata_t *edata1,
    edata_t *edata2) {
	assert(edata1 != NULL && edata2 != NULL);
	mutex_pool_unlock2(tsdn, &emap->mtx_pool, (uintptr_t)edata1,
	    (uintptr_t)edata2);
}

static inline emap_lock_result_t
emap_try_lock_rtree_leaf_elm(tsdn_t *tsdn, emap_t *emap, rtree_leaf_elm_t *elm,
    edata_t **result, bool inactive_only) {
	edata_t *edata1 = rtree_leaf_elm_edata_read(tsdn, &emap->rtree,
	    elm, true);

	/* Slab implies active extents and should be skipped. */
	if (edata1 == NULL || (inactive_only && rtree_leaf_elm_slab_read(tsdn,
	    &emap->rtree, elm, true))) {
		return emap_lock_result_no_extent;
	}

	/*
	 * It's possible that the extent changed out from under us, and with it
	 * the leaf->edata mapping.  We have to recheck while holding the lock.
	 */
	emap_lock_edata(tsdn, emap, edata1);
	edata_t *edata2 = rtree_leaf_elm_edata_read(tsdn, &emap->rtree, elm,
	    true);

	if (edata1 == edata2) {
		*result = edata1;
		return emap_lock_result_success;
	} else {
		emap_unlock_edata(tsdn, emap, edata1);
		return emap_lock_result_failure;
	}
}

/*
 * Returns a pool-locked edata_t * if there's one associated with the given
 * address, and NULL otherwise.
 */
edata_t *
emap_lock_edata_from_addr(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    void *addr, bool inactive_only) {
	edata_t *ret = NULL;
	rtree_leaf_elm_t *elm = rtree_leaf_elm_lookup(tsdn, &emap->rtree,
	    rtree_ctx, (uintptr_t)addr, false, false);
	if (elm == NULL) {
		return NULL;
	}
	emap_lock_result_t lock_result;
	do {
		lock_result = emap_try_lock_rtree_leaf_elm(tsdn, emap, elm,
		    &ret, inactive_only);
	} while (lock_result == emap_lock_result_failure);
	return ret;
}

bool
emap_rtree_leaf_elms_lookup(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    const edata_t *edata, bool dependent, bool init_missing,
    rtree_leaf_elm_t **r_elm_a, rtree_leaf_elm_t **r_elm_b) {
	*r_elm_a = rtree_leaf_elm_lookup(tsdn, &emap->rtree, rtree_ctx,
	    (uintptr_t)edata_base_get(edata), dependent, init_missing);
	if (!dependent && *r_elm_a == NULL) {
		return true;
	}
	assert(*r_elm_a != NULL);

	*r_elm_b = rtree_leaf_elm_lookup(tsdn, &emap->rtree, rtree_ctx,
	    (uintptr_t)edata_last_get(edata), dependent, init_missing);
	if (!dependent && *r_elm_b == NULL) {
		return true;
	}
	assert(*r_elm_b != NULL);

	return false;
}

void
emap_rtree_write_acquired(tsdn_t *tsdn, emap_t *emap, rtree_leaf_elm_t *elm_a,
    rtree_leaf_elm_t *elm_b, edata_t *edata, szind_t szind, bool slab) {
	rtree_leaf_elm_write(tsdn, &emap->rtree, elm_a, edata, szind, slab);
	if (elm_b != NULL) {
		rtree_leaf_elm_write(tsdn, &emap->rtree, elm_b, edata, szind,
		    slab);
	}
}

bool
emap_register_boundary(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    edata_t *edata, szind_t szind, bool slab) {
	rtree_leaf_elm_t *elm_a, *elm_b;
	bool err = emap_rtree_leaf_elms_lookup(tsdn, emap, rtree_ctx, edata,
	    false, true, &elm_a, &elm_b);
	if (err) {
		return true;
	}
	emap_rtree_write_acquired(tsdn, emap, elm_a, elm_b, edata, szind, slab);
	return false;
}

void
emap_register_interior(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    edata_t *edata, szind_t szind) {
	assert(edata_slab_get(edata));

	/* Register interior. */
	for (size_t i = 1; i < (edata_size_get(edata) >> LG_PAGE) - 1; i++) {
		rtree_write(tsdn, &emap->rtree, rtree_ctx,
		    (uintptr_t)edata_base_get(edata) + (uintptr_t)(i <<
		    LG_PAGE), edata, szind, true);
	}
}

void
emap_deregister_boundary(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    edata_t *edata) {
	rtree_leaf_elm_t *elm_a, *elm_b;

	emap_rtree_leaf_elms_lookup(tsdn, emap, rtree_ctx, edata,
	    true, false, &elm_a, &elm_b);
	emap_rtree_write_acquired(tsdn, emap, elm_a, elm_b, NULL, SC_NSIZES,
	    false);
}

void
emap_deregister_interior(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    edata_t *edata) {
	assert(edata_slab_get(edata));
	for (size_t i = 1; i < (edata_size_get(edata) >> LG_PAGE) - 1; i++) {
		rtree_clear(tsdn, &emap->rtree, rtree_ctx,
		    (uintptr_t)edata_base_get(edata) + (uintptr_t)(i <<
		    LG_PAGE));
	}
}

bool
emap_split_prepare(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    emap_split_prepare_t *split_prepare, edata_t *edata, size_t size_a,
    szind_t szind_a, bool slab_a, edata_t *trail, size_t size_b,
    szind_t szind_b, bool slab_b) {
	/*
	 * Note that while the trail mostly inherits its attributes from the
	 * extent to be split, it maintains its own arena ind -- this allows
	 * cross-arena edata interactions, such as occur in the range ecache.
	 */
	edata_init(trail, edata_arena_ind_get(trail),
	    (void *)((uintptr_t)edata_base_get(edata) + size_a), size_b,
	    slab_b, szind_b, edata_sn_get(edata), edata_state_get(edata),
	    edata_zeroed_get(edata), edata_committed_get(edata),
	    edata_dumpable_get(edata), EXTENT_NOT_HEAD);

	/*
	 * We use incorrect constants for things like arena ind, zero, dump, and
	 * commit state, and head status.  This is a fake edata_t, used to
	 * facilitate a lookup.
	 */
	edata_t lead;
	edata_init(&lead, 0U, edata_addr_get(edata), size_a, slab_a, szind_a, 0,
	    extent_state_active, false, false, false, EXTENT_NOT_HEAD);

	emap_rtree_leaf_elms_lookup(tsdn, emap, rtree_ctx, &lead, false, true,
	    &split_prepare->lead_elm_a, &split_prepare->lead_elm_b);
	emap_rtree_leaf_elms_lookup(tsdn, emap, rtree_ctx, trail, false, true,
	    &split_prepare->trail_elm_a, &split_prepare->trail_elm_b);

	if (split_prepare->lead_elm_a == NULL
	    || split_prepare->lead_elm_b == NULL
	    || split_prepare->trail_elm_a == NULL
	    || split_prepare->trail_elm_b == NULL) {
		return true;
	}
	return false;
}

void
emap_split_commit(tsdn_t *tsdn, emap_t *emap,
    emap_split_prepare_t *split_prepare, edata_t *lead, size_t size_a,
    szind_t szind_a, bool slab_a, edata_t *trail, size_t size_b,
    szind_t szind_b, bool slab_b) {
	edata_size_set(lead, size_a);
	edata_szind_set(lead, szind_a);

	emap_rtree_write_acquired(tsdn, emap, split_prepare->lead_elm_a,
	    split_prepare->lead_elm_b, lead, szind_a, slab_a);
	emap_rtree_write_acquired(tsdn, emap, split_prepare->trail_elm_a,
	    split_prepare->trail_elm_b, trail, szind_b, slab_b);
}

void
emap_merge_prepare(tsdn_t *tsdn, emap_t *emap, rtree_ctx_t *rtree_ctx,
    emap_split_prepare_t *split_prepare, edata_t *lead, edata_t *trail) {
	emap_rtree_leaf_elms_lookup(tsdn, emap, rtree_ctx, lead, true, false,
	    &split_prepare->lead_elm_a, &split_prepare->lead_elm_b);
	emap_rtree_leaf_elms_lookup(tsdn, emap, rtree_ctx, trail, true, false,
	    &split_prepare->trail_elm_a, &split_prepare->trail_elm_b);
}

void
emap_merge_commit(tsdn_t *tsdn, emap_t *emap,
    emap_split_prepare_t *split_prepare, edata_t *lead, edata_t *trail) {
	if (split_prepare->lead_elm_b != NULL) {
		rtree_leaf_elm_write(tsdn, &emap->rtree,
		    split_prepare->lead_elm_b, NULL, SC_NSIZES, false);
	}

	rtree_leaf_elm_t *merged_b;
	if (split_prepare->trail_elm_b != NULL) {
		rtree_leaf_elm_write(tsdn, &emap->rtree,
		    split_prepare->trail_elm_a, NULL, SC_NSIZES, false);
		merged_b = split_prepare->trail_elm_b;
	} else {
		merged_b = split_prepare->trail_elm_a;
	}

	edata_size_set(lead, edata_size_get(lead) + edata_size_get(trail));
	edata_szind_set(lead, SC_NSIZES);
	edata_sn_set(lead, (edata_sn_get(lead) < edata_sn_get(trail)) ?
	    edata_sn_get(lead) : edata_sn_get(trail));
	edata_zeroed_set(lead, edata_zeroed_get(lead)
	    && edata_zeroed_get(trail));

	emap_rtree_write_acquired(tsdn, emap, split_prepare->lead_elm_a,
	    merged_b, lead, SC_NSIZES, false);
}
