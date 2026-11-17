// Private first-level caches, shared last-level capacity, and dirty writebacks.
// memsys.cpp
// Defines the functions for the memory system.

#include "memsys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


///////////////////////////////////////////////////////////////////////////////
//                                 CONSTANTS                                 //
///////////////////////////////////////////////////////////////////////////////

/** The number of bytes in a page. */
#define PAGE_SIZE 4096

/** The hit time of the data cache in cycles. */
#define DCACHE_HIT_LATENCY 1

/** The hit time of the instruction cache in cycles. */
#define ICACHE_HIT_LATENCY 1

/** The hit time of the L2 cache in cycles. */
#define L2CACHE_HIT_LATENCY 10

///////////////////////////////////////////////////////////////////////////////
//                    EXTERNALLY DEFINED GLOBAL VARIABLES                    //
///////////////////////////////////////////////////////////////////////////////


extern Mode SIM_MODE;

/** The number of bytes in a cache line. */
extern uint64_t CACHE_LINESIZE;

/** The replacement policy to use for the L1 data and instruction caches. */
extern ReplacementPolicy REPL_POLICY;

/** The size of the data cache in bytes. */
extern uint64_t DCACHE_SIZE;

/** The associativity of the data cache. */
extern uint64_t DCACHE_ASSOC;

/** The size of the instruction cache in bytes. */
extern uint64_t ICACHE_SIZE;

/** The associativity of the instruction cache. */
extern uint64_t ICACHE_ASSOC;

/** The size of the L2 cache in bytes. */
extern uint64_t L2CACHE_SIZE;

/** The associativity of the L2 cache. */
extern uint64_t L2CACHE_ASSOC;

/** The replacement policy to use for the L2 cache. */
extern ReplacementPolicy L2CACHE_REPL;

/** The number of cores being simulated. */
extern unsigned int NUM_CORES;

/**
 * The current clock cycle number.
 * 
 * This can be used as a timestamp for implementing the LRU replacement policy.
 */
extern uint64_t current_cycle;

///////////////////////////////////////////////////////////////////////////////
//                           FUNCTION DEFINITIONS                            //
///////////////////////////////////////////////////////////////////////////////

/**
 * Allocate and initialize the memory system.
 * @return A pointer to the memory system.
 */
MemorySystem *memsys_new()
{
    MemorySystem *sys = (MemorySystem *)calloc(1, sizeof(MemorySystem));

    if (SIM_MODE == SIM_MODE_A)
    {
        sys->dcache = cache_new(DCACHE_SIZE, DCACHE_ASSOC, CACHE_LINESIZE,
                                REPL_POLICY);
    }

    if (SIM_MODE == SIM_MODE_B || SIM_MODE == SIM_MODE_C)
    {
        sys->dcache = cache_new(DCACHE_SIZE, DCACHE_ASSOC, CACHE_LINESIZE,
                                REPL_POLICY);
        sys->icache = cache_new(ICACHE_SIZE, ICACHE_ASSOC, CACHE_LINESIZE,
                                REPL_POLICY);
        sys->l2cache = cache_new(L2CACHE_SIZE, L2CACHE_ASSOC, CACHE_LINESIZE,
                                 REPL_POLICY);
        sys->dram = dram_new();
    }

    if (SIM_MODE == SIM_MODE_DEF)
    {
        bool l2_needs_umonitor = (L2CACHE_REPL == DWP);
        sys->l2cache = cache_new(L2CACHE_SIZE, L2CACHE_ASSOC, CACHE_LINESIZE,
                                 L2CACHE_REPL, l2_needs_umonitor);
        sys->dram = dram_new();
        for (unsigned int i = 0; i < NUM_CORES; i++)
        {
            sys->dcache_coreid[i] = cache_new(DCACHE_SIZE, DCACHE_ASSOC,
                                              CACHE_LINESIZE, REPL_POLICY);
            sys->icache_coreid[i] = cache_new(ICACHE_SIZE, ICACHE_ASSOC,
                                              CACHE_LINESIZE, REPL_POLICY);
        }
    }

    return sys;
}

/**
 * Access the given memory address from an instruction fetch or load/store.
 * Return the delay in cycles incurred by this memory access. Also update the
 * statistics accordingly.
 * @param sys The memory system to use for the access.
 * @param addr The address to access (in bytes).
 * @param type The type of memory access.
 * @param core_id The CPU core ID that requested this access.
 * @return The delay in cycles incurred by this memory access.
 */
uint64_t memsys_access(MemorySystem *sys, uint64_t addr, AccessType type,
                       unsigned int core_id)
{
    uint64_t delay = 0;

    // All cache transactions happen at line granularity, so we convert the
    // byte address to a cache line address.
    uint64_t line_addr = addr / CACHE_LINESIZE;

    if (SIM_MODE == SIM_MODE_A)
    {
        delay = memsys_access_modeA(sys, line_addr, type, core_id);
    }

    if (SIM_MODE == SIM_MODE_B || SIM_MODE == SIM_MODE_C)
    {
        delay = memsys_access_modeBC(sys, line_addr, type, core_id);
    }

    if (SIM_MODE == SIM_MODE_DEF)
    {
        delay = memsys_access_modeDEF(sys, line_addr, type, core_id);
    }

    // Update the statistics.
    if (type == ACCESS_TYPE_IFETCH)
    {
        sys->stat_ifetch_access++;
        sys->stat_ifetch_delay += delay;
    }

    if (type == ACCESS_TYPE_LOAD)
    {
        sys->stat_load_access++;
        sys->stat_load_delay += delay;
    }

    if (type == ACCESS_TYPE_STORE)
    {
        sys->stat_store_access++;
        sys->stat_store_delay += delay;
    }

    return delay;
}

/**
 * In mode A, access the given memory address from a load or store.
 * Return the simulated delay in cycles for this memory access, which is 0
 * because timing is not simulated in this mode.
 * @param sys The memory system to use for the access.
 * @param line_addr The address of the cache line to access (in units of the
 *                  cache line size, i.e., excluding the line offset bits).
 * @param type The type of memory access.
 * @param core_id The CPU core ID that requested this access.
 * @return Always 0 in this mode.
 */
uint64_t memsys_access_modeA(MemorySystem *sys, uint64_t line_addr,
                             AccessType type, unsigned int core_id)
{
    bool needs_dcache_access = false;
    bool is_write = false;

    if (type == ACCESS_TYPE_IFETCH)
    {
        // Ignore instruction fetches because there is no instruction cache in
        // this mode.
    }

    if (type == ACCESS_TYPE_LOAD)
    {
        needs_dcache_access = true;
        is_write = false;
    }

    if (type == ACCESS_TYPE_STORE)
    {
        needs_dcache_access = true;
        is_write = true;
    }

    if (needs_dcache_access)
    {
        CacheResult outcome = cache_access(sys->dcache, line_addr, is_write,
                                           core_id);
        if (outcome == MISS)
        {
            cache_install(sys->dcache, line_addr, is_write, core_id);
        }
    }

    
    return 0;
}

/**
 * In mode B or C, access the given memory address from an instruction fetch or
 * load/store.
 * Return the delay in cycles incurred by this memory access. It is intended
 * that the calling function will be responsible for updating the statistics
 * accordingly.
 * @param sys The memory system to use for the access.
 * @param line_addr The address of the cache line to access (in units of the
 *                  cache line size, i.e., excluding the line offset bits).
 * @param type The type of memory access.
 * @param core_id The CPU core ID that requested this access.
 * @return The delay in cycles incurred by this memory access.
 */
uint64_t memsys_access_modeBC(MemorySystem *sys, uint64_t line_addr,
                              AccessType type, unsigned int core_id)
{
    uint64_t delay = 0;

    if (type == ACCESS_TYPE_IFETCH)
    {
        delay += ICACHE_HIT_LATENCY;
        // L1 instruction cache access
        CacheResult outcome = cache_access(sys->icache, line_addr, false, core_id);
        if (outcome == MISS) {
            // Fetch from L2, then install in L1
            delay += memsys_l2_access(sys, line_addr, false, core_id);
            cache_install(sys->icache, line_addr, false, core_id);
            //icache lines are never dirty, so no writeback needed
        }
    }

    if (type == ACCESS_TYPE_LOAD)
    {
        delay += DCACHE_HIT_LATENCY;
        
        // L1 data cache access (read)
        CacheResult outcome = cache_access(sys->dcache, line_addr, false, core_id);
        if (outcome == MISS) {
            // Fetch from L2, then install in L1
            delay += memsys_l2_access(sys, line_addr, false, core_id);
            cache_install(sys->dcache, line_addr, false, core_id);
            
            // Check if cache_install caused a dirty eviction
            if (sys->dcache->last_evict.last_evicted_line.valid && sys->dcache->last_evict.last_evicted_line.dirty) {
                // The evicted line was from the same set as the new line
                uint64_t evicted_line_addr = sys->dcache->last_evict.last_evicted_line.tag * sys->dcache->num_sets + (line_addr % sys->dcache->num_sets);
                // Writeback the evicted dirty line to L2
                memsys_l2_access(sys, evicted_line_addr, true, core_id);
            }
        }
    }

    if (type == ACCESS_TYPE_STORE)
    {
        delay += DCACHE_HIT_LATENCY;
        
        // L1 data cache access (write)
        CacheResult outcome = cache_access(sys->dcache, line_addr, true, core_id);
        if (outcome == MISS) {
            // Fetch from L2, then install in L1 as dirty
            delay += memsys_l2_access(sys, line_addr, false, core_id);
            cache_install(sys->dcache, line_addr, true, core_id);
            
            // Check if cache_install caused a dirty eviction
            if (sys->dcache->last_evict.last_evicted_line.valid && sys->dcache->last_evict.last_evicted_line.dirty) {
                // The evicted line was from the same set as the new line
                uint64_t evicted_line_addr = sys->dcache->last_evict.last_evicted_line.tag * sys->dcache->num_sets + (line_addr % sys->dcache->num_sets);
                // Writeback the evicted dirty line to L2
                memsys_l2_access(sys, evicted_line_addr, true, core_id);
            }
        }
    }

    return delay;
}

