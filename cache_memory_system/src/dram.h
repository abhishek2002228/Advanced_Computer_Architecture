// dram.h
// Contains declarations of data structures and functions used to implement
// DRAM.

#ifndef __DRAM_H__
#define __DRAM_H__

#include "types.h"


///////////////////////////////////////////////////////////////////////////////
//                              DATA STRUCTURES                              //
///////////////////////////////////////////////////////////////////////////////


/** Possible page policies for DRAM. */
typedef enum DRAMPolicyEnum
{
    OPEN_PAGE = 0,  // The DRAM uses an open-page policy.
    CLOSE_PAGE = 1, // The DRAM uses a close-page policy.
} DRAMPolicy;

typedef struct RowBufEntry {
    bool valid;
    uint64_t row_id;

    RowBufEntry();
} RowBufEntry;

typedef struct DRAMBank {
    RowBufEntry row_buf;
    uint64_t bank_id;

    DRAMBank(uint64_t id);
} DRAMBank;

/** A DRAM module. */
typedef struct DRAM
{
    

    DRAMBank* banks;
    
    unsigned long long stat_read_access;

    
    uint64_t stat_read_delay;

    
    unsigned long long stat_write_access;

    
    uint64_t stat_write_delay;

    DRAMPolicy page_policy;
    DRAM(DRAMPolicy policy);

    ~DRAM();
} DRAM;


///////////////////////////////////////////////////////////////////////////////
//                            FUNCTION PROTOTYPES                            //
///////////////////////////////////////////////////////////////////////////////


/**
 * Allocate and initialize a DRAM module.
 * @return A pointer to the DRAM module.
 */
DRAM *dram_new();

/**
 * Access the DRAM at the given cache line address.
 * Return the delay in cycles incurred by this DRAM access. Also update the
 * DRAM statistics accordingly.
 * Note that the address is given in units of the cache line size!
 * @param dram The DRAM module to access.
 * @param line_addr The address of the cache line to access (in units of the
 *                  cache line size).
 * @param is_dram_write Whether this access writes to DRAM.
 * @return The delay in cycles incurred by this DRAM access.
 */
uint64_t dram_access(DRAM *dram, uint64_t line_addr, bool is_dram_write);

/**
 * Return the delay in cycles incurred by this DRAM access. It is intended that
 * the calling function will be responsible for updating DRAM statistics
 * accordingly.
 * Note that the address is given in units of the cache line size!
 * @param dram The DRAM module to access.
 * @param line_addr The address of the cache line to access (in units of the
 *                  cache line size).
 * @param is_dram_write Whether this access writes to DRAM.
 * @return The delay in cycles incurred by this DRAM access.
 */
uint64_t dram_access_mode_CDEF(DRAM *dram, uint64_t line_addr,
                               bool is_dram_write);

/**
 * Print the statistics of the DRAM module.
 * @param dram The DRAM module to print the statistics of.
 */
void dram_print_stats(DRAM *dram);

#endif // __DRAM_H__
