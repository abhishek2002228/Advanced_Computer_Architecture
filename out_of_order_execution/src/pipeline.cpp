// Register renaming, ready-instruction scheduling, writeback wakeup, and ordered commit.
// pipeline.cpp
// Implements the out-of-order pipeline.

#include "pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

/**
 * The width of the pipeline; that is, the maximum number of instructions that
 * can be processed during any given cycle in each of the issue, schedule, and
 * commit stages of the pipeline.
 * 
 * (Note that this does not apply to the writeback stage: as many as
 * MAX_WRITEBACKS instructions can be written back to the ROB in a single
 * cycle!)
 * 
 * When the width is 1, the pipeline is scalar.
 * When the width is greater than 1, the pipeline is superscalar.
 */
extern uint32_t PIPE_WIDTH;

/**
 * The number of entries in the ROB; that is, the maximum number of
 * instructions that can be stored in the ROB at any given time.
 */
extern uint32_t NUM_ROB_ENTRIES;

/**
 * Whether to use in-order scheduling or out-of-order scheduling.
 * The possible values are SCHED_IN_ORDER for in-order scheduling and
 * SCHED_OUT_OF_ORDER for out-of-order scheduling.
 */
extern SchedulingPolicy SCHED_POLICY;

/**
 * The number of cycles an LD instruction should take to execute.
 * 
 * This is used by the code in exeq.cpp to determine how long to wait before
 * considering the execution of an LD instruction done.
 */
extern uint32_t LOAD_EXE_CYCLES;

/**
 * Read a single trace record from the trace file and use it to populate the
 * given fe_latch.
 * @param p the pipeline whose trace file should be read
 * @param fe_latch the PipelineLatch struct to populate
 */
void pipe_fetch_inst(Pipeline *p, PipelineLatch *fe_latch)
{
    InstInfo *inst = &fe_latch->inst;
    TraceRec trace_rec;
    uint8_t *trace_rec_buf = (uint8_t *)&trace_rec;
    size_t bytes_read_total = 0;
    ssize_t bytes_read_last = 0;
    size_t bytes_left = sizeof(TraceRec);

    // Read a total of sizeof(TraceRec) bytes from the trace file.
    while (bytes_left > 0)
    {
        bytes_read_last = read(p->trace_fd, trace_rec_buf, bytes_left);
        if (bytes_read_last <= 0)
        {
            // EOF or error
            break;
        }

        trace_rec_buf += bytes_read_last;
        bytes_read_total += bytes_read_last;
        bytes_left -= bytes_read_last;
    }

    // Check for error conditions.
    if (bytes_left > 0 || trace_rec.op_type >= NUM_OP_TYPES)
    {
        fe_latch->valid = false;
        p->halt_inst_num = p->last_inst_num;

        if (p->stat_retired_inst >= p->halt_inst_num)
        {
            p->halt = true;
        }

        if (bytes_read_last == -1)
        {
            fprintf(stderr, "\n");
            perror("Couldn't read from pipe");
            return;
        }

        if (bytes_read_total == 0)
        {
            // No more trace records to read
            return;
        }

        // Too few bytes read or invalid op_type
        fprintf(stderr, "\n");
        fprintf(stderr, "Error: Invalid trace file\n");
        return;
    }

    // Got a valid trace record!
    fe_latch->valid = true;
    fe_latch->stall = false;
    inst->inst_num = ++p->last_inst_num;
    inst->op_type = (OpType)trace_rec.op_type;

    inst->dest_reg = trace_rec.dest_needed ? trace_rec.dest_reg : -1;
    inst->src1_reg = trace_rec.src1_needed ? trace_rec.src1_reg : -1;
    inst->src2_reg = trace_rec.src2_needed ? trace_rec.src2_reg : -1;

    inst->dr_tag = -1;
    inst->src1_tag = -1;
    inst->src2_tag = -1;
    inst->src1_ready = false;
    inst->src2_ready = false;
    inst->exe_wait_cycles = 0;
}

/**
 * Allocate and initialize a new pipeline.
 * @param trace_fd the file descriptor from which to read trace records
 * @return a pointer to a newly allocated pipeline
 */
