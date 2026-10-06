# CSCE 435 Group project

## 0. Group number: 200-1

## 1. Group members:

1. Sandeep Kandrigi
2. Benjamin Aleman
3. Zachary Smith
4. Skyler (Justin) Camenisch
Communicating through iMessage

## 2. Project topic (e.g., parallel sorting algorithms)

### 2a. Brief project description (what algorithms will you be comparing and on what architectures)

- Bitonic Sort:
- Sample Sort:
- Merge Sort:
- Radix Sort: Sandeep Kandrigi

### 2b. Pseudocode for each parallel algorithm

- For MPI programs, include MPI calls you will use to coordinate between processes

Radix Sort Pseudocode (BASE 10):
```
RADIX-SORT(A, d):
    // A: array of non-negative integers
    // d: number of digits in the largest number
    for i = 1 to d:
        STABLE-COUNTING-SORT(A, digit i)

STABLE-COUNTING-SORT(A, digit i):
    // base 10 → digits 0..9
    count = array of 10 zeros
    output = array of length n

    // 1. Count occurrences of each digit
    for j = 0 to n-1:
        k = (A[j] / 10^(i-1)) mod 10
        count[k] += 1

    // 2. Prefix sums → final positions
    for k = 1 to 9:
        count[k] += count[k-1]

    // 3. Place elements, iterating backwards to keep it stable
    for j = n-1 down to 0:
        k = (A[j] / 10^(i-1)) mod 10
        count[k] -= 1
        output[count[k]] = A[j]

    copy output into A
```

Radix Sort Pseudocode (BASE 256):
```
RADIX-SORT-256(A):
    // A: array of non-negative 32-bit integers
    // 32-bit int = 4 bytes → always 4 passes, one byte (8 bits) per pass
    for i = 1 to 4:
        STABLE-COUNTING-SORT-256(A, byte i)

STABLE-COUNTING-SORT-256(A, byte i):
    // base 256 → digits 0..255
    count = array of 256 zeros
    output = array of length n
    shift = 8 * (i-1)

    // 1. Count occurrences of each byte value
    for j = 0 to n-1:
        k = (A[j] >> shift) & 0xFF
        count[k] += 1

    // 2. Prefix sums → final positions
    for k = 1 to 255:
        count[k] += count[k-1]

    // 3. Place elements, iterating backwards to keep it stable
    for j = n-1 down to 0:
        k = (A[j] >> shift) & 0xFF
        count[k] -= 1
        output[count[k]] = A[j]

    copy output into A
```

Parallel Radix Sort Pseudocode (MPI, BASE 256):
```
PARALLEL-RADIX-SORT-256(n):
    // n: total number of elements, p: number of MPI ranks
    // each rank holds local_n = n / p elements (n, p are powers of 2)
    MPI_Init()
    MPI_Comm_rank(MPI_COMM_WORLD, &rank)
    MPI_Comm_size(MPI_COMM_WORLD, &p)
    local_n = n / p
    A = generate local_n non-negative ints for this rank

    for i = 1 to 4:
        shift = 8 * (i-1)

        // 1. Local histogram of byte i
        count = array of 256 zeros
        for j = 0 to local_n-1:
            count[(A[j] >> shift) & 0xFF] += 1

        // 2. Global bucket totals and this rank's offset in each bucket   // comm_small
        MPI_Allreduce(count, total, 256, MPI_INT, MPI_SUM, MPI_COMM_WORLD)
        MPI_Exscan(count, rank_offset, 256, MPI_INT, MPI_SUM, MPI_COMM_WORLD)
        if rank == 0: rank_offset = array of 256 zeros

        // 3. Global start of each bucket
        bucket_start[0] = 0
        for k = 1 to 255:
            bucket_start[k] = bucket_start[k-1] + total[k-1]

        // 4. Stable local counting sort by byte i, so elements are grouped by
        //    bucket; their global positions are then increasing, which means
        //    elements going to the same destination rank are contiguous
        STABLE-COUNTING-SORT-256(A, byte i)

        // 5. Global position of each element → destination rank
        sendcounts = array of p zeros
        seen = array of 256 zeros
        for j = 0 to local_n-1:
            k = (A[j] >> shift) & 0xFF
            pos = bucket_start[k] + rank_offset[k] + seen[k]
            seen[k] += 1
            sendcounts[pos / local_n] += 1

        // 6. Exchange counts, then exchange the data
        MPI_Alltoall(sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, MPI_COMM_WORLD)
        sdispls, rdispls = exclusive prefix sums of sendcounts, recvcounts
        MPI_Alltoallv(A, sendcounts, sdispls, MPI_INT,
                      B, recvcounts, rdispls, MPI_INT, MPI_COMM_WORLD)

        // 7. B arrives grouped by source rank. A stable sort by byte i puts it in
        //    global order (within a bucket, lower ranks come first)
        STABLE-COUNTING-SORT-256(B, byte i)
        A = B        // each rank again holds exactly local_n elements

    // Correctness check
    MPI_Sendrecv(A[local_n-1] to rank+1, first element of rank+1 from rank+1)
    MPI_Allreduce(local_ok, global_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD)

    MPI_Finalize()
```

