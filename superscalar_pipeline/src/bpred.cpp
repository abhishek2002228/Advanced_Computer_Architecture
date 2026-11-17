// Always-taken and 12-bit gshare direction prediction with saturating counters.
// bpred.cpp
// Implements the branch predictor class.

#include "bpred.h"

/**
 * Construct a branch predictor with the given policy.
 * @param policy the policy this branch predictor should use
 */
BPred::BPred(BPredPolicy policy)
{
    
    this->policy = policy;
    stat_num_branches = 0;
    stat_num_mispred = 0;

    for (int i = 0; i < 4096; i++) {
        pattern_history_table[i] = 2; // Initialize to weakly taken
    }
    
}

/**
 * Get a prediction for the branch with the given address.
 * @param pc the address (program counter) of the branch to predict
 * @return the prediction for whether the branch is taken or not taken
 */
BranchDirection BPred::predict(uint64_t pc)
{


    if (this->policy == BPRED_ALWAYS_TAKEN) {
        return TAKEN;
    } else if (this->policy == BPRED_GSHARE) {
        uint32_t index = ((pc & 0xFFF) ^ ghr);
        uint8_t counter = pattern_history_table[index];
        if (counter >= 2) {
            return TAKEN;
        } else {
            return NOT_TAKEN;
        }
    }
    
    return NOT_TAKEN; // default case
}


/**
 * @param pc the address (program counter) of the branch
 * @param prediction the prediction made by the branch predictor
 * @param resolution the actual outcome of the branch
 */
void BPred::update(uint64_t pc, BranchDirection prediction,
                   BranchDirection resolution)
{
    

    // Update branch statistics
    stat_num_branches++;
    int mispred = (prediction != resolution);
    if (mispred) {
        stat_num_mispred++;
    }
    

    //update GHR and Page History Table for Gshare
    if (this->policy == BPRED_GSHARE) {
        uint32_t index = ((pc & 0xFFF) ^ ghr);
        uint8_t counter = pattern_history_table[index];
        if (resolution == TAKEN) {
            pattern_history_table[index] = sat_increment(counter, 3);
        } else {
            pattern_history_table[index] = sat_decrement(counter);
        }

        //ghr = (ghr << 1) | 1;

        if (resolution == TAKEN) {
            ghr = ghr << 1;
            ghr = ghr | 0x1;
            ghr = ghr & 0xFFF; 
        } else {
            ghr = ghr << 1;
            ghr = ghr & 0xFFF; 
        }
    }

    
}
