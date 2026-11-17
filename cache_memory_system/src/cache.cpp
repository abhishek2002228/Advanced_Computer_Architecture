// Set-associative caches with LRU, random, static, and utility-guided replacement.
// cache.cpp
// Defines the functions used to implement the cache.

#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <iostream>


///////////////////////////////////////////////////////////////////////////////
//                    EXTERNALLY DEFINED GLOBAL VARIABLES                    //
///////////////////////////////////////////////////////////////////////////////

extern unsigned int NUM_CORES;
/**
 * The current clock cycle number.
 * 
 * This can be used as a timestamp for implementing the LRU replacement policy.
 */
extern uint64_t current_cycle;

/**
 * For static way partitioning, the quota of ways in each set that can be
 * assigned to core 0.
 * The remaining number of ways is the quota for core 1.
 */
extern unsigned int SWP_CORE0_WAYS;

///////////////////////////////////////////////////////////////////////////////
//                           FUNCTION DEFINITIONS                            //
///////////////////////////////////////////////////////////////////////////////


/**
 * Allocate and initialize a cache.
 * @param size The size of the cache in bytes.
 * @param associativity The associativity of the cache.
 * @param line_size The size of a cache line in bytes.
 * @param replacement_policy The replacement policy of the cache.
 * @return A pointer to the cache.
 */

UtilityMonitor::UtilityMonitor(uint64_t num_ways) : num_ways(num_ways), total_accesses(0) {
    hits_per_size.resize(num_ways, 0);
}

UtilityMonitor::UtilityMonitor() : num_ways(0), total_accesses(0) {
}

// Accumulate hits for every capacity larger than the observed stack position.
void UtilityMonitor::record_hit(int recency_pos) {
    // If hit at stack position k, all caches of size > k would have hit
    for (int i = recency_pos; i < num_ways; i++) {
        hits_per_size[i]++;
    }
    total_accesses++;
}
void UtilityMonitor::record_miss() {
    total_accesses++;
}

std::vector<double> UtilityMonitor::compute_utilities() {
    std::vector<double> utilities(num_ways, 0.0);
    for (int i = 0; i < num_ways; i++) {
        utilities[i] = static_cast<double>(hits_per_size[i]);
    }
    return utilities;
}

// Start a fresh measurement window.
void UtilityMonitor::decay_counters() {
    std::fill(hits_per_size.begin(), hits_per_size.end(), 0);
    total_accesses = 0;
}

void Cache::recompute_partitions() {
    auto util0 = util_monitor[0].compute_utilities();
    auto util1 = util_monitor[1].compute_utilities();

    unsigned int alloc0 = 0, alloc1 = 0;
    
    for (unsigned int way = 0; way < num_ways; way++) {
        double gain0 = (alloc0 < num_ways) ? 
            (alloc0 == 0 ? util0[0] : util0[alloc0] - util0[alloc0 - 1]) : 0.0;
        double gain1 = (alloc1 < num_ways) ? 
            (alloc1 == 0 ? util1[0] : util1[alloc1] - util1[alloc1 - 1]) : 0.0;

        if (gain0 >= gain1) {
            alloc0++;
        } else {
            alloc1++;
        }
    }

    ways_allocated[0] = alloc0;
    ways_allocated[1] = alloc1;
}

Line::Line() : valid(false), dirty(false), tag(0), lru_timestamp(0), core_id(0){};

Set::Set(uint64_t assoc) {
    entries = new Line[assoc];
}

Set::~Set() {
    delete[] entries;
}