Pipeline *pipe_init(int trace_fd)
{
    printf("\n** PIPELINE IS %d WIDE **\n\n", PIPE_WIDTH);

    // Allocate pipeline.
    Pipeline *p = (Pipeline *)calloc(1, sizeof(Pipeline));

    // Initialize pipeline.
    p->rat = rat_init();
    p->rob = rob_init();
    p->exeq = exeq_init();
    p->trace_fd = trace_fd;
    p->halt_inst_num = (uint64_t)(-1) - 3;

    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        p->FE_latch[i].valid = false;
        p->ID_latch[i].valid = false;
        p->SC_latch[i].valid = false;
    }
    for (unsigned int i = 0; i < MAX_WRITEBACKS; i++)
    {
        p->EX_latch[i].valid = false;
    }

    return p;
}

/**
 * Commit the given instruction.
 * This updates counters and flags on the pipeline.
 * @param p the pipeline to update.
 * @param inst the instruction to commit.
 */
void pipe_commit_inst(Pipeline *p, InstInfo inst)
{
    p->stat_retired_inst++;

    if (inst.inst_num >= p->halt_inst_num)
    {
        p->halt = true;
    }
}

/**
 * Print out the state of the pipeline for debugging purposes.
 * @param p the pipeline
 */
void pipe_print_state(Pipeline *p)
{
    printf("\n");
    // Print table header
    for (unsigned int latch_type = 0; latch_type < 4; latch_type++)
    {
        switch (latch_type)
        {
        case 0:
            printf(" FE:    ");
            break;
        case 1:
            printf(" ID:    ");
            break;
        case 2:
            printf(" SCH:   ");
            break;
        case 3:
            printf(" EX:    ");
            break;
        default:
            printf(" ------ ");
        }
    }
    printf("\n");

    // Print row for each lane in pipeline width
    unsigned int ex_i = 0;
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (p->FE_latch[i].valid)
        {
            printf(" %6lu ",
                   (unsigned long)p->FE_latch[i].inst.inst_num);
        }
        else
        {
            printf(" ------ ");
        }
        if (p->ID_latch[i].valid)
        {
            printf(" %6lu ",
                   (unsigned long)p->ID_latch[i].inst.inst_num);
        }
        else
        {
            printf(" ------ ");
        }
        if (p->SC_latch[i].valid)
        {
            printf(" %6lu ",
                   (unsigned long)p->SC_latch[i].inst.inst_num);
        }
        else
        {
            printf(" ------ ");
        }
        bool flag = false;
        for (; ex_i < MAX_WRITEBACKS; ex_i++)
        {
            if (p->EX_latch[ex_i].valid)
            {
                printf(" %6lu ",
                       (unsigned long)p->EX_latch[ex_i].inst.inst_num);
                ex_i++;
                flag = true;
                break;
            }
        }
        if (!flag) {
            printf(" ------ ");
        }
        printf("\n");
    }
    printf("\n");

    rat_print_state(p->rat);
    exeq_print_state(p->exeq);
    rob_print_state(p->rob);
}

/**
 * Simulate one cycle of all stages of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle(Pipeline *p)
{
    p->stat_num_cycle++;

    #ifdef DEBUG
        printf("\n--------------------------------------------\n");
        printf("Cycle count: %lu, retired instructions: %lu\n\n",
           (unsigned long)p->stat_num_cycle,
           (unsigned long)p->stat_retired_inst);
    #endif
    
    // In our simulator, stages are processed in reverse order.
    pipe_cycle_commit(p);
    pipe_cycle_writeback(p);
    pipe_cycle_exe(p);
    pipe_cycle_schedule(p);
    pipe_cycle_issue(p);
    pipe_cycle_decode(p);
    pipe_cycle_fetch(p);

    // Compile with "make debug" to have this show!
    #ifdef DEBUG
        pipe_print_state(p);
    #endif
}

/**
 * Simulate one cycle of the fetch stage of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_fetch(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (!p->FE_latch[i].stall && !p->FE_latch[i].valid)
        {
            // No stall and latch empty, so fetch a new instruction.
            pipe_fetch_inst(p, &p->FE_latch[i]);
        }
    }
}

/**
 * Simulate one cycle of the instruction decode stage of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_decode(Pipeline *p)
{
    static uint64_t next_inst_num = 1;
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (!p->ID_latch[i].stall && !p->ID_latch[i].valid)
        {
            // No stall and latch empty, so decode the next instruction.
            // Loop to find the next in-order instruction.
            for (unsigned int j = 0; j < PIPE_WIDTH; j++)
            {
                if (p->FE_latch[j].valid &&
                    p->FE_latch[j].inst.inst_num == next_inst_num)
                {
                    p->ID_latch[i] = p->FE_latch[j];
                    p->FE_latch[j].valid = false;
                    next_inst_num++;
                    break;
                }
            }
        }
    }
}

/**
 * Simulate one cycle of the execute stage of a pipeline. This handles
 * instructions that take multiple cycles to execute.
 * @param p the pipeline to simulate
 */
