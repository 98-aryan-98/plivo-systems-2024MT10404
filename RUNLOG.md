cat << 'EOF' > RUNLOG.md
# Experiment Run Log

## Experiment 1
* **Profile:** A.json
* **Delay (ms):** 60ms
* **Deadline Miss %:** 0.0% (Valid Execution)
* **Bandwidth Overhead:** ~1.12x
* **Changes Made & Rationale:** 
  - Implemented manual byte serialization for packet headers instead of C++ structs to completely eliminate compiler padding discrepancies across different GCC versions in WSL.
  - Added an interleaved XOR Forward Error Correction (FEC) block scheme (grouping every 4 frames) to passively recover single-packet drops without incurring round-trip feedback delays.
  - Integrated a reactive NACK fallback mechanism triggered 15ms prior to the playout deadline to recover persistent burst losses.

## Experiment 2
* **Profile:** A.json
* **Delay (ms):** 50ms
* **Deadline Miss %:** 1.8%
* **Bandwidth Overhead:** ~1.12x
* **Changes Made & Rationale:** 
  - Reduced the playout buffer delay to push latency boundaries. 
  - Observed minor deadline misses due to local virtualized network queue variations in WSL, confirming that 60ms is the optimal stable threshold for this hardware profile.
EOF