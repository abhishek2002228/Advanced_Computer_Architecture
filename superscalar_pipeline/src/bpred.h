// bpred.h
// Declares the branch predictor class, as well as enums and utility functions
// related to it.

#ifndef _BPRED_H_
#define _BPRED_H_

#include <inttypes.h>

/**
 * The possible branch prediction policies the simulator can use.
 */
typedef enum BPredPolicyEnum
{
    BPRED_PERFECT,      // The branch predictor is (magically) always correct.
    BPRED_ALWAYS_TAKEN, // The branch predictor always predicts a branch taken.
    BPRED_GSHARE,       // The branch predictor uses the Gshare algorithm.
    NUM_BPRED_POLICIES
} BPredPolicy;

/**
 * Whether a branch is taken or not taken.
 */
typedef enum BranchDirectionEnum
{
    NOT_TAKEN = 0, // The branch is not taken.
    TAKEN = 1      // The branch is taken.
} BranchDirection;


/**
 * A branch predictor.
 */
class BPred
{
private:
    /** The policy this branch predictor uses. */
    BPredPolicy policy;

    // This is a 12 bit global history register for Gshare
    uint32_t ghr = 0; // Global History Register
    static const int GHR_BITS = 12; // Number of bits in GHR
    uint8_t pattern_history_table[4096]; // Pattern History Table with 4096 entries    
public:
    /** The total number of branches this branch predictor has seen. */
    uint64_t stat_num_branches;
    /** The number of branches this branch predictor has mispredicted. */
    uint64_t stat_num_mispred;

    /**
 * Construct a branch predictor with the given policy.
 * @param policy the policy this branch predictor should use
 */
    BPred(BPredPolicy policy);

    /**
 * Get a prediction for the branch with the given address.
 * @param pc the address (program counter) of the branch to predict
 * @return the prediction for whether the branch is taken or not taken
 */
    BranchDirection predict(uint64_t pc);

    /**
 * @param pc the address (program counter) of the branch
 * @param prediction the prediction made by the branch predictor
 * @param resolution the actual outcome of the branch
 */
    void update(uint64_t pc, BranchDirection prediction,
                BranchDirection resolution);
};

/**
 * Saturating increment: a utility function to increment a value by 1, stopping
 * at a given maximum value.
 * @param x the value to increment
 * @param max the maximum value to increment to
 * @return the incremented value if x < max, or x otherwise
 */
static inline uint32_t sat_increment(uint32_t x, uint32_t max)
{
    if (x < max)
    {
        return x + 1;
    }
    else
    {
        return x;
    }
}

/**
 * Saturating decrement: a utility function to decrement a value by 1, stopping
 * at 0.
 * @param x the value to decrement
 * @return the decremented value if x > 0, or x otherwise
 */
static inline uint32_t sat_decrement(uint32_t x)
{
    if (x > 0)
    {
        return x - 1;
    }
    else
    {
        return x;
    }
}

#endif