### 2c. Evaluation plan - what and how will you measure and compare

**Data:** 32-bit non-negative integers (`int`), generated at runtime on each rank (`data_init_runtime`).

**Input types:** Sorted, Reverse sorted, Random (uniform), 1% perturbed (sorted, then 1% of elements swapped at random positions).

**Input sizes:** 2^16, 2^18, 2^20, 2^22, 2^24, 2^26, 2^28

**Processes (MPI ranks):** 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024
→ 7 sizes × 4 types × 10 process counts = 280 runs per algorithm.

**Strong scaling:** For each input size and input type, hold the total problem size fixed and increase the number of processes. We will plot time vs. num_procs and speedup (T_2 / T_p) for `main`, `comm`, and `comp_large`.

**Weak scaling:** Hold the elements per process (n/p) constant while increasing p. Sizes grow 4× per step while process counts grow 2× per step, so we take the diagonals of the run grid where n/p is constant, e.g. n/p = 2^14: (2^16, 4), (2^18, 16), (2^20, 64), (2^22, 256), (2^24, 1024). We will plot time vs. num_procs for each input type; ideal weak scaling is a flat line.

**Metrics (Caliper + Thicket):** min, max, and average time per rank, total time, and variance of time per rank, for `main`, `comm` (`comm_small`/`comm_large`), and `comp` (`comp_small`/`comp_large`). Comparing max with average time per rank shows load imbalance. The comm-to-comp ratio shows where each algorithm stops scaling.

**Comparison:** All four algorithms (bitonic, sample, merge, radix) run on the same grid on Grace, so we can compare them directly at each (size, type, procs) point.

### 3a. Caliper instrumentation

Please use the caliper build `/scratch/group/csce-435-f26/Caliper/caliper/share/cmake/caliper`
(same as lab2 build.sh) to collect caliper files for each experiment you run.

Your Caliper annotations should result in the following calltree
(use `Thicket.tree()` to see the calltree):

```
main
|_ data_init_X      # X = runtime OR io
|_ comm
|    |_ comm_small
|    |_ comm_large
|_ comp
|    |_ comp_small
|    |_ comp_large
|_ correctness_check
```

Required region annotations:

