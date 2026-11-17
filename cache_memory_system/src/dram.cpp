// Fixed-latency memory and banked open-page row-buffer timing.
// dram.cpp
// Defines the functions used to implement DRAM.

#include "dram.h"
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <assert.h>


///////////////////////////////////////////////////////////////////////////////
//                                 CONSTANTS                                 //
///////////////////////////////////////////////////////////////////////////////


#define DELAY_SIM_MODE_B 100

/** The DRAM activation latency (ACT), in cycles. (Also known as RAS.) */
#define DELAY_ACT 45

/** The DRAM column selection latency (CAS), in cycles. */
#define DELAY_CAS 45

/** The DRAM precharge latency (PRE), in cycles. */
#define DELAY_PRE 45

/**
 * The DRAM bus latency, in cycles.
 */
#define DELAY_BUS 10

/** The row buffer size, in bytes. */
#define ROW_BUFFER_SIZE 1024

/** The number of banks in the DRAM module. */
#define NUM_BANKS 16

///////////////////////////////////////////////////////////////////////////////
//                    EXTERNALLY DEFINED GLOBAL VARIABLES                    //
///////////////////////////////////////////////////////////////////////////////


extern Mode SIM_MODE;

/** The number of bytes in a cache line. */
extern uint64_t CACHE_LINESIZE;

/** Which page policy the DRAM should use. */
extern DRAMPolicy DRAM_PAGE_POLICY;

///////////////////////////////////////////////////////////////////////////////
//                           FUNCTION DEFINITIONS                            //
///////////////////////////////////////////////////////////////////////////////


/**
 * Allocate and initialize a DRAM module.
 * @return A pointer to the DRAM module.
 */

RowBufEntry::RowBufEntry() : valid(false), row_id(0) {}
DRAMBank::DRAMBank(uint64_t id) : bank_id(id) {};

DRAM::DRAM(DRAMPolicy policy) : stat_read_access(0), stat_read_delay(0), stat_write_access(0), stat_write_delay(0), page_policy(policy) {
    banks = static_cast<DRAMBank*>(calloc(NUM_BANKS, sizeof(DRAMBank)));

    for (uint64_t ii = 0; ii < NUM_BANKS; ii++) {
        new (&banks[ii]) DRAMBank(ii);
    }
};

DRAM::~DRAM()
{
    free(banks);
}

DRAM *dram_new()
{
    DRAM* dram = new DRAM(DRAM_PAGE_POLICY);
    return dram;
}

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
uint64_t dram_access(DRAM *dram, uint64_t line_addr, bool is_dram_write)
{
    

    if (SIM_MODE == SIM_MODE_A) {
        assert(false);
        //should not reach here
    }
    uint64_t dram_access_delay = DELAY_SIM_MODE_B;

    if ((SIM_MODE == SIM_MODE_C || SIM_MODE == SIM_MODE_DEF))
    {
        if (dram->page_policy == OPEN_PAGE) {
            dram_access_delay = dram_access_mode_CDEF(dram, line_addr, is_dram_write);
        }
    }

    if (!is_dram_write)
    {
        dram->stat_read_access++;
        dram->stat_read_delay += dram_access_delay;
    }
    else
    {
        dram->stat_write_access++;
        dram->stat_write_delay += dram_access_delay;
    }
    return dram_access_delay;
}

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
                               bool is_dram_write)
{
    
    uint64_t delay = DELAY_BUS;

    // number of cache lines per row
    uint64_t lines_per_row = ROW_BUFFER_SIZE / CACHE_LINESIZE;

    // line addr is {row_id, line_in_row, byte_in_line}
    uint64_t row_id = (line_addr / lines_per_row);

    // for row interleaving, consecutive rows map to consecutive banks
    // we use the bottom bits of the row_id to select the bank
    uint64_t bank_id = row_id % NUM_BANKS;
    row_id /= NUM_BANKS;

    // based on page policy 
    if (dram->page_policy == DRAMPolicyEnum::OPEN_PAGE) {
        // Open-page policy
        if (dram->banks[bank_id].row_buf.valid && dram->banks[bank_id].row_buf.row_id == row_id) {
            // Row hit
            delay += DELAY_CAS;
        } else if (!dram->banks[bank_id].row_buf.valid) {
            // Row conflict
            delay +=  DELAY_ACT + DELAY_CAS;
            dram->banks[bank_id].row_buf.row_id = row_id;
            dram->banks[bank_id].row_buf.valid = true;

        } else {
            // Row buffer empty
            delay += DELAY_PRE + DELAY_ACT + DELAY_CAS;
            dram->banks[bank_id].row_buf.valid = true;
            dram->banks[bank_id].row_buf.row_id = row_id;
        }
    } else {
        // Close-page policy: close the row after each access
        assert(false);
        delay += DELAY_ACT + DELAY_CAS;
    }

    return delay;
}

/**
 * Print the statistics of the DRAM module.
 * @param dram The DRAM module to print the statistics of.
 */
void dram_print_stats(DRAM *dram)
{
    double avg_read_delay = 0.0;
    double avg_write_delay = 0.0;

    if (dram->stat_read_access)
    {
        avg_read_delay = (double)(dram->stat_read_delay) /
                         (double)(dram->stat_read_access);
    }

    if (dram->stat_write_access)
    {
        avg_write_delay = (double)(dram->stat_write_delay) /
                          (double)(dram->stat_write_access);
    }

    printf("\n");
    printf("DRAM_READ_ACCESS     \t\t : %10llu\n", dram->stat_read_access);
    printf("DRAM_WRITE_ACCESS    \t\t : %10llu\n", dram->stat_write_access);
    printf("DRAM_READ_DELAY_AVG  \t\t : %10.3f\n", avg_read_delay);
    printf("DRAM_WRITE_DELAY_AVG \t\t : %10.3f\n", avg_write_delay);
}