void pipe_cycle_exe(Pipeline *p)
{
    // If all operations are single-cycle, just copy SC latches to EX latches.
    if (LOAD_EXE_CYCLES == 1)
    {
        for (unsigned int i = 0; i < PIPE_WIDTH; i++)
        {
            if (p->SC_latch[i].valid)
            {
                p->EX_latch[i] = p->SC_latch[i];
                p->SC_latch[i].valid = false;
            }
        }
        return;
    }

    // Otherwise, we need to handle multi-cycle instructions with EXEQ.

    // All valid entries from the SC latches are inserted into the EXEQ.
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (p->SC_latch[i].valid)
        {
            if (!exeq_insert(p->exeq, p->SC_latch[i].inst))
            {
                fprintf(stderr, "Error: EXEQ full\n");
                p->halt = true;
                return;
            }

            p->SC_latch[i].valid = false;
        }
    }

    // Cycle the EXEQ to reduce wait time for each instruction by 1 cycle.
    exeq_cycle(p->exeq);

    // Transfer all finished entries from the EXEQ to the EX latch.
    for (unsigned int i = 0; i < MAX_WRITEBACKS && exeq_check_done(p->exeq); i++)
    {
        p->EX_latch[i].valid = true;
        p->EX_latch[i].stall = false;
        p->EX_latch[i].inst = exeq_remove(p->exeq);
    }
}

/**
 * Simulate one cycle of the issue stage of a pipeline: insert decoded
 * instructions into the ROB and perform register renaming.
 * @param p the pipeline to simulate
 */
