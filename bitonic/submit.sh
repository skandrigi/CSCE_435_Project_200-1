#!/bin/bash
# Submit one (N, P, input_type) point:  bash submit.sh <N> <P> <input_type> [strong|weak]
#
# Requests the minimum number of 48-core Grace nodes that fit P ranks
# (P <= 48 -> 1 node, P = 64 -> 2 nodes, P = 1024 -> 22 nodes). sbatch flags must
# come before the job file; everything after it is passed to mpi.grace_job.

if [ $# -lt 3 ]; then
    echo "usage: $0 <N> <P> <Sorted|ReverseSorted|Random|1_perc_perturbed> [strong|weak]"
    exit 1
fi

N=$1
P=$2
input_type=$3
scalability=${4:-strong}
CORES_PER_NODE=48

nodes=$(( (P + CORES_PER_NODE - 1) / CORES_PER_NODE ))
tasks_per_node=$(( (P + nodes - 1) / nodes ))

# 3 int buffers per rank; 2^28 ints is 1 GiB, so budget ~4 GiB per node for the
# largest inputs plus headroom. Smaller inputs get the 16G default.
mem=16G
if [ "$N" -ge 67108864 ]; then mem=32G; fi

mkdir -p logs cali

sbatch --job-name=bit_${N}_${P}_${input_type} \
       --nodes=$nodes \
       --ntasks-per-node=$tasks_per_node \
       --mem=$mem \
       --time=00:10:00 \
       --output=logs/output_${input_type}_${N}_${P}.%j \
       mpi.grace_job $N $P $input_type $scalability
