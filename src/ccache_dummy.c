#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/ccache.h"

szind_t ccache_maxind = 0;
szind_t ccache_minind = 0;
size_t ccache_maxclass = 0;

static inline void
assert_unused() {
	assert(!config_cpu_cache);
	not_reached();
}

bool
ccache_init(tsdn_t *tsdn, base_t *base) {
	assert_unused();
	return false;
}

void*
ccache_alloc(tsd_t *tsd, arena_t *arena,
    size_t size, szind_t ind, bool zero, bool small) {
	assert_unused();
	return NULL;
}

bool
ccache_free(tsd_t *tsd, void *ptr, szind_t ind, bool small) {
	assert_unused();
	return true;
}

uint32_t
ccache_nflushes_get() {
	assert_unused();
	return 0;
}
uint32_t
ccache_nfills_get() {
	assert_unused();
	return 0;
}

void
tsd_ccache_init(tsd_t *tsd) {
	assert_unused();
}
void ccache_merge_tstats(tsdn_t *tsdn) {
	assert_unused();
}

/* Non thread-safe functions, use only without contention, e.g. in tests */
void
ccache_full_flush_unsafe() {
	assert_unused();
}

bool
ccache_is_empty_unsafe() {
	assert_unused();
	return false;
}

int
ccache_ncached_elements_unsafe() {
	assert_unused();
	return 0;
}