Cache::Cache(uint64_t num_ways, uint64_t line_size, uint64_t num_sets, ReplacementPolicy replacement_policy, bool needs_umonitor) : num_ways(num_ways), line_size(line_size), num_sets(num_sets), replacement_policy(replacement_policy), needs_umonitor(needs_umonitor) {
    // Assertions to validate constructor parameters
    assert(num_ways > 0);
    assert(line_size > 0);
    assert(num_sets > 0);
    assert(num_ways <= MAX_WAYS_PER_CACHE_SET);
    
    sets = static_cast<Set*>(malloc(sizeof(Set) * num_sets));
    assert(sets != nullptr);  // Check malloc succeeded

    stat_read_access = 0;
    stat_write_access = 0;
    stat_read_miss = 0;
    stat_write_miss = 0;
    stat_evicts = 0;
    stat_dirty_evicts = 0;

    for (uint64_t ii = 0; ii < num_sets; ii++) {
        new (&sets[ii]) Set(num_ways);
    }

    if (needs_umonitor) {
        for (unsigned int i = 0; i < NUM_CORES; i++) {
            util_monitor[i] = UtilityMonitor();
            util_monitor[i].num_ways = num_ways;
            util_monitor[i].hits_per_size.resize(num_ways, 0);
        }
        ways_allocated[0] =  num_ways/2;
        ways_allocated[1] = num_ways - ways_allocated[0];
        global_occupancy[0] = 0;
        global_occupancy[1] = 0;
    }
}

Cache::~Cache() {
    free(sets);
}

CacheResult Cache::cache_access(uint64_t set_index, uint64_t tag, bool is_write, unsigned int core_id) {
    // Assertions to validate function parameters
    assert(set_index < num_sets);
    assert(core_id >= 0);
    bool found = false;
    for (uint64_t ii = 0; ii < num_ways; ii++) {
        if (sets[set_index].entries[ii].valid) {
            if (sets[set_index].entries[ii].tag == tag) {
                found = true;
                if (is_write) {
                    sets[set_index].entries[ii].dirty = true;  // Set dirty on write
                }
                //if found and umon is enabled
                if (needs_umonitor) {
                    uint64_t hit_time = sets[set_index].entries[ii].lru_timestamp;
                    //find recency position
                int recency_pos = 0;
                for (uint64_t jj = 0; jj < num_ways; jj++) {  
                    if (jj != ii &&  
                        sets[set_index].entries[jj].valid && 
                        sets[set_index].entries[jj].lru_timestamp > hit_time) {
                        recency_pos++;
                    }
                }
                    util_monitor[core_id].record_hit(recency_pos);
                }
                sets[set_index].entries[ii].lru_timestamp = current_cycle;
                last_evict.last_access_was_dirty_evict = false;
                break;  // Found the line, exit loop early
            }
        }
    }

    if (needs_umonitor && !found) {
        util_monitor[core_id].record_miss();
    }
    // if (!found) {
    //     std::cout << "Core " << core_id << " cache miss for tag " << tag << " in set " << set_index << std::endl;
    // }
    if (is_write) {
        stat_write_access++;
        if (!found) {
            stat_write_miss++;
        }
    }
    else {
        stat_read_access++;
        if (!found) {
            stat_read_miss++;
        }
    }

    CacheResult result = found ? HIT : MISS;

    // Every UMON interval, recompute partitions and decay counters
    if (needs_umonitor && (((util_monitor[0].total_accesses + util_monitor[1].total_accesses) % (UMON_INTERVAL)) == 0)) {
        recompute_partitions();
        std::cout << "Recomputed partitions: Core0=" << ways_allocated[0]
                  << " Core1=" << ways_allocated[1] << std::endl;
        util_monitor[0].decay_counters();
        util_monitor[1].decay_counters();
    }

    return result;
}


