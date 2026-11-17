// cache.h
// Contains declarations of data structures and functions used to implement a
// cache.

#ifndef __CACHE_H__
#define __CACHE_H__

#include "types.h"
#include <vector>


extern unsigned int NUM_CORES;
///////////////////////////////////////////////////////////////////////////////
//                                 CONSTANTS                                 //
///////////////////////////////////////////////////////////////////////////////

/**
 * The maximum allowed number of ways in a cache set.
 * 
 * At runtime, the actual number of ways in each cache set is guaranteed to be
 * less than or equal to this value.
 */
#define MAX_WAYS_PER_CACHE_SET 16

///////////////////////////////////////////////////////////////////////////////
//                              DATA STRUCTURES                              //
///////////////////////////////////////////////////////////////////////////////
/** Whether a cache access is a hit or a miss. */

typedef enum CacheResultEnum
{
    HIT = 1,  // The access hit the cache.
    MISS = 0, // The access missed the cache.
} CacheResult;

/** Possible replacement policies for the cache. */
typedef enum ReplacementPolicyEnum
{
    LRU = 0,    // Evict the least recently used line.
    RANDOM = 1, // Evict a random line.

    
    SWP = 2,

    
    DWP = 3,
} ReplacementPolicy;


struct UtilityMonitor {
    // Cumulative hit estimates indexed by capacity in ways.
    uint64_t num_ways;
    std::vector<uint64_t> hits_per_size;
    uint64_t total_accesses;

    void record_hit (int recency_pos);
    void record_miss ();
    std::vector<double> compute_utilities();
    void decay_counters();

    UtilityMonitor(uint64_t num_ways);
    UtilityMonitor();
} ;

typedef struct Line {
    bool valid;
    bool dirty;
    uint64_t tag;
    uint64_t lru_timestamp;
    unsigned int core_id;

    //constructor
    Line();
} Line;

typedef struct CacheEvict {
    Line last_evicted_line;
    bool last_access_was_dirty_evict;
    uint64_t evicted_line_addr;
} CacheEvict;

typedef struct Set {
    Line* entries;
    //constructor
    Set(uint64_t assoc);
    //destructor
    ~Set();
} Set;

/** A single cache module. */
typedef struct Cache
{
    
    Set* sets;
    uint64_t num_ways;
    uint64_t line_size;
    uint64_t num_sets;
    ReplacementPolicy replacement_policy;
    CacheEvict last_evict;
    bool needs_umonitor;
    UtilityMonitor util_monitor[2];
    int ways_allocated[2];
    const uint64_t UMON_INTERVAL = 1000000; // needs tuning
    // --- Global per-core occupancy counters (updated on every fill/evict) ---
    uint64_t global_occupancy[2];


    Cache(uint64_t num_ways, uint64_t line_size, uint64_t num_sets, ReplacementPolicy replacement_policy, bool needs_umonitor);

    ~Cache();
    

    unsigned long long stat_read_access;

    
    unsigned long long stat_read_miss;

    
    unsigned long long stat_write_access;

    
    unsigned long long stat_write_miss;

    
    unsigned long long stat_dirty_evicts;

    unsigned long long stat_evicts;

    CacheResult cache_access(uint64_t set_index, uint64_t tag, bool is_write, unsigned int core_id);
    void cache_install(uint64_t set_index, uint64_t tag, bool is_write, unsigned int core_id);
    uint64_t cache_find_victim(uint64_t set_index, unsigned int core_id, uint64_t tag);
    void recompute_partitions();

} Cache;

///////////////////////////////////////////////////////////////////////////////
//                            FUNCTION PROTOTYPES                            //
///////////////////////////////////////////////////////////////////////////////


/**
 * Allocate and initialize a cache.
 * @param size The size of the cache in bytes.
 * @param associativity The associativity of the cache.
 * @param line_size The size of a cache line in bytes.
 * @param replacement_policy The replacement policy of the cache.
 * @return A pointer to the cache.
 */
Cache *cache_new(uint64_t size, uint64_t associativity, uint64_t line_size,
                 ReplacementPolicy replacement_policy, bool needs_umonitor = false);

/**
 * Access the cache at the given address.
 * Also update the cache statistics accordingly.
 * @param c The cache to access.
 * @param line_addr The address of the cache line to access (in units of the
 *                  cache line size, i.e., excluding the line offset bits).
 * @param is_write Whether this access is a write.
 * @param core_id The CPU core ID that requested this access.
 * @return Whether the cache access was a hit or a miss.
 */
CacheResult cache_access(Cache *c, uint64_t line_addr, bool is_write,
                         unsigned int core_id);

/**
 * Install the cache line with the given address.
 * Also update the cache statistics accordingly.
 * @param c The cache to install the line into.
 * @param line_addr The address of the cache line to install (in units of the
 *                  cache line size, i.e., excluding the line offset bits).
 * @param is_write Whether this install is triggered by a write.
 * @param core_id The CPU core ID that requested this access.
 */
void cache_install(Cache *c, uint64_t line_addr, bool is_write,
                   unsigned int core_id);

/**
 * Find which way in a given cache set to replace when a new cache line needs
 * to be installed. This should be chosen according to the cache's replacement
 * policy.
 * The returned victim can be valid (non-empty), in which case the calling
 * function is responsible for evicting the cache line from that victim way.
 * @param c The cache to search.
 * @param set_index The index of the cache set to search.
 * @param core_id The CPU core ID that requested this access.
 * @return The index of the victim way.
 */
unsigned int cache_find_victim(Cache *c, unsigned int set_index,
                               unsigned int core_id);

/**
 * Print the statistics of the given cache.
 * @param c The cache to print the statistics of.
 * @param label A label for the cache, which is used as a prefix for each
 *              statistic.
 */
void cache_print_stats(Cache *c, const char *label);

#endif // __CACHE_H__
