// Parallel bitonic sort (MPI, block-based) with Caliper regions and Adiak metadata.
// CSCE 435 group 200-1 -- Bitonic Sort: Benjamin Aleman
//
// Translates the PARALLEL-BITONIC-SORT pseudocode in Report.md section 2b:
//   1. each rank generates local_n = N / P ints and sorts them (comp_large)
//   2. the bitonic network of BITONIC-SORT-ITERATIVE runs over ranks instead of
//      elements: log P stages of up to log P compare-exchange steps, each step
//      trading whole blocks with partner = rank ^ j (comm_large) and keeping the
//      low or high half of the merged pair (comp_large)
//   3. correctness check: sorted locally and across block boundaries
//
// usage: mpirun -np <P> ./bitonic <N> <input_type> [strong|weak]
//   N           total number of elements, power of 2, divisible by P (P power of 2)
//   input_type  Sorted | ReverseSorted | Random | 1_perc_perturbed

#include <mpi.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <caliper/cali.h>
#include <caliper/cali-manager.h>
#include <adiak.hpp>

// Adiak metadata constants (see Report.md section 3b).
static const char* ALGORITHM = "bitonic";
static const char* PROGRAMMING_MODEL = "mpi";
static const int GROUP_NUM = 200;  // group "200-1"
// choices: "online", "ai", "handwritten". The pseudocode and design are handwritten;
// this C++ translation and the scripts were written with the help of an AI assistant
// (see README.md, "AI assistance disclosure"). Change if course policy says otherwise.
static const char* IMPLEMENTATION_SOURCE = "ai";

static inline bool is_pow2(int64_t x) { return x > 0 && (x & (x - 1)) == 0; }

// Deterministic "random" non-negative int that depends only on the global index,
// so every rank count sees the same input array for a given N.
static inline int random_value(int64_t i) {
    uint64_t x = static_cast<uint64_t>(i) + 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return static_cast<int>(x & 0x7FFFFFFFULL);
}

// Canonical input type names used in the adiak metadata.
static const char* canonical_input_type(const char* s) {
    if (std::strcmp(s, "Sorted") == 0 || std::strcmp(s, "sorted") == 0) return "Sorted";
    if (std::strcmp(s, "ReverseSorted") == 0 || std::strcmp(s, "Reverse") == 0 ||
        std::strcmp(s, "reverse") == 0 || std::strcmp(s, "reverse_sorted") == 0)
        return "ReverseSorted";
    if (std::strcmp(s, "Random") == 0 || std::strcmp(s, "random") == 0) return "Random";
    if (std::strcmp(s, "1_perc_perturbed") == 0 || std::strcmp(s, "1%perturbed") == 0 ||
        std::strcmp(s, "Perturbed") == 0 || std::strcmp(s, "perturbed") == 0)
        return "1_perc_perturbed";
    return nullptr;
}

// ----- data_init_runtime -----
// Rank `rank` fills A with its block of the global input: global indices
// [rank*local_n, (rank+1)*local_n). No communication is needed.
static void generate_input(std::vector<int>& A, int64_t N, int rank, int local_n,
                           const std::string& input_type) {
    const int64_t base = static_cast<int64_t>(rank) * local_n;
    int* a = A.data();

    if (input_type == "Random") {
        for (int i = 0; i < local_n; ++i) a[i] = random_value(base + i);
        return;
    }
    if (input_type == "ReverseSorted") {
        for (int i = 0; i < local_n; ++i) a[i] = static_cast<int>(N - 1 - (base + i));
        return;
    }
    // Sorted and 1_perc_perturbed both start from the identity permutation.
    for (int i = 0; i < local_n; ++i) a[i] = static_cast<int>(base + i);

    if (input_type == "1_perc_perturbed" && local_n >= 2) {
        // Swap 1% of this rank's elements with random positions in the block.
        // Perturbing locally keeps data_init free of communication.
        int64_t swaps = std::max<int64_t>(1, local_n / 100);
        std::mt19937 gen(12345u + static_cast<unsigned>(rank));
        std::uniform_int_distribution<int> pos(0, local_n - 1);
        for (int64_t s = 0; s < swaps; ++s) {
            int i = pos(gen);
            int j = pos(gen);
            std::swap(a[i], a[j]);
        }
    }
}

// ----- comp_large: merge step of the bitonic network -----
// A and B are both sorted, length n. Writes the n smallest (keep_low) or the
// n largest values of A+B into out. Only n elements are ever produced, so this
// is O(n) and never materialises the full 2n merge.
static void merge_keep_low(const int* A, const int* B, int* out, int n) {
    int i = 0, j = 0, k = 0;
    while (k < n) {
        if (j >= n || (i < n && A[i] <= B[j])) out[k++] = A[i++];
        else                                   out[k++] = B[j++];
    }
}

