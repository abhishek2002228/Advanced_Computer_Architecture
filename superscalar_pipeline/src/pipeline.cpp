// Five-stage timing model with dependence stalls and selectable forwarding paths.
// pipeline.cpp
// Implements functions to simulate a pipelined processor.

#include "pipeline.h"
#include <cstdlib>
#include <stdio.h>
#include <unistd.h>
#include <vector>
/**
 * Read a single trace record from the trace file and use it to populate the
 * given fetch_op.
 * @param p the pipeline whose trace file should be read
 * @param fetch_op the PipelineLatch struct to populate
 */
void pipe_get_fetch_op(Pipeline *p, PipelineLatch *fetch_op)
{
    TraceRec *trace_rec = &fetch_op->trace_rec;
    uint8_t *trace_rec_buf = (uint8_t *)trace_rec;
    size_t bytes_read_total = 0;
    ssize_t bytes_read_last = 0;
    size_t bytes_left = sizeof(*trace_rec);

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
    if (bytes_left > 0 || trace_rec->op_type >= NUM_OP_TYPES)
    {
        fetch_op->valid = false;
        p->halt_op_id = p->last_op_id;

        if (p->last_op_id == 0)
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
    fetch_op->valid = true;
    fetch_op->stall = false;
    fetch_op->is_mispred_cbr = false;
    fetch_op->op_id = ++p->last_op_id;
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
    p->trace_fd = trace_fd;
    p->halt_op_id = (uint64_t)(-1) - 3;

    // Allocate and initialize a branch predictor if needed.
    if (BPRED_POLICY != BPRED_PERFECT)
    {
        p->b_pred = new BPred(BPRED_POLICY);
    }

    return p;
}

/**
 * Print out the state of the pipeline latches for debugging purposes.
 * @param p the pipeline
 */
void pipe_print_state(Pipeline *p)
{
    printf("\n");
    // Print table header
    for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES; latch_type++)
    {
        switch (latch_type)
        {
        case IF_LATCH:
            printf(" IF:    ");
            break;
        case ID_LATCH:
            printf(" ID:    ");
            break;
        case EX_LATCH:
            printf(" EX:    ");
            break;
        case MA_LATCH:
            printf(" MA:    ");
            break;
        default:
            printf(" ------ ");
        }
    }
    printf("\n");

    // Print row for each lane in pipeline width
    for (uint8_t i = 0; i < PIPE_WIDTH; i++)
    {
        for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES;
             latch_type++)
        {
            if (p->pipe_latch[latch_type][i].valid)
            {
                printf(" %6lu ",
                       (unsigned long)p->pipe_latch[latch_type][i].op_id);
            }
            else
            {
                printf(" ------ ");
            }
        }
        printf("\n");
    }
    for (uint8_t i = 0; i < PIPE_WIDTH; i++)
    {
        for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES;
             latch_type++)
        {
            if (p->pipe_latch[latch_type][i].valid)
            {
                int dest = (p->pipe_latch[latch_type][i].trace_rec.dest_needed) ? 
                        p->pipe_latch[latch_type][i].trace_rec.dest_reg : -1;
                int src1 = (p->pipe_latch[latch_type][i].trace_rec.src1_needed) ? 
                        p->pipe_latch[latch_type][i].trace_rec.src1_reg : -1;
                int src2 = (p->pipe_latch[latch_type][i].trace_rec.src2_needed) ? 
                        p->pipe_latch[latch_type][i].trace_rec.src2_reg : -1;
                int cc_read = p->pipe_latch[latch_type][i].trace_rec.cc_read;
                int cc_write = p->pipe_latch[latch_type][i].trace_rec.cc_write;
                int br_dir = p->pipe_latch[latch_type][i].trace_rec.br_dir;

                const char *op_type;
                if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_ALU)
                    op_type = "ALU";
                else if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_LD)
                    op_type = "LD";
                else if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_ST)
                    op_type = "ST";
                else if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_CBR)
                    op_type = "BR";
                else
                    op_type = "OTHER";

                printf("(%lu : %s) dest: %d, src1: %d, src2: %d , ccread: %d, ccwrite: %d, br_dir: %d\n",
                       (unsigned long)p->pipe_latch[latch_type][i].op_id,
                       op_type,
                       dest,
                       src1,
                       src2,
                       cc_read,
                       cc_write,
                       br_dir);
            }
            else
            {
                printf(" ------ \n");
            }
        }
        printf("\n");
    }
}

