#include <stdbool.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "fifo.c"
#include "pe.c"

typedef struct {
    PE grid[N][N];
    FIFO top[N];
    FIFO left[N];
    int cycle;
} SystolicArray;


// ========== SYSTOLIC ARRAY (PRINT STATE) ==============

void sysarray_print_state(const SystolicArray *arr){
  const int LW = 6;   // width for left label column
  const int W  = 14;  // width for each ACC column (wider to fit "acc (+a*b)")

  printf("Cycle = %d\n", arr->cycle);

  // Column headers (left-aligned to match cells)
  printf("%-*s", LW, "i \\ j");
  for (int j = 0; j < N; j++) {
    printf("%-*d", W, j);
  }
  printf("\n");

  // Rows
  for (int i = 0; i < N; i++) {
    printf("%-*d", LW, i);

    for (int j = 0; j < N; j++) {
      char cell[64];
      snprintf(cell, sizeof(cell), "%d", arr->grid[i][j].acc);

      // Truncate if longer than column width to preserve alignment
      if ((int)strlen(cell) > W) {
        // print first W characters
        printf("%-*.*s", W, W, cell);
      } else {
        printf("%-*s", W, cell);
      }
    }
    printf("\n");
  }
  printf("\n");
}

//----------Returns true if all PEs have done==true---------
bool sysarray_all_done(const SystolicArray *arr){
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      if (!arr->grid[i][j].done) {
        return false; }}}
  return true;
}

// Copy the PE accumulators into C[row][col]
void sysarray_readC(const SystolicArray *arr, int C[N][N]){
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      C[i][j] = arr->grid[i][j].acc;
    }
  }
}


// Init the systolic arrays
//   (1) NxN PEs 
//   (2) N fifos on left 
//   (3) N fifos on top 
//   (4) Init both sets of fifos appropriately


void sysarray_init(SystolicArray *arr, int A[N][N], int B[N][N]){

  arr->cycle = 0;

  // Initialize PEs
  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      pe_init(&arr->grid[i][j]);
    }
  }

  // Initialize FIFOs
  for(int i=0; i<N; i++){
    fifo_init(&arr->left[i]);
  }
  for(int j=0; j<N; j++){
    fifo_init(&arr->top[j]);
  }

  // Preload left FIFOs with A rows
  for(int i=0; i<N; i++){
    for(int k=0; k<N; k++){
      fifo_push(&arr->left[i], A[i][k]);
    }
  }

  // Preload top FIFOs with B columns
  for(int j=0; j<N; j++){
    for(int k=0; k<N; k++){
      fifo_push(&arr->top[j], B[k][j]);
    }
  }
}


// Advance the systolic array by one cycle.
// Steps:
//   (1) Cycle PEs 
//   (2) If PE did useful work, then invalidate west_in and north_in
//   (3) If PE did useful work, then transfer east_out of PE to west_in next PE
//   (4) If PE did useful work, then transfer sout_out of PE to north_in of next PE
//   (5) Check if Col-0 can consume from left FIFOs
//   (6) Check if Row-0 can consume from top FIFOs


void sysarray_cycle(SystolicArray *arr){

  // Step 1: cycle all PEs with their current inputs
  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      pe_cycle(&arr->grid[i][j]);
    }
  }

  // Prepare staging for next cycle's inputs
  DataPacket next_west[N][N];
  DataPacket next_north[N][N];
  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      next_west[i][j] = arr->grid[i][j].west_in;
      next_north[i][j] = arr->grid[i][j].north_in;
    }
  }

  // Step 2: invalidate inputs that were consumed this cycle
  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      if(arr->grid[i][j].worked){
        next_west[i][j].valid = false;
        next_north[i][j].valid = false;
      }
    }
  }

  // Step 3 and 4: propagate outputs to neighbors for next cycle
  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      if(!arr->grid[i][j].worked){
        continue;
      }

      if(j+1 < N){
        next_west[i][j+1] = arr->grid[i][j].east_out;
      }
      if(i+1 < N){
        next_north[i+1][j] = arr->grid[i][j].south_out;
      }
    }
  }

  // Step 5: feed column 0 from left FIFOs when input is free
  for(int i=0; i<N; i++){
    if(!next_west[i][0].valid){
      int val;
      if(fifo_pop(&arr->left[i], &val)){
        next_west[i][0].valid = true;
        next_west[i][0].data = val;
      }
    }
  }

  // Step 6: feed row 0 from top FIFOs when input is free
  for(int j=0; j<N; j++){
    if(!next_north[0][j].valid){
      int val;
      if(fifo_pop(&arr->top[j], &val)){
        next_north[0][j].valid = true;
        next_north[0][j].data = val;
      }
    }
  }

  // Commit staged inputs for next cycle
  for(int i=0; i<N; i++){
    for(int j=0; j<N; j++){
      arr->grid[i][j].west_in = next_west[i][j];
      arr->grid[i][j].north_in = next_north[i][j];
    }
  }

  arr->cycle++;
}


