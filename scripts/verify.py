#!/usr/bin/env python3
"""Exercise timing-model modes and compare the systolic array to a scalar product."""
from pathlib import Path
import gzip, re, struct, subprocess, tempfile
root = Path(__file__).resolve().parents[1]

def run(argv, **kwargs):
    result = subprocess.run([str(x) for x in argv], check=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, timeout=90, **kwargs)
    return result.stdout

def metric(text, key):
    match = re.search(r'^' + re.escape(key) + r'\s*:\s*([\d.]+)', text, re.M)
    assert match, (key, text[-500:])
    return float(match[1])

with tempfile.TemporaryDirectory() as scratch:
    tmp = Path(scratch)
    # Serialize through the public structure so padding matches the local compiler ABI.
    writer = tmp / 'write_trace.cpp'
    writer.write_text('''#include "trace.h"
#include <cstdio>
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 1;
    for (int i = 0; i < 2048; ++i) {
        TraceRec r = {};
        r.inst_addr = 4096 + (i % 64) * 4;
        r.op_type = (i % 7 == 0) ? OP_CBR : ((i % 5 == 0) ? OP_LD : OP_ALU);
        r.dest_needed = r.op_type != OP_CBR;
        r.dest_reg = i % 16;
        r.src1_needed = 1;
        r.src1_reg = (i + 15) % 16;
        r.src2_needed = 1;
        r.src2_reg = (i + 13) % 16;
        r.br_dir = i % 2;
        r.mem_read = r.op_type == OP_LD;
        r.mem_addr = 8192 + (i % 128) * 64;
        fwrite(&r, sizeof(r), 1, f);
    }
    fclose(f);
}
''')
    run(['c++', '-std=c++11', '-I', root / 'superscalar_pipeline/src', writer, '-o', tmp / 'write_trace'])
    run([tmp / 'write_trace', tmp / 'pipeline.bin'])
    trace = tmp / 'pipeline.gz'
    with gzip.open(trace, 'wb') as f:
        f.write((tmp / 'pipeline.bin').read_bytes())
    pipeline = root / 'superscalar_pipeline/src/sim'
    for policy in [0, 1, 2]:
        text = run([pipeline, '-pipewidth', '2', '-enablememfwd', '-enableexefwd', '-bpredpolicy', str(policy), trace])
        assert metric(text, 'SIM_NUM_INST') == 2048
        assert metric(text, 'SIM_NUM_CYCLES') > 0
        if policy:
            assert metric(text, 'SIM_BPRED_BRANCHES') > 0
    ooo = root / 'out_of_order_execution/src/sim'
    for width in [1, 2]:
        for policy in [0, 1]:
            for latency in [1, 4]:
                text = run([ooo, '-pipewidth', str(width), '-schedpolicy', str(policy), '-loadlatency', str(latency), trace])
                assert metric(text, 'SIM_NUM_INST') == 2048
                assert metric(text, 'SIM_NUM_CYCLES') >= 2048 / width
    memtrace = tmp / 'memory.gz'
    with gzip.open(memtrace, 'wb') as f:
        for i in range(8192):
            f.write(struct.pack('=IBI', 4096 + (i % 32) * 4, 2 if i % 4 == 0 else 1, 16384 + (i % 512) * 64))
    cache = root / 'cache_memory_system/src/sim'
    for mode in [1, 2, 3, 4]:
        for policy in ([0, 2, 3] if mode == 4 else [0]):
            text = run([cache, '-mode', str(mode), '-L2repl', str(policy), memtrace, *([memtrace] if mode == 4 else [])])
            assert metric(text, 'CORE_0_INST') == 8192
            if mode == 4:
                assert metric(text, 'CORE_1_INST') == 8192
    # Check the predictor's state transitions and convergence separately from timing.
    pred_test = tmp / 'predictor.cpp'
    pred_test.write_text('''#include "bpred.h"
#include <cassert>
int main() {
    BPred p(BPRED_GSHARE);
    for (int i=0; i<128; ++i) { auto v=p.predict(64); p.update(64,v,NOT_TAKEN); }
    assert(p.predict(64)==NOT_TAKEN);
    BPred a(BPRED_ALWAYS_TAKEN);
    assert(a.predict(0)==TAKEN);
    a.update(0, TAKEN, NOT_TAKEN);
    assert(a.stat_num_mispred==1 && a.stat_num_branches==1);
}
''')
    run(['c++', '-std=c++11', '-I', root / 'superscalar_pipeline/src', pred_test, root / 'superscalar_pipeline/src/bpred.cpp', '-o', tmp / 'predictor'])
    run([tmp / 'predictor'])
    text = run([root / 'matrix_computing/systolic_array/src/sim'])
    assert 'PASS' in text and 'FAIL' not in text
    cycle = re.search(r'Cycle = (\d+)', text)
    assert cycle and int(cycle[1]) == 11
print('PASS: branch prediction; 8 scheduling configurations; cache/DRAM modes; 4x4 systolic result (11 cycles).')
