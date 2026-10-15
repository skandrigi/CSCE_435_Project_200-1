#!/bin/bash
# Submit the full evaluation grid from Report.md section 2c / 4a:
#   7 sizes x 4 input types x 10 process counts = 280 jobs
#
#   bash run_all.sh                 # everything
#   bash run_all.sh 2 4 8           # only these process counts
#
# Not needed for the implementation assignment (one .cali file is enough there);
# this is for the performance evaluation part. P >= 64 spans several nodes and
# P = 1024 needs 22 nodes, so those jobs may sit in the queue for a while.

if [ $# -gt 0 ]; then PROCS="$*"; else PROCS="2 4 8 16 32 64 128 256 512 1024"; fi
SIZES="65536 262144 1048576 4194304 16777216 67108864 268435456"
TYPES="Sorted ReverseSorted Random 1_perc_perturbed"

mkdir -p logs cali
count=0
for P in $PROCS; do
    for N in $SIZES; do
        for T in $TYPES; do
            bash submit.sh $N $P $T strong
            count=$((count + 1))
        done
    done
done
echo "submitted $count jobs; watch with: squeue -u $USER"