void pipe_cycle_issue(Pipeline *p)
{


    // For each instruction in ID latch, check if ROB has space, perform register renaming, then insert
    // Support partial issue: issue as many instructions as possible until ROB is full
    // When ROB is full, stall remaining instructions and propagate stall upstream
    
    // Sort valid instructions in ID_latch by inst_num to ensure program order
    // Create array of indices sorted by inst_num  
    int sorted_indices[MAX_PIPE_WIDTH];
    int valid_count = 0;
    
    // Collect valid instruction indices
    for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
        if (p->ID_latch[i].valid) {
            sorted_indices[valid_count] = i;
            valid_count++;
        }
    }
    
    // Sort indices by inst_num (simple bubble sort for small arrays)
    for (int i = 0; i < valid_count - 1; i++) {
        for (int j = 0; j < valid_count - 1 - i; j++) {
            if (p->ID_latch[sorted_indices[j]].inst.inst_num > 
                p->ID_latch[sorted_indices[j + 1]].inst.inst_num) {
                // Swap indices
                int temp = sorted_indices[j];
                sorted_indices[j] = sorted_indices[j + 1];
                sorted_indices[j + 1] = temp;
            }
        }
    }
    
    // Process instructions in program order (sorted by inst_num)
    for (int idx = 0; idx < valid_count; idx++) {
        unsigned int i = sorted_indices[idx];
        if (p->ID_latch[i].valid) {
            if (!rob_check_space(p->rob)) {
                // ROB is full - stall this instruction and all subsequent ones
                p->ID_latch[i].stall = true;
                // Continue loop to stall remaining instructions too
                continue;
            }
            
            // ROB has space - clear any previous stall and proceed with issue
            p->ID_latch[i].stall = false;

            // Perform register renaming on the instruction in the ID latch
            // Handle source register 1 renaming
            if (p->ID_latch[i].inst.src1_reg != -1) {
                int src1_tag = rat_get_remap(p->rat, p->ID_latch[i].inst.src1_reg);
                if (src1_tag == -1) {
                    // src1 not remapped, operand is ready in register file
                    p->ID_latch[i].inst.src1_tag = -1;
                    p->ID_latch[i].inst.src1_ready = true;
                } else {
                    // src1 is remapped, check if producer is ready
                    p->ID_latch[i].inst.src1_tag = src1_tag;
                    p->ID_latch[i].inst.src1_ready = rob_check_ready(p->rob, src1_tag);
                }
            } else {
                // No src1 needed
                p->ID_latch[i].inst.src1_tag = -1;
                p->ID_latch[i].inst.src1_ready = true;
            }
            
            // Handle source register 2 renaming
            if (p->ID_latch[i].inst.src2_reg != -1) {
                int src2_tag = rat_get_remap(p->rat, p->ID_latch[i].inst.src2_reg);
                if (src2_tag == -1) {
                    // src2 not remapped, operand is ready in register file
                    p->ID_latch[i].inst.src2_tag = -1;
                    p->ID_latch[i].inst.src2_ready = true;
                } else {
                    // src2 is remapped, check if producer is ready
                    p->ID_latch[i].inst.src2_tag = src2_tag;
                    p->ID_latch[i].inst.src2_ready = rob_check_ready(p->rob, src2_tag);
                }
            } else {
                // No src2 needed
                p->ID_latch[i].inst.src2_tag = -1;
                p->ID_latch[i].inst.src2_ready = true;
            }
            
            // Insert the renamed instruction into ROB (moves from ID_latch to ROB)
            int rob_index = rob_insert(p->rob, p->ID_latch[i].inst);
            assert(rob_index != -1);  // Should not fail since we checked space
            
            // Set the destination register tag (ROB entry index)
            p->rob->entries[rob_index].inst.dr_tag = rob_index;
            
            // Update RAT if this instruction has a destination register
            if (p->ID_latch[i].inst.dest_reg != -1) {
                rat_set_remap(p->rat, p->ID_latch[i].inst.dest_reg, rob_index);
            }
            
            // Invalidate the ID latch entry after successful issue
            p->ID_latch[i].valid = false;
        } else {
            // No valid instruction in this slot - clear stall if set
            p->ID_latch[i].stall = false;
        }
    }
    
    // Clear stall flag for any remaining invalid instruction slots
    for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
        if (!p->ID_latch[i].valid) {
            p->ID_latch[i].stall = false;
        }
    }
}

/**
 * Simulate one cycle of the scheduling stage of a pipeline: schedule
 * instructions to execute if they are ready.
 * @param p the pipeline to simulate
 */