- `main` - top-level main function.
  - `data_init_X` - the function where input data is generated or read in from file. Use _data_init_runtime_ if you are generating the data during the program, and _data_init_io_ if you are reading the data from a file.
  - `correctness_check` - function for checking the correctness of the algorithm output (e.g., checking if the resulting data is sorted).
  - `comm` - All communication-related functions in your algorithm should be nested under the `comm` region.
    - Inside the `comm` region, you should create regions to indicate how much data you are communicating (i.e., `comm_small` if you are sending or broadcasting a few values, `comm_large` if you are sending all of your local values).
    - Notice that auxillary functions like MPI_init are not under here.
  - `comp` - All computation functions within your algorithm should be nested under the `comp` region.
    - Inside the `comp` region, you should create regions to indicate how much data you are computing on (i.e., `comp_small` if you are sorting a few values like the splitters, `comp_large` if you are sorting values in the array).
    - Notice that auxillary functions like data_init are not under here.
  - `MPI_X` - You will also see MPI regions in the calltree if using the appropriate MPI profiling configuration (see **Builds/**). Examples shown below.

All functions will be called from `main` and most will be grouped under either `comm` or `comp` regions, representing communication and computation, respectively. You should be timing as many significant functions in your code as possible. **Do not** time print statements or other insignificant operations that may skew the performance measurements.

### **Nesting Code Regions Example** - all computation code regions should be nested in the "comp" parent code region as following:

```
CALI_MARK_BEGIN("comp");
CALI_MARK_BEGIN("comp_small");
sort_pivots(pivot_arr);
CALI_MARK_END("comp_small");
CALI_MARK_END("comp");

# Other non-computation code
...

CALI_MARK_BEGIN("comp");
CALI_MARK_BEGIN("comp_large");
sort_values(arr);
CALI_MARK_END("comp_large");
CALI_MARK_END("comp");
```

### **Calltree Example**:

```
# MPI Mergesort
4.695 main
├─ 0.001 MPI_Comm_dup
├─ 0.000 MPI_Finalize
├─ 0.000 MPI_Finalized
├─ 0.000 MPI_Init
├─ 0.000 MPI_Initialized
├─ 2.599 comm
│  ├─ 2.572 MPI_Barrier
│  └─ 0.027 comm_large
│     ├─ 0.011 MPI_Gather
│     └─ 0.016 MPI_Scatter
├─ 0.910 comp
│  └─ 0.909 comp_large
├─ 0.201 data_init_runtime
└─ 0.440 correctness_check
```

### 3b. Collect Metadata

Have the following code in your programs to collect metadata:

```
adiak::init(NULL);
adiak::launchdate();    // launch date of the job
adiak::libraries();     // Libraries used
adiak::cmdline();       // Command line used to launch the job
adiak::clustername();   // Name of the cluster
adiak::value("algorithm", algorithm); // The name of the algorithm you are using (e.g., "merge", "bitonic")
adiak::value("programming_model", programming_model); // e.g. "mpi"
adiak::value("data_type", data_type); // The datatype of input elements (e.g., double, int, float)
adiak::value("size_of_data_type", size_of_data_type); // sizeof(datatype) of input elements in bytes (e.g., 1, 2, 4)
adiak::value("input_size", input_size); // The number of elements in input dataset (1000)
adiak::value("input_type", input_type); // For sorting, this would be choices: ("Sorted", "ReverseSorted", "Random", "1_perc_perturbed")
adiak::value("num_procs", num_procs); // The number of processors (MPI ranks)
adiak::value("scalability", scalability); // The scalability of your algorithm. choices: ("strong", "weak")
adiak::value("group_num", group_number); // The number of your group (integer, e.g., 1, 10)
adiak::value("implementation_source", implementation_source); // Where you got the source code of your algorithm. choices: ("online", "ai", "handwritten").
```

They will show up in the `Thicket.metadata` if the caliper file is read into Thicket.

### **See the `Builds/` directory to find the correct Caliper configurations to get the performance metrics.** They will show up in the `Thicket.dataframe` when the Caliper file is read into Thicket.

## 4. Performance evaluation

Include detailed analysis of computation performance, communication performance.
Include figures and explanation of your analysis.

### 4a. Vary the following parameters

For input_size's:

- 2^16, 2^18, 2^20, 2^22, 2^24, 2^26, 2^28

For input_type's:

- Sorted, Random, Reverse sorted, 1%perturbed

MPI: num_procs:

- 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024

This should result in 4x7x10=280 Caliper files for your MPI experiments.

### 4b. Hints for performance analysis

To automate running a set of experiments, parameterize your program.

- input_type: "Sorted" could generate a sorted input to pass into your algorithms
- algorithm: You can have a switch statement that calls the different algorithms and sets the Adiak variables accordingly
- num_procs: How many MPI ranks you are using

When your program works with these parameters, you can write a shell script
that will run a for loop over the parameters above (e.g., on 64 processors,
perform runs that invoke algorithm2 for Sorted, ReverseSorted, and Random data).

### 4c. You should measure the following performance metrics

- `Time`
  - Min time/rank
  - Max time/rank
  - Avg time/rank
  - Total time
  - Variance time/rank

## 5. Presentation

Plots for the presentation should be as follows:

- For each implementation:
  - For each of comp_large, comm, and main:
    - Strong scaling plots for each input_size with lines for input_type (7 plots - 4 lines each)
    - Strong scaling speedup plot for each input_type (4 plots)
    - Weak scaling plots for each input_type (4 plots)

Analyze these plots and choose a subset to present and explain in your presentation.

## 6. Final Report

Submit a zip named `TeamX.zip` where `X` is your team number. The zip should contain the following files:

- Algorithms: Directory of source code of your algorithms.
- Data: All `.cali` files used to generate the plots separated by algorithm/implementation.
- Jupyter notebook: The Jupyter notebook(s) used to generate the plots for the report.
- Report.md
