# Bitonic Sort (MPI) -- Benjamin Aleman

Implementation of the `PARALLEL-BITONIC-SORT` pseudocode from `Report.md` 2b with
Caliper regions (3a) and Adiak metadata (3b).

## AI assistance disclosure

Everything in this folder was written with the assistance of an AI coding assistant
(Claude Code), not generated wholesale by it. The algorithm design, the pseudocode in
`Report.md` 2b, the choice of Caliper regions, and all testing on Grace were done by
Benjamin Aleman. The assistant was used to help translate the pseudocode into C++, to
draft and debug the CMake/Slurm scripts and `calltree.py`, and to work through Grace
build and module issues. All code was reviewed, run, and verified by hand on Grace
before being committed. The Adiak field `implementation_source` is set to `"ai"` to
reflect this assistance (the course offers only `online`, `ai`, or `handwritten`).

| file | purpose |
|---|---|
| `bitonic.cpp` | the program: `mpirun -np <P> ./bitonic <N> <input_type> [strong\|weak]` |
| `CMakeLists.txt`, `build.sh` | build, same module stack and Caliper/Adiak paths as lab 2 |
| `build.grace_job` | `sbatch` wrapper that runs `build.sh` on a compute node |
| `test.grace_job` | small correctness runs on one node (prints sorted arrays for N=32) |
| `mpi.grace_job`, `submit.sh` | one timed run -> `cali/bitonic-<type>-<N>-<P>-<job>.cali` |
| `run_all.sh` | the full 280-job grid for the evaluation part (not needed yet) |
| `calltree.py` | prints the Thicket calltree + metadata for `.cali` files |

## Running on Grace

Verified on grace2 on 2026-10-14 (build in `$SCRATCH`, not `$HOME`):

```bash
# copy this folder to $SCRATCH/csce435/bitonic on Grace, then:
cd $SCRATCH/csce435/bitonic
bash build.sh                      # login node is fine; or: sbatch build.grace_job
sbatch test.grace_job              # check=PASSED for every line in logs/test.<job>
bash submit.sh 1048576 8 Random    # one .cali file for the assignment

# calltree with Thicket
module load GCCcore/12.2.0 Python/3.10.8
python3 calltree.py cali/bitonic-Random-1048576-8-*.cali

# fallback without Python: cali-query needs the PAPI module
module load GCC/12.2.0 OpenMPI/4.1.4 PAPI/7.0.1
/scratch/group/csce-435-f26/caliper-openmpi/bin/cali-query \
  -q "SELECT path,avg#inclusive#sum#time.duration AS avg GROUP BY path FORMAT tree" cali/<file>.cali
```

`cali/` holds the two Caliper files from the first Grace runs (job 20150766: 2^20 Random on
8 ranks; job 20150767: 2^20 ReverseSorted on 16 ranks). The test job 20150765 passed all 52
correctness runs (N=32 with P=1..16, N=2^16 and 2^20 with P=2..16, all four input types).

## Regions

```
main
|_ data_init_runtime      generate local block (Sorted / ReverseSorted / Random / 1_perc_perturbed)
|_ comm                   MPI_Barrier before the timed algorithm
|    |_ comm_large        MPI_Sendrecv of the whole local block with partner = rank ^ j
|_ comp
|    |_ comp_large        local std::sort, then merge-and-keep-half after each exchange
|_ correctness_check      local sortedness + block boundary MPI_Sendrecv + MPI_Allreduce
```

There is no `comm_small` / `comp_small`: bitonic sort never exchanges or computes on a
few values. Every exchange moves a full block and every computation touches the block.