/**
 * Simulate one cycle of all stages of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle(Pipeline *p)
{
    p->stat_num_cycle++;

    // In hardware, all pipeline stages execute in parallel, and each pipeline
    // latch is populated at the start of the next clock cycle.

    // In our simulator, we simulate the pipeline stages one at a time in
    // reverse order, from the Write Back stage (WB) to the Fetch stage (IF).
    // We do this so that each stage can read from the latch before it and
    // write to the latch after it without needing to "double-buffer" the
    // latches.

    // Additionally, it means that earlier pipeline stages can know about
    // stalls triggered in later pipeline stages in the same cycle, as would be
    // the case with hardware stall signals asserted by combinational logic.

    pipe_cycle_WB(p);
    pipe_cycle_MA(p);
    pipe_cycle_EX(p);
    pipe_cycle_ID(p);
    pipe_cycle_IF(p);

    // Compile with "make debug" to have this show!
    #ifdef DEBUG
        pipe_print_state(p);
    #endif
}

/**
 * Simulate one cycle of the Write Back stage (WB) of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_WB(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (p->pipe_latch[MA_LATCH][i].valid)
        {
            p->stat_retired_inst++;

            if (p->pipe_latch[MA_LATCH][i].op_id >= p->halt_op_id)
            {
                // Halt the pipeline if we've reached the end of the trace.
                p->halt = true;
            }

            // If this instruction is a mispredicted branch, we need to handle it.
            if (p->pipe_latch[MA_LATCH][i].is_mispred_cbr)
            {
                // Clear the fetch_cbr_stall flag to allow IF stage to fetch new instructions
                p->fetch_cbr_stall = false;
            }
        }
    }
}

/**
 * Simulate one cycle of the Memory Access stage (MA) of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_MA(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the EX latch to the MA latch.
        p->pipe_latch[MA_LATCH][i] = p->pipe_latch[EX_LATCH][i];

    }
}

/**
 * Simulate one cycle of the Execute stage (EX) of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_EX(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the ID latch to the EX latch.
        p->pipe_latch[EX_LATCH][i] = p->pipe_latch[ID_LATCH][i];
    }
}

/**
 * Simulate one cycle of the Instruction Decode stage (ID) of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_ID(Pipeline *p)
{
    std::vector<int> IF_lane_stalls(PIPE_WIDTH, 0); // to mark which IF lanes need to be stalled
    std::vector<int> load_use_stalls(PIPE_WIDTH, 0); // to mark if there is a load-use stall


    if ((p->pipe_latch[IF_LATCH][0].op_id == 315) || (p->pipe_latch[IF_LATCH][1].op_id == 316))
    {
        // for setting breakpoints
    }
    

    // first pass to check inter stage dependancies.
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        for (unsigned int j = 0; j < PIPE_WIDTH; j++)
        {
            //check dependancies from EX latch to IF latch
            if (p->pipe_latch[EX_LATCH][j].op_id < p->pipe_latch[IF_LATCH][i].op_id)
            {
                if (p->pipe_latch[EX_LATCH][j].valid && p->pipe_latch[IF_LATCH][i].valid)
                {
                    int src1 = (p->pipe_latch[IF_LATCH][i].trace_rec.src1_needed) ? 
                                p->pipe_latch[IF_LATCH][i].trace_rec.src1_reg : -1;
                    int src2 = (p->pipe_latch[IF_LATCH][i].trace_rec.src2_needed) ? 
                                p->pipe_latch[IF_LATCH][i].trace_rec.src2_reg : -1;
                    int cc_read = p->pipe_latch[IF_LATCH][i].trace_rec.cc_read; 
                    int dest_ID = (p->pipe_latch[EX_LATCH][j].trace_rec.dest_needed) ? 
                                p->pipe_latch[EX_LATCH][j].trace_rec.dest_reg : -1;
                    int cc_write_ID = p->pipe_latch[EX_LATCH][j].trace_rec.cc_write;
                    if ((src1 != -1 && src1 == dest_ID) || (src2 != -1 && src2 == dest_ID) || 
                        (cc_read == 1 && cc_write_ID == 1))
                    {
                        if (ENABLE_EXE_FWD && p->pipe_latch[EX_LATCH][j].trace_rec.op_type != OP_LD)
                        {
                            // can be forwarded from EX stage
                            continue;
                        }
                        // check if there is a load instruction in EX stage that is stsalling the IF lane
                        if (ENABLE_EXE_FWD && p->pipe_latch[EX_LATCH][j].trace_rec.op_type == OP_LD)
                        {
                            // check if this is the younger load instruction in EX stage that is causing the load-use stall
                            int load_lane = load_use_stalls[i] - 1; // last stored lane number that is causing the load-use stall
                            bool is_younger_load = false;
                            if (load_lane != -1 && p->pipe_latch[EX_LATCH][j].op_id > p->pipe_latch[EX_LATCH][load_lane].op_id)
                            {
                                is_younger_load = true;                                
                            }
                            if (load_use_stalls[i] == 0 || is_younger_load)
                            {
                                // this is the youngest load instruction in EX stage that is causing the load-use stall
                                load_use_stalls[i] = j+1; // mark the lane number (j+1) that is causing the load-use stall
                                // j+1 is used to differentiate from no stall (0)
                            }
                            continue;
                        }
                        // stall the IF lane and invalidate the ID lane
                        IF_lane_stalls[i] = 1;
                        break; // no need to check further dependancies for this IF lane
                    }
                }
            }
            
            //check dependancies from MA latch to IF latch
            if (p->pipe_latch[MA_LATCH][j].op_id < p->pipe_latch[IF_LATCH][i].op_id)
            {
                if (p->pipe_latch[MA_LATCH][j].valid && p->pipe_latch[IF_LATCH][i].valid)
                {
                    int src1 = (p->pipe_latch[IF_LATCH][i].trace_rec.src1_needed) ? 
                                p->pipe_latch[IF_LATCH][i].trace_rec.src1_reg : -1;
                    int src2 = (p->pipe_latch[IF_LATCH][i].trace_rec.src2_needed) ? 
                                p->pipe_latch[IF_LATCH][i].trace_rec.src2_reg : -1;
                    int cc_read = p->pipe_latch[IF_LATCH][i].trace_rec.cc_read;
                    int dest_ID = (p->pipe_latch[MA_LATCH][j].trace_rec.dest_needed) ? 
                                p->pipe_latch[MA_LATCH][j].trace_rec.dest_reg : -1;
                    int cc_write_ID = p->pipe_latch[MA_LATCH][j].trace_rec.cc_write;
                    if ((src1 != -1 && src1 == dest_ID) || (src2 != -1 && src2 == dest_ID) || 
                        (cc_read == 1 && cc_write_ID == 1))
                    {
                        if (ENABLE_MEM_FWD)
                        {
                            // can be forwarded from MA stage
                            continue;
                        }
                        // stall the IF lane and invalidate the ID lane
                        IF_lane_stalls[i] = 1;
                        break; // no need to check further dependancies for this IF lane
                    }
                }
            }
        }
    }

    // 1.5 pass to check if any load_use stalls can be avoided by forwarding from a younger instruction in EX stage. 
    if (ENABLE_EXE_FWD)
    {
        for (unsigned int i = 0; i < PIPE_WIDTH; i++)
        {
            if (load_use_stalls[i] != 0) // there is a load-use stall for this IF lane
            {
                int load_lane = load_use_stalls[i] - 1; // lane number that is causing the load-use stall
                for (unsigned int j = 0; j < PIPE_WIDTH; j++)
                {
                    if (static_cast<int>(j) != load_lane && p->pipe_latch[EX_LATCH][j].valid && p->pipe_latch[IF_LATCH][i].valid &&
                        p->pipe_latch[EX_LATCH][j].op_id < p->pipe_latch[IF_LATCH][i].op_id
                        && p->pipe_latch[EX_LATCH][j].op_id > p->pipe_latch[EX_LATCH][load_lane].op_id) 
                    {
                        // found an instruction in EX stage that is younger than the load and can forward to the IF lane
                        int src1 = (p->pipe_latch[IF_LATCH][i].trace_rec.src1_needed) ? 
                                    p->pipe_latch[IF_LATCH][i].trace_rec.src1_reg : -1;
                        int src2 = (p->pipe_latch[IF_LATCH][i].trace_rec.src2_needed) ? 
                                    p->pipe_latch[IF_LATCH][i].trace_rec.src2_reg : -1;
                        int cc_read = p->pipe_latch[IF_LATCH][i].trace_rec.cc_read; 
                        int dest_ID = (p->pipe_latch[EX_LATCH][j].trace_rec.dest_needed) ? 
                                    p->pipe_latch[EX_LATCH][j].trace_rec.dest_reg : -1;
                        int cc_write_ID = p->pipe_latch[EX_LATCH][j].trace_rec.cc_write;
                        if ((src1 != -1 && src1 == dest_ID) || (src2 != -1 && src2 == dest_ID) || 
                            (cc_read == 1 && cc_write_ID == 1))
                        {
                            if (p->pipe_latch[EX_LATCH][j].trace_rec.op_type != OP_LD)
                            {
                                // can be forwarded from this younger EX stage instruction
                                load_use_stalls[i] = 0; // no longer a load-use stall
                        
                                break;
                            }
                        }
                    }
                }

                //check if such a load-use stall can be avoided 

                if (load_use_stalls[i] == 0) 
                {
                    IF_lane_stalls[i] = 0;
                }
                else {
                    IF_lane_stalls[i] = 1;
                }
            }
        }
    }

    // second pass to check inter lane dependancies. 
    for (unsigned int i = 0; i < PIPE_WIDTH; i++) 
    {
        for (unsigned int j = 0; j < PIPE_WIDTH; j++) 
        {
            if (j != i && p->pipe_latch[IF_LATCH][j].valid && p->pipe_latch[IF_LATCH][i].valid &&
                p->pipe_latch[IF_LATCH][j].op_id < p->pipe_latch[IF_LATCH][i].op_id ) 
                {
                    int src1 = (p->pipe_latch[IF_LATCH][i].trace_rec.src1_needed) ? 
                                p->pipe_latch[IF_LATCH][i].trace_rec.src1_reg : -1;
                    int src2 = (p->pipe_latch[IF_LATCH][i].trace_rec.src2_needed) ? 
                                p->pipe_latch[IF_LATCH][i].trace_rec.src2_reg : -1;
                    int cc_read = p->pipe_latch[IF_LATCH][i].trace_rec.cc_read; 
                    int dest_ID = (p->pipe_latch[IF_LATCH][j].trace_rec.dest_needed) ? 
                                p->pipe_latch[IF_LATCH][j].trace_rec.dest_reg : -1;
                    int cc_write_ID = p->pipe_latch[IF_LATCH][j].trace_rec.cc_write;
                    if ((src1 != -1 && src1 == dest_ID) || (src2 != -1 && src2 == dest_ID) || 
                        (cc_read == 1 && cc_write_ID == 1))
                    {
                        IF_lane_stalls[i] = 1;
                        break; // no need to check further dependancies for this IF lane
                    }
                }
        }
    }

    // third pass to ensure if any older op_id is stalled, all younger op_ids are stalled too.
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        for (unsigned int j = 0; j < PIPE_WIDTH; j++)
        {
            if (j != i && p->pipe_latch[IF_LATCH][j].valid && p->pipe_latch[IF_LATCH][i].valid &&
                p->pipe_latch[IF_LATCH][j].op_id < p->pipe_latch[IF_LATCH][i].op_id ) 
                {
                    // lane j has the older instruction.
                    if (IF_lane_stalls[j] == 1) 
                    {
                        IF_lane_stalls[i] = 1;
                        break; // no need to check further dependancies for this IF lane
                    }
                }
        }
    }

    // to set the stalls and valids in the latches
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (IF_lane_stalls[i] == 1) 
        {
            p->pipe_latch[IF_LATCH][i].stall = true; // stall the IF lane
            p->pipe_latch[ID_LATCH][i].valid = false; // invalidate the ID lane
            continue; // no need to copy the instruction to ID latch
        }

        // Copy each instruction from the IF latch to the ID latch.
        p->pipe_latch[ID_LATCH][i] = p->pipe_latch[IF_LATCH][i];
    }
}

/**
 * Simulate one cycle of the Instruction Fetch stage (IF) of a pipeline.
 * @param p the pipeline to simulate
 */
void pipe_cycle_IF(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        PipelineLatch fetch_op = {};
        if (p->pipe_latch[IF_LATCH][i].stall) {
            p->pipe_latch[IF_LATCH][i].stall = false;
            continue;
        }

        // Copy the instruction to the IF latch if stall was not set
        // Read an instruction from the trace file.
        if (!p->fetch_cbr_stall) {
            pipe_get_fetch_op(p, &fetch_op);
        }

        if (fetch_op.valid == false) {
            // no more instructions to fetch
            p->pipe_latch[IF_LATCH][i].valid = false;
            continue;
        }
        // Handle branch (mis)prediction.
        if (BPRED_POLICY != BPRED_PERFECT)
        {
            pipe_check_bpred(p, &fetch_op);
        }
        p->pipe_latch[IF_LATCH][i] = fetch_op;
        if (p->fetch_cbr_stall) {
            fetch_op.valid = false; // insert bubble in IF
        }
    }
}

/**
 * If the instruction just fetched is a conditional branch, check for a branch
 * misprediction, update the branch predictor, and set appropriate flags in the
 * pipeline.
 * @param p the pipeline
 * @param fetch_op the pipeline latch containing the operation fetched
 */
void pipe_check_bpred(Pipeline *p, PipelineLatch *fetch_op)
{
    
    if (fetch_op->valid && fetch_op->trace_rec.op_type == OP_CBR) {
        BranchDirection prediction = p->b_pred->predict(fetch_op->trace_rec.inst_addr);
        BranchDirection resolution = (fetch_op->trace_rec.br_dir == 1) ? TAKEN : NOT_TAKEN;

        // Update branch statistics
        p->b_pred->update(fetch_op->trace_rec.inst_addr, prediction, resolution);
        
        if (prediction != resolution) {
            // Misprediction
            fetch_op->is_mispred_cbr = true;
            p->fetch_cbr_stall = true; // stall the IF stage, this is cleared after branch resultion in MEM stage.
        } else {
            fetch_op->is_mispred_cbr = false;
        }
    }


}