/**
 * Access the given address through the shared L2 cache.
 * Return the delay in cycles incurred by the L2 (and possibly DRAM) access.
 * @param sys The memory system to use for the access.
 * @param line_addr The (physical) address of the cache line to access (in
 *                  units of the cache line size, i.e., excluding the line
 *                  offset bits).
 * @param is_writeback Whether this access is a writeback from an L1 cache.
 * @param core_id The CPU core ID that requested this access.
 * @return The delay in cycles incurred by this access.
 */
uint64_t memsys_l2_access(MemorySystem *sys, uint64_t line_addr,
                          bool is_writeback, unsigned int core_id)
{
    uint64_t delay = L2CACHE_HIT_LATENCY;

    // Perform the L2 cache access
    CacheResult outcome = cache_access(sys->l2cache, line_addr, is_writeback, core_id);

    if (outcome == MISS) {
        // L2 miss - need to access DRAM
        delay += dram_access(sys->dram, line_addr, false);
        cache_install(sys->l2cache, line_addr, is_writeback, core_id);
        
        // Check if cache_install caused a dirty eviction
        if (sys->l2cache->last_evict.last_evicted_line.valid && sys->l2cache->last_evict.last_evicted_line.dirty) {
            // Get evicted line address
            uint64_t evicted_line_addr = sys->l2cache->last_evict.evicted_line_addr;
            
            // Writeback dirty evicted line to DRAM (off critical path for reads)
            dram_access(sys->dram, evicted_line_addr, true);
        }
    }

    return delay;
}

/**
 * In mode D, E, or F, access the given virtual address from an instruction
 * fetch or load/store.
 * Return the delay in cycles incurred by this memory access. It is intended
 * that the calling function will be responsible for updating the statistics
 * accordingly.
 * @param sys The memory system to use for the access.
 * @param v_line_addr The virtual address of the cache line to access (in units
 *                    of the cache line size, i.e., excluding the line offset
 *                    bits).
 * @param type The type of memory access.
 * @param core_id The CPU core ID that requested this access.
 * @return The delay in cycles incurred by this memory access.
 */
uint64_t memsys_access_modeDEF(MemorySystem *sys, uint64_t v_line_addr,
                               AccessType type, unsigned int core_id)
{
    uint64_t delay = 0;
    uint64_t p_line_addr = 0;


    // 

    uint64_t virtual_page = v_line_addr / (PAGE_SIZE / CACHE_LINESIZE);
    uint64_t physical_frame = memsys_convert_vpn_to_pfn(sys, virtual_page, core_id);
    uint64_t page_offset = v_line_addr % (PAGE_SIZE / CACHE_LINESIZE);
    p_line_addr = (physical_frame * (PAGE_SIZE / CACHE_LINESIZE)) + page_offset;

    if (type == ACCESS_TYPE_IFETCH)
    {
        delay += ICACHE_HIT_LATENCY;   
        // private L1 instruction cache
        CacheResult outcome = cache_access(sys->icache_coreid[core_id], p_line_addr, false, core_id);
        if (outcome == MISS) {
            delay += memsys_l2_access(sys, p_line_addr, false, core_id);
            cache_install(sys->icache_coreid[core_id], p_line_addr, false, core_id);
            //icache lines are never dirty, so no writeback needed
        }
    }

    if (type == ACCESS_TYPE_LOAD)
    {
        
        delay += DCACHE_HIT_LATENCY;
        // private L1 data cache
        CacheResult outcome = cache_access(sys->dcache_coreid[core_id], p_line_addr, false, core_id);
        if (outcome == MISS) {
            delay += memsys_l2_access(sys, p_line_addr, false, core_id);
            cache_install(sys->dcache_coreid[core_id], p_line_addr, false, core_id);

            // Check if cache_install caused a dirty eviction  
            if (sys->dcache_coreid[core_id]->last_evict.last_evicted_line.valid && sys->dcache_coreid[core_id]->last_evict.last_evicted_line.dirty) {
                uint64_t evicted_line_addr = sys->dcache_coreid[core_id]->last_evict.last_evicted_line.tag * sys->dcache_coreid[core_id]->num_sets + (p_line_addr % sys->dcache_coreid[core_id]->num_sets);
                // Writeback the evicted dirty line to L2
                memsys_l2_access(sys, evicted_line_addr, true, core_id);
            }
        }
    }

    if (type == ACCESS_TYPE_STORE)
    {
        
        delay += DCACHE_HIT_LATENCY;
        // private L1 data cache
        CacheResult outcome = cache_access(sys->dcache_coreid[core_id], p_line_addr, true, core_id);
        if (outcome == MISS) {
            delay += memsys_l2_access(sys, p_line_addr, false, core_id);
            cache_install(sys->dcache_coreid[core_id], p_line_addr, true, core_id);
            
            // Check if cache_install caused a dirty eviction
            if (sys->dcache_coreid[core_id]->last_evict.last_evicted_line.valid && sys->dcache_coreid[core_id]->last_evict.last_evicted_line.dirty) {
                uint64_t evicted_line_addr = sys->dcache_coreid[core_id]->last_evict.last_evicted_line.tag * sys->dcache_coreid[core_id]->num_sets + (p_line_addr % sys->dcache_coreid[core_id]->num_sets);
                // Writeback the evicted dirty line to L2
                memsys_l2_access(sys, evicted_line_addr, true, core_id);
            }
        }
    }

    return delay;
}

/**
 * Convert the given virtual page number (VPN) to its corresponding physical
 * frame number (PFN; also known as physical page number, or PPN).
 * @param sys The memory system being used.
 * @param vpn The virtual page number to convert.
 * @param core_id The CPU core ID that requested this access.
 * @return The physical frame number corresponding to the given VPN.
 */
uint64_t memsys_convert_vpn_to_pfn(MemorySystem *sys, uint64_t vpn,
                                   unsigned int core_id)
{
    assert(NUM_CORES == 2);
    uint64_t tail = vpn & 0x000fffff;
    uint64_t head = vpn >> 20;
    uint64_t pfn = tail + (core_id << 21) + (head << 21);
    return pfn;
}

/**
 * Print the statistics of the memory system.
 * @param dram The memory system to print the statistics of.
 */
void memsys_print_stats(MemorySystem *sys)
{
    double ifetch_delay_avg = 0;
    double load_delay_avg = 0;
    double store_delay_avg = 0;

    if (sys->stat_ifetch_access)
    {
        ifetch_delay_avg = (double)(sys->stat_ifetch_delay) / (double)(sys->stat_ifetch_access);
    }

    if (sys->stat_load_access)
    {
        load_delay_avg = (double)(sys->stat_load_delay) / (double)(sys->stat_load_access);
    }

    if (sys->stat_store_access)
    {
        store_delay_avg = (double)(sys->stat_store_delay) / (double)(sys->stat_store_access);
    }

    printf("\n");
    printf("MEMSYS_IFETCH_ACCESS   \t\t : %10llu\n", sys->stat_ifetch_access);
    printf("MEMSYS_LOAD_ACCESS     \t\t : %10llu\n", sys->stat_load_access);
    printf("MEMSYS_STORE_ACCESS    \t\t : %10llu\n", sys->stat_store_access);
    printf("MEMSYS_IFETCH_AVGDELAY \t\t : %10.3f\n", ifetch_delay_avg);
    printf("MEMSYS_LOAD_AVGDELAY   \t\t : %10.3f\n", load_delay_avg);
    printf("MEMSYS_STORE_AVGDELAY  \t\t : %10.3f\n", store_delay_avg);

    if (SIM_MODE == SIM_MODE_A)
    {
        cache_print_stats(sys->dcache, "DCACHE");
    }

    if ((SIM_MODE == SIM_MODE_B) || (SIM_MODE == SIM_MODE_C))
    {
        cache_print_stats(sys->icache, "ICACHE");
        cache_print_stats(sys->dcache, "DCACHE");
        cache_print_stats(sys->l2cache, "L2CACHE");
        dram_print_stats(sys->dram);
    }

    if (SIM_MODE == SIM_MODE_DEF)
    {
        assert(NUM_CORES == 2);
        cache_print_stats(sys->icache_coreid[0], "ICACHE_0");
        cache_print_stats(sys->dcache_coreid[0], "DCACHE_0");
        cache_print_stats(sys->icache_coreid[1], "ICACHE_1");
        cache_print_stats(sys->dcache_coreid[1], "DCACHE_1");
        cache_print_stats(sys->l2cache, "L2CACHE");
        dram_print_stats(sys->dram);
    }
}