uint64_t Cache::cache_find_victim(uint64_t set_index, unsigned int core_id, uint64_t tag) {
    Set& eviction_set = sets[set_index];

    if (replacement_policy == DWP) {
        // First look for invalid lines
        for (uint64_t ii = 0; ii < num_ways; ii++) {
            if (!eviction_set.entries[ii].valid) {
                return ii;
            }
        }
        
        // Calculate target based on global allocation
        uint64_t target_core0_ways = ways_allocated[0] * num_sets;
        
        // Determine which core should give up a line based on GLOBAL occupancy
        unsigned int core_to_evict;
        if (global_occupancy[0] < target_core0_ways) {
            core_to_evict = 1;
        } else if (global_occupancy[0] > target_core0_ways) {
            core_to_evict = 0;
        } else {
            core_to_evict = core_id;
        }
        
        // Find LRU victim among lines owned by core_to_evict
        uint64_t victim_way = 0;
        uint64_t oldest_timestamp = UINT64_MAX;
        bool found_victim = false;
        
        for (uint64_t ii = 0; ii < num_ways; ii++) {
            if (eviction_set.entries[ii].valid && 
                eviction_set.entries[ii].core_id == core_to_evict &&
                eviction_set.entries[ii].lru_timestamp < oldest_timestamp) {
                oldest_timestamp = eviction_set.entries[ii].lru_timestamp;
                victim_way = ii;
                found_victim = true;
            }
        }
        
        // Fallback: if preferred core has no lines in THIS set
        if (!found_victim) {
            unsigned int fallback_core = (core_to_evict == 0) ? 1 : 0;
            for (uint64_t ii = 0; ii < num_ways; ii++) {
                if (eviction_set.entries[ii].valid && 
                    eviction_set.entries[ii].core_id == fallback_core &&
                    eviction_set.entries[ii].lru_timestamp < oldest_timestamp) {
                    oldest_timestamp = eviction_set.entries[ii].lru_timestamp;
                    victim_way = ii;
                    found_victim = true;
                }
            }
        }
        
        assert(found_victim);
        return victim_way;
    }
    if (replacement_policy == SWP) {
        // First look for invalid lines
        for (uint64_t ii = 0; ii < num_ways; ii++) {
            if (!eviction_set.entries[ii].valid) {
                return ii;
            }
        }
        
        // Count how many lines core 0 currently has in this set
        uint64_t num_core_0 = 0;
        for (uint64_t ii = 0; ii < num_ways; ii++) {
            if (eviction_set.entries[ii].core_id == 0) {
                num_core_0++;
            }
        }
        
        // Determine which core should give up a line
        unsigned int core_to_evict;
        if (num_core_0 < SWP_CORE0_WAYS) {
            // Core 0 is under quota, evict from core 1
            core_to_evict = 1;
        } else if (num_core_0 == SWP_CORE0_WAYS) {
            // Core 0 is at quota, evict from requesting core
            core_to_evict = core_id;
        } else {
            // Core 0 is over quota, evict from core 0
            core_to_evict = 0;
        }
        
        // Find LRU victim among lines owned by core_to_evict
        uint64_t victim_way = 0;
        uint64_t oldest_timestamp = UINT64_MAX;
        bool found_victim = false;
        
        for (uint64_t ii = 0; ii < num_ways; ii++) {
            if (eviction_set.entries[ii].valid && 
                eviction_set.entries[ii].core_id == core_to_evict &&
                eviction_set.entries[ii].lru_timestamp < oldest_timestamp) {
                oldest_timestamp = eviction_set.entries[ii].lru_timestamp;
                victim_way = ii;
                found_victim = true;
            }
        }
        
        assert(found_victim); // Should always find a victim
        return victim_way;
    }
    
    // For non-WP, look for empty slot first across all ways
    for (u_int64_t ii = 0; ii < num_ways; ii++) {
        if (!eviction_set.entries[ii].valid) {
            return ii;
        }
    }

    // No non-valid entry is present, use replacement policy
    if (replacement_policy == LRU) {
        uint64_t LRU_index = 0;
        uint64_t LRU_timestamp = eviction_set.entries[0].lru_timestamp;
        for (uint64_t ii = 1; ii < num_ways; ii++) {
            if (eviction_set.entries[ii].lru_timestamp < LRU_timestamp) {
                LRU_timestamp = eviction_set.entries[ii].lru_timestamp;
                LRU_index = ii;
            }
        }
        return LRU_index;
    }
    else if (replacement_policy == RANDOM) {
        // Find random index in [0, num_ways - 1]
        return rand() % num_ways;
    }
    else {
        assert(false);  // This should never be reached
    }
}


