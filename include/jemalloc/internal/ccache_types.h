#ifndef JEMALLOC_INTERNAL_CCACHE_TYPES_H
#define JEMALLOC_INTERNAL_CCACHE_TYPES_H

#include "jemalloc/internal/jemalloc_preamble.h"

#include "jemalloc/internal/tcache_types.h"
#include "jemalloc/internal/atomic.h"

#ifdef JEMALLOC_CPU_CACHE
  #include <linux/rseq.h>
  typedef struct rseq rseq_t;

  #define CCACHE_TDATA_ZERO_INITIALIZER {0}
#else
  #define CCACHE_TDATA_ZERO_INITIALIZER {}
#endif

#define CCACHE_BIN_ELEMENTS ((PAGE - sizeof(void **)) / sizeof(void *))
#define CCACHE_NCLASSES 16

typedef struct ccache_bin_s ccache_bin_t;
struct ccache_bin_s {
	void *ccache_bin_entry[CCACHE_BIN_ELEMENTS];
	void **head;
};

typedef struct ccache_stats_s ccache_stats_t;
struct ccache_stats_s {
	atomic_u32_t nfills;
	atomic_u32_t nflushes;
};

typedef struct ccache_s ccache_t;
struct ccache_s {
	/* TODO: make ccache_nclasses a runtime option */
	ccache_bin_t bins[CCACHE_NCLASSES];
	ccache_stats_t stats;
};

/*
 * This is some ccache-specific metadata, which is stored in thread local
 * storage rather than per-CPU
 */
typedef struct ccache_tdata_s ccache_tdata_t;
struct ccache_tdata_s {
#ifdef JEMALLOC_CPU_CACHE
	/*
	 * When Ccache serves a request, it still needs to maintain the correct
	 * nrequests arena bin level metric. This data is cached in TSD and
	 * flushed periodically to the arenas, to avoid taking lock for every
	 * request.
	 */
	cache_bin_stats_t ccache_stats[CCACHE_NCLASSES];
	rseq_t rseq_abi;
#endif
};
#endif /* JEMALLOC_INTERNAL_CCACHE_TYPES_H */