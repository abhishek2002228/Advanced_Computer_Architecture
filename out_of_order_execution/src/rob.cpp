// Circular reorder buffer with validity, execution, and completion tracking.
// rob.cpp
// Implements the re-order buffer.

#include "rob.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/**
 * The number of entries in the ROB; that is, the maximum number of
 * instructions that can be stored in the ROB at any given time.
 */
extern uint32_t NUM_ROB_ENTRIES;

/**
 * Allocate and initialize a new ROB.
 * @return a pointer to a newly allocated ROB
 */
ROB *rob_init()
{
    ROB *rob = (ROB *)calloc(1, sizeof(ROB));

    rob->head_ptr = 0;
    rob->tail_ptr = 0;

    for (int i = 0; i < MAX_ROB_ENTRIES; i++)
    {
        rob->entries[i].valid = false;
        rob->entries[i].ready = false;
        rob->entries[i].exec = false;
    }

    return rob;
}

/**
 * Print out the state of the ROB for debugging purposes.
 * @param rob the ROB
 */
void rob_print_state(ROB *rob)
{
    printf("Current ROB state:\n");
    printf("Entry\t\tInst\tValid\tExec\tReady\tsrc1_reg\tsrc1_ready\tsrc1_tag\tsrc2_reg\tsrc2_ready\tsrc2_tag\tdest_reg\tdr_tag\n");
    for (unsigned int i = 0; i < NUM_ROB_ENTRIES; i++)
    {
        printf("%5d ::  %5d", i, (int)rob->entries[i].inst.inst_num);
        printf(" %5d", rob->entries[i].valid);
        printf(" %7d", rob->entries[i].exec);
        printf(" %7d", rob->entries[i].ready);
        printf(" %8d", rob->entries[i].inst.src1_reg);
        printf(" %10d", rob->entries[i].inst.src1_ready);
        printf(" %12d", rob->entries[i].inst.src1_tag);
        printf(" %11d", rob->entries[i].inst.src2_reg);
        printf(" %10d", rob->entries[i].inst.src2_ready);
        printf(" %12d", rob->entries[i].inst.src2_tag);
        printf(" %11d", rob->entries[i].inst.dest_reg);
        printf(" %10d", rob->entries[i].inst.dr_tag);
        if (i == (unsigned int) rob->head_ptr && i == (unsigned int) rob->tail_ptr) {
            printf(" (head/tail)");
        } else if (i == (unsigned int) rob->head_ptr) {
            printf(" (head)");
        } else if (i == (unsigned int) rob->tail_ptr) {
            printf(" (tail)");
        } 
        printf("\n");
    }
    printf("\n");
}

/**
 * Check if there is space available to insert another instruction into the
 * ROB.
 * @param rob the ROB
 * @return true if the ROB has space for another instruction, false otherwise
 */
bool rob_check_space(ROB *rob)
{
    

    // The tail ptr always point to the next available entry.
    // Since we use valid bits, we just check if the current tail position is available
    return !rob->entries[rob->tail_ptr].valid;
}

/**
 * Insert an instruction into the ROB at the tail pointer.
 * @param rob the ROB
 * @param inst the instruction to insert
 * @return the ID (index) of the newly inserted instruction in the ROB, or -1
 *         if there is no more space in the ROB
 */
int rob_insert(ROB *rob, InstInfo inst)
{
    

    // check if ROB has space
    if (!rob_check_space(rob)) {
        return -1; // No space available
    }
    
    // Insert at current tail position
    int index = rob->tail_ptr;
    rob->entries[index].inst = inst;
    rob->entries[index].valid = true;
    rob->entries[index].exec = false;
    rob->entries[index].ready = false;
    
    // Advance tail pointer, wrapping around if needed
    rob->tail_ptr = (rob->tail_ptr + 1) % NUM_ROB_ENTRIES;
    
    return index; // Return the index where instruction was inserted
}

/**
 * Find the given instruction in the ROB and mark it as executing.
 * @param rob the ROB
 * @param inst the instruction that is now executing
 */
void rob_mark_exec(ROB *rob, InstInfo inst)
{
    

    // The dr tag will be the renamed destination register tag. 
    // Because we use the ROB to rename registers, the dr_tag is the index of the ROB entry
    int index = inst.dr_tag;


    #ifdef DEBUG
        // Assert that the index is valid 
        // This to just add a sanity check
        assert(index >= 0 && index < static_cast<int>(NUM_ROB_ENTRIES) && rob->entries[index].valid);
    #endif

    rob->entries[index].exec = true;
}

/**
 * Find the given instruction in the ROB and mark it as having its output
 * ready (i.e., being ready to commit).
 * @param rob the ROB
 * @param inst the instruction whose output is ready
 */
void rob_mark_ready(ROB *rob, InstInfo inst)
{
    

    int index = inst.dr_tag;
    #ifdef DEBUG
        // Assert that the index is valid 
        // This to just add a sanity check
        assert(index >= 0 && index < static_cast<int>(NUM_ROB_ENTRIES) && rob->entries[index].valid);
    #endif
    rob->entries[index].ready = true;
}


/**
 * Check if the instruction with the given tag (ID/index) has its output ready.
 * This is the same as asking if that instruction is ready to commit.
 * @param rob the ROB
 * @param tag the tag (ID/index) of the instruction to check
 * @return true if the instruction has its output ready, false if it is not or
 *         if there is no valid instruction at this tag in the ROB
 */
bool rob_check_ready(ROB *rob, int tag)
{
    
    if (tag < 0 || tag >= static_cast<int>(NUM_ROB_ENTRIES)) {
        return false; // Invalid tag
    }
    return rob->entries[tag].valid && rob->entries[tag].ready;
}

/**
 * Check if the instruction at the head of the ROB is ready to commit.
 * @param rob the ROB
 * @return true if the instruction at the head of the ROB is ready to commit,
 *         false if it is not or if there is no valid instruction at the head
 *         of the ROB
 */
bool rob_check_head(ROB *rob)
{
    
    int head_index = rob->head_ptr;

    #ifdef DEBUG
        // Assert that the head index is valid 
        // This to just add a sanity check
        assert(head_index >= 0 && head_index < static_cast<int>(NUM_ROB_ENTRIES));
    #endif

    return rob->entries[head_index].valid && rob->entries[head_index].ready;
}

/**
 * Wake up instructions that are dependent on the instruction with the given
 * tag.
 * Then, for each source operand for each valid instruction in the ROB, if the
 * tag of the source operand matches the given tag, this function should mark
 * that source operand as ready.
 * @param rob the ROB
 * @param tag the tag of the instruction that has finished executing
 */
void rob_wakeup(ROB *rob, int tag)
{
    

    // When an instruction has finished executing, 
    // check all instructios in the ROB that are valid and have matching src1 or src2 tags
    // This is the ROB broadcasting the result to all dependent instructions

    #ifdef DEBUG
        // Assert that the tag is valid 
        // This to just add a sanity check
        assert(tag >= 0 && tag < static_cast<int>(NUM_ROB_ENTRIES));
    #endif

    for (int i = 0; i < static_cast<int>(NUM_ROB_ENTRIES); i++) {
        if (rob->entries[i].valid) {
            if (rob->entries[i].inst.src1_tag == tag) {
                rob->entries[i].inst.src1_ready = true;
            }
            if (rob->entries[i].inst.src2_tag == tag) {
                rob->entries[i].inst.src2_ready = true;
            }
        }
    }
}

/**
 * If the head entry of the ROB is ready to commit, remove that entry and
 * return the instruction contained there.
 * @param rob the ROB
 * @return the instruction that was previously at the head of the ROB
 */
InstInfo rob_remove_head(ROB *rob)
{
    
    
    // Check if head is valid and ready
    if (!rob_check_head(rob)) {
        // Return invalid instruction with invalid optypes. 
        InstInfo invalid_inst;
        invalid_inst.inst_num = UINT64_MAX;
        invalid_inst.op_type = static_cast<OpType>(-1);   // Invalid op type
        invalid_inst.dest_reg = -1;
        invalid_inst.src1_reg = -1;
        invalid_inst.src2_reg = -1;
        invalid_inst.dr_tag = -1;
        invalid_inst.src1_tag = -1;
        invalid_inst.src2_tag = -1;
        invalid_inst.src1_ready = false;
        invalid_inst.src2_ready = false;
        invalid_inst.exe_wait_cycles = 0;
        return invalid_inst;
    }
    
    // Head is ready to commit
    InstInfo inst = rob->entries[rob->head_ptr].inst;
    
    // Invalidate the entry
    rob->entries[rob->head_ptr].valid = false;
    rob->entries[rob->head_ptr].ready = false;
    rob->entries[rob->head_ptr].exec = false;
    
    // Advance head pointer
    rob->head_ptr = (rob->head_ptr + 1) % NUM_ROB_ENTRIES;
    
    return inst;
}