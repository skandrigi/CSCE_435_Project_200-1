#!/bin/bash
# Build on Grace:   bash build.sh        (or sbatch build.grace_job to build on a compute node)
# Same module stack as the lab 2 build.sh.
module load GCC/11.3.0
module load GCC/12.2.0
module load NVHPC/22.7-CUDA-11.7.0
module load OpenMPI/4.1.4
module load CMake/3.12.1
module load GCCcore/12.2.0
module load PAPI/7.0.1

# Report.md section 3a names /scratch/group/csce-435-f26/Caliper/caliper; the lab 2
# build that produced working .cali files with the OpenMPI/4.1.4 module used
# caliper-openmpi. Prefer the one that is known to work, fall back to the other.
CALIPER_DIR=/scratch/group/csce-435-f26/caliper-openmpi/share/cmake/caliper
if [ ! -d "$CALIPER_DIR" ]; then
    CALIPER_DIR=/scratch/group/csce-435-f26/Caliper/caliper/share/cmake/caliper
fi
echo "using caliper_DIR=$CALIPER_DIR"

cmake \
    -Dcaliper_DIR="$CALIPER_DIR" \
    -Dadiak_DIR=/scratch/group/csce-435-f26/Adiak/adiak/lib/cmake/adiak \
    .

make