void Cache::cache_install(uint64_t set_index, uint64_t tag, bool is_write, unsigned int core_id) {
    // Assertions to validate parameters
    assert(set_index < num_sets);
    assert(core_id >= 0);


    uint64_t eviction_index = Cache::cache_find_victim(set_index, core_id, tag);
    bool eviction = sets[set_index].entries[eviction_index].valid;
    if (eviction) {
        if (replacement_policy == DWP) {
            // Update global occupancy counters
            global_occupancy[sets[set_index].entries[eviction_index].core_id]--;
        }
        last_evict.last_evicted_line = sets[set_index].entries[eviction_index];
        last_evict.evicted_line_addr = last_evict.last_evicted_line.tag * num_sets + set_index;
        stat_evicts++;
        if (last_evict.last_evicted_line.dirty) {
            stat_dirty_evicts++;
            last_evict.last_access_was_dirty_evict = true;
        } else {
            last_evict.last_access_was_dirty_evict = false;
        }
    } else {
        // No eviction occurred - reset the eviction info
        last_evict.last_evicted_line.valid = false;
        last_evict.last_access_was_dirty_evict = false;
    }

    sets[set_index].entries[eviction_index].valid = true;
    sets[set_index].entries[eviction_index].tag = tag;
    sets[set_index].entries[eviction_index].dirty = is_write;
    sets[set_index].entries[eviction_index].lru_timestamp = current_cycle;
    sets[set_index].entries[eviction_index].core_id = core_id;
    if (replacement_policy == DWP) {
        // Update global occupancy counters
        global_occupancy[core_id]++;
    }
}

Cache *cache_new(uint64_t size, uint64_t associativity, uint64_t line_size,
                 ReplacementPolicy replacement_policy, bool needs_umonitor)
{
    
    //calculate set size from given params
    uint64_t num_sets = size/(associativity * line_size);
    Cache* c = new Cache(associativity, line_size, num_sets, replacement_policy, needs_umonitor);

    return c;
}

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
                         unsigned int core_id)
{
    

    //calculate set index from line address
    uint64_t set_index = line_addr % c->num_sets;
    uint64_t tag = line_addr / c->num_sets;
    return c->cache_access(set_index, tag, is_write, core_id);
}

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
                   unsigned int core_id)
{
    
    uint64_t set_index = line_addr % c->num_sets;
    uint64_t tag = line_addr / c->num_sets;
    c->cache_install(set_index, tag, is_write, core_id);
}

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


/**
 * Print the statistics of the given cache.
 * @param c The cache to print the statistics of.
 * @param label A label for the cache, which is used as a prefix for each
 *              statistic.
 */
void cache_print_stats(Cache *c, const char *header)
{
    double read_miss_percent = 0.0;
    double write_miss_percent = 0.0;

    if (c->stat_read_access)
    {
        read_miss_percent = 100.0 * (double)(c->stat_read_miss) /
                            (double)(c->stat_read_access);
    }

    if (c->stat_write_access)
    {
        write_miss_percent = 100.0 * (double)(c->stat_write_miss) /
                             (double)(c->stat_write_access);
    }

    printf("\n");
    printf("%s_READ_ACCESS     \t\t : %10llu\n", header, c->stat_read_access);
    printf("%s_WRITE_ACCESS    \t\t : %10llu\n", header, c->stat_write_access);
    printf("%s_READ_MISS       \t\t : %10llu\n", header, c->stat_read_miss);
    printf("%s_WRITE_MISS      \t\t : %10llu\n", header, c->stat_write_miss);
    printf("%s_READ_MISS_PERC  \t\t : %10.3f\n", header, read_miss_percent);
    printf("%s_WRITE_MISS_PERC \t\t : %10.3f\n", header, write_miss_percent);
    printf("%s_DIRTY_EVICTS    \t\t : %10llu\n", header, c->stat_dirty_evicts);
}