static void merge_keep_high(const int* A, const int* B, int* out, int n) {
    int i = n - 1, j = n - 1, k = n - 1;
    while (k >= 0) {
        if (j < 0 || (i >= 0 && A[i] >= B[j])) out[k--] = A[i--];
        else                                   out[k--] = B[j--];
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0, P = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    // ----- argument parsing (not timed) -----
    if (argc < 3) {
        if (rank == 0)
            std::fprintf(stderr,
                         "usage: %s <N> <Sorted|ReverseSorted|Random|1_perc_perturbed> [strong|weak]\n",
                         argv[0]);
        MPI_Finalize();
        return 1;
    }
    const int64_t N = static_cast<int64_t>(std::atoll(argv[1]));
    const char* type_c = canonical_input_type(argv[2]);
    const char* scalability = (argc >= 4) ? argv[3] : "strong";

    const char* err = nullptr;
    if (!is_pow2(N))                      err = "N must be a power of 2";
    else if (!is_pow2(P))                 err = "number of MPI ranks must be a power of 2";
    else if (N % P != 0)                  err = "N must be divisible by the number of ranks";
    else if (N / P > INT_MAX)             err = "N/P must fit in an int";
    else if (N > INT_MAX)                 err = "N must fit in an int (element values are indices)";
    else if (type_c == nullptr)           err = "unknown input_type";
    else if (std::strcmp(scalability, "strong") != 0 && std::strcmp(scalability, "weak") != 0)
                                          err = "scalability must be strong or weak";
    if (err) {
        if (rank == 0) std::fprintf(stderr, "error: %s\n", err);
        MPI_Finalize();
        return 1;
    }
    const std::string input_type = type_c;
    const int local_n = static_cast<int>(N / P);

    // ----- Caliper / Adiak setup (config comes from the CALI_CONFIG env var) -----
    cali::ConfigManager mgr;
    mgr.start();

    adiak::init(nullptr);
    adiak::launchdate();
    adiak::libraries();
    adiak::cmdline();
    adiak::clustername();
    adiak::value("algorithm", ALGORITHM);
    adiak::value("programming_model", PROGRAMMING_MODEL);
    adiak::value("data_type", "int");
    adiak::value("size_of_data_type", sizeof(int));
    adiak::value("input_size", N);
    adiak::value("input_type", input_type);
    adiak::value("num_procs", P);
    adiak::value("scalability", scalability);
    adiak::value("group_num", GROUP_NUM);
    adiak::value("implementation_source", IMPLEMENTATION_SOURCE);

    CALI_MARK_BEGIN("main");

    std::vector<int> A(local_n), B(local_n), C(local_n);

    // ----- data_init_runtime -----
    CALI_MARK_BEGIN("data_init_runtime");
    generate_input(A, N, rank, local_n, input_type);
    CALI_MARK_END("data_init_runtime");

    // Line every rank up so start-up skew is not charged to the first Sendrecv.
    CALI_MARK_BEGIN("comm");
    MPI_Barrier(MPI_COMM_WORLD);
    CALI_MARK_END("comm");

    // ----- 1. local sort: every rank holds one sorted block -----
    CALI_MARK_BEGIN("comp");
    CALI_MARK_BEGIN("comp_large");
    std::sort(A.begin(), A.end());
    CALI_MARK_END("comp_large");
    CALI_MARK_END("comp");

    // ----- 2. bitonic network over ranks -----
    // k = size of the rank groups being merged, j = distance to the partner.
    for (int k = 2; k <= P; k <<= 1) {
        for (int j = k >> 1; j > 0; j >>= 1) {
            const int partner = rank ^ j;
            const bool ascending = (rank & k) == 0;
            // In an ascending group the lower rank keeps the small half; in a
            // descending group it keeps the large half.
            const bool keep_low = (ascending == (rank < partner));

            // 3. trade whole blocks with the partner
            CALI_MARK_BEGIN("comm");
            CALI_MARK_BEGIN("comm_large");
            MPI_Sendrecv(A.data(), local_n, MPI_INT, partner, 0,
                         B.data(), local_n, MPI_INT, partner, 0,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            CALI_MARK_END("comm_large");
            CALI_MARK_END("comm");

            // 4. merge and keep the half this rank is owed; A stays sorted
            CALI_MARK_BEGIN("comp");
            CALI_MARK_BEGIN("comp_large");
            if (keep_low) merge_keep_low(A.data(), B.data(), C.data(), local_n);
            else          merge_keep_high(A.data(), B.data(), C.data(), local_n);
            A.swap(C);
            CALI_MARK_END("comp_large");
            CALI_MARK_END("comp");
        }
    }

    // ----- correctness_check -----
    // Every rank's block is sorted, and the last element of rank r must be
    // <= the first element of rank r+1. Every rank still holds local_n items,
    // so only the block boundaries need to cross ranks.
    int global_ok = 0;
    CALI_MARK_BEGIN("correctness_check");
    {
        int local_ok = 1;
        for (int i = 1; i < local_n; ++i) {
            if (A[i - 1] > A[i]) { local_ok = 0; break; }
        }
        const int next = (rank + 1 < P) ? rank + 1 : MPI_PROC_NULL;
        const int prev = (rank > 0) ? rank - 1 : MPI_PROC_NULL;
        int my_last = A[local_n - 1];
        int prev_last = INT_MIN;
        MPI_Sendrecv(&my_last, 1, MPI_INT, next, 1,
                     &prev_last, 1, MPI_INT, prev, 1,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (rank > 0 && prev_last > A[0]) local_ok = 0;
        MPI_Allreduce(&local_ok, &global_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);
    }
    CALI_MARK_END("correctness_check");

    CALI_MARK_END("main");

    // ----- untimed output -----
    if (N <= 64) {
        // Small runs: print the full sorted array on rank 0 for eyeballing.
        std::vector<int> all(rank == 0 ? N : 0);
        MPI_Gather(A.data(), local_n, MPI_INT, all.data(), local_n, MPI_INT, 0, MPI_COMM_WORLD);
        if (rank == 0) {
            std::printf("sorted: ");
            for (int64_t i = 0; i < N; ++i) std::printf("%d ", all[i]);
            std::printf("\n");
        }
    }
    if (rank == 0) {
        std::printf("bitonic sort: N=%lld P=%d local_n=%d input_type=%s  check=%s\n",
                    static_cast<long long>(N), P, local_n, input_type.c_str(),
                    global_ok ? "PASSED" : "FAILED");
        std::fflush(stdout);
    }

    // ----- Caliper / Adiak teardown -----
    mgr.stop();
    mgr.flush();
    adiak::fini();
    MPI_Finalize();
    return global_ok ? 0 : 2;
}