void pipe_cycle_schedule(Pipeline *p)
{
    

    if (SCHED_POLICY == SCHED_IN_ORDER)
    {
        

        // schedule if ROB is not empty 
        bool rob_empty = (p->rob->head_ptr == p->rob->tail_ptr) && !(p->rob->entries[p->rob->head_ptr].valid);
        if (rob_empty) {
            // ROB is empty - nothing to schedule
            // invalidate all SC latches
            for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
                p->SC_latch[i].valid = false;
                p->SC_latch[i].stall = false;
            }
            //stop scheduling instructions
            return;
        }
        for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
            // find the oldest valid entry in the ROB that is not already executing
            // iterate from head to tail of ROB.
            bool found = false;
            
            // Iterate through ROB entries, handling full ROB case
            uint32_t entries_checked = 0;
            for (int j = p->rob->head_ptr; entries_checked < NUM_ROB_ENTRIES; j = (j + 1) % NUM_ROB_ENTRIES) {
                entries_checked++;
                
                if (p->rob->entries[j].valid && !p->rob->entries[j].exec) {
                    // found a valid entry that is not executing
                    // check if it is stalled
                    if (!p->rob->entries[j].inst.src1_ready || !p->rob->entries[j].inst.src2_ready) {
                        // at least one source operand is not ready - stop scheduling instructions
                        // In-order scheduling: cannot skip over unready instructions
                        return;
                    }
                    // instruction is ready to execute - mark it as executing in the ROB
                    rob_mark_exec(p->rob, p->rob->entries[j].inst);
                    // send it to the next latch (SC latch)
                    p->SC_latch[i].valid = true;
                    p->SC_latch[i].stall = false;
                    p->SC_latch[i].inst = p->rob->entries[j].inst;
                    found = true;
                    break;  // move to next lane of pipeline
                }
                
                // Stop when we've wrapped around to tail (for full ROB case)
                if (j == p->rob->tail_ptr && entries_checked > 1) {
                    break;
                }
            }
            if (!found) {
                // no suitable instruction found - invalidate SC latch
                p->SC_latch[i].valid = false;
                p->SC_latch[i].stall = false;
            }
        }

    }

    if (SCHED_POLICY == SCHED_OUT_OF_ORDER)
    {
        

        // schedule if ROB is not empty 
        bool rob_empty = (p->rob->head_ptr == p->rob->tail_ptr) && !(p->rob->entries[p->rob->head_ptr].valid);
        if (rob_empty) {
            // ROB is empty - nothing to schedule
            // invalidate all SC latches
            for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
                p->SC_latch[i].valid = false;
                p->SC_latch[i].stall = false;
            }
        }

        for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
            // find the oldest valid entry in the ROB that has both source operands ready and is not already executing
            // iterate from head to tail of ROB. 
            bool found = false;
            
            // Iterate through ROB entries, handling full ROB case
            uint32_t entries_checked = 0;
            for (int j = p->rob->head_ptr; entries_checked < NUM_ROB_ENTRIES; j = (j + 1) % NUM_ROB_ENTRIES) {
                entries_checked++;
                
                if (p->rob->entries[j].valid && !p->rob->entries[j].exec &&
                    p->rob->entries[j].inst.src1_ready && p->rob->entries[j].inst.src2_ready) {
                    // found a valid entry that is not executing and has both source operands ready
                    // mark it as executing in the ROB
                    rob_mark_exec(p->rob, p->rob->entries[j].inst);
                    // send it to the next latch (SC latch)
                    p->SC_latch[i].valid = true;
                    p->SC_latch[i].stall = false;
                    p->SC_latch[i].inst = p->rob->entries[j].inst;
                    found = true;
                    break;  // move to next lane of pipeline
                }
                
                // Stop when we've wrapped around to tail (for full ROB case)
                if (j == p->rob->tail_ptr && entries_checked > 1) {
                    break;
                }
            }
            if (!found) {
                // no suitable instruction found - invalidate SC latch
                p->SC_latch[i].valid = false;
                p->SC_latch[i].stall = false;
            }
        }
    }
}

/**
 * Simulate one cycle of the writeback stage of a pipeline: update the ROB
 * with information from instructions that have finished executing.
 * @param p the pipeline to simulate
 */
void pipe_cycle_writeback(Pipeline *p)
{
    

    // Remember: how many instructions can the EX stage send to the WB stage
    // in one cycle?

    for (unsigned int i = 0; i < MAX_WRITEBACKS; i++) {
        if (p->EX_latch[i].valid) {
            // Broadcast result to all ROB entries
            int tag = p->EX_latch[i].inst.dr_tag;
            if (tag != -1) {
                // Instruction has a destination register - update all ROB entries waiting for this tag
                rob_wakeup(p->rob, tag);
            }


            // Mark the instruction ready to commit in the ROB
            rob_mark_ready(p->rob, p->EX_latch[i].inst);
            // Invalidate the EX latch entry after writeback
            p->EX_latch[i].valid = false;
        }
    }
}

/**
 * Simulate one cycle of the commit stage of a pipeline: commit instructions
 * in the ROB that are ready to commit.
 * @param p the pipeline to simulate
 */
void pipe_cycle_commit(Pipeline *p)
{


    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        //rob_remove_head automaticcally checks if head is valid and ready
        InstInfo inst = rob_remove_head(p->rob);
        //if InstInfo has -1 op_id, it is invalid - did not commit
        if (inst.op_type != static_cast<OpType>(-1)) 
        {
            // 1. Update pipeline statistics and check for termination
            pipe_commit_inst(p, inst);
            
            // 2. Update RAT if this instruction wrote to a register
            if (inst.dest_reg != -1) {
                int current_mapping = rat_get_remap(p->rat, inst.dest_reg);
                if (current_mapping == inst.dr_tag) {
                    // This instruction is still the latest producer of this register
                    // Clear the mapping - register value now committed to ARF
                    rat_reset_entry(p->rat, inst.dest_reg);
                }
                // If current_mapping != inst.dr_tag, a newer instruction has 
                // already remapped this register, so we don't reset the RAT
            }
        } else {
            // No more instructions to commit
            break;
        }
    }
}
