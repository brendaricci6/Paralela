#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 200112L

#include <strings.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mpi.h>

// Inclusão da biblioteca de medição de tempo do professor
#include "chronos.c"

#define TAMANHO_CACHE_LLC (16 * 1024 * 1024)
#define TAMANHO_BUFFER_LIMPEZA (3 * TAMANHO_CACHE_LLC)

// -----------------------------------------------------------------------------
// Funções Auxiliares e PRNG
// -----------------------------------------------------------------------------

static inline unsigned long long rand63(void) {
    return ((unsigned long long)(unsigned)rand())
         | ((unsigned long long)(unsigned)rand() << 21)
         | ((unsigned long long)(unsigned)rand() << 42);
}

static inline long long rand64(void) {
    union {
        unsigned long long u;
        long long s;
    } x;

    x.u = ((unsigned long long)(unsigned)rand() << 49)
        | ((unsigned long long)(unsigned)rand() << 34)
        | ((unsigned long long)(unsigned)rand() << 19)
        | ((unsigned long long)(unsigned)rand() << 4)
        | ((unsigned long long)(unsigned)rand() & 0xF);

    return x.s;
}

static inline void ll_swap(long long *a, long long *b) {
    long long t = *a;
    *a = *b;
    *b = t;
}

static void limpar_cache(char *buffer, size_t tamanho) {
    volatile long long soma = 0;
    for (size_t i = 0; i < tamanho; i += 64)
        soma += buffer[i];
    (void)soma;
}

static int comparar_long_long(const void *a, const void *b) {
    long long x = *(const long long *)a;
    long long y = *(const long long *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// Funções Baseadas no Trabalho 1
// -----------------------------------------------------------------------------

static void build_limits_sp2_serial(const long long *Input, long long n, int npivots, int nbins, long long *pivots, long long *limits) {
    long long stride = n / npivots;
    if (stride <= 0) stride = 1;

    for (int i = 0; i < npivots; i++) {
        long long jitter = (long long)(rand63() % (unsigned long long)stride);
        long long idx = i * stride + jitter;
        if (idx >= n) idx = n - 1;
        pivots[i] = Input[idx];
    }
    
    qsort(pivots, npivots, sizeof(long long), comparar_long_long);

    limits[0] = LLONG_MIN;
    limits[nbins] = LLONG_MAX;

    for (int i = 1; i < nbins; i++) {
        int idx = (int)(((long long)i * (npivots - 1)) / nbins);
        limits[i] = pivots[idx];
    }
    
    for (int k = 1; k <= nbins - 1; k++) {
        if (limits[k] <= limits[k - 1]) {
            if (limits[k - 1] < LLONG_MAX - 1)
                limits[k] = limits[k - 1] + 1;
            else
                limits[k] = limits[k - 1];
        }
    }
}

static inline int find_bin(long long v, const long long *limits, int nbins) {
    if (v >= limits[nbins - 1]) return nbins - 1;
    if (v < limits[1]) return 0;

    int left = 0;
    int right = nbins - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (v >= limits[mid]) {
            if (v < limits[mid + 1]) return mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Pthreads Pool e Variáveis
// -----------------------------------------------------------------------------

typedef struct {
    int tid;
    const long long *Input;
    long long *Output;
    const long long *Limits;
    long long nElements;
    int nbins;
    int nthreads;
    long long *Pos;
    long long *hist_local;
} ThreadData;

static pthread_t *threads = NULL;
static ThreadData *td = NULL;
static long long **thread_offsets = NULL;

static pthread_barrier_t barrier;
static volatile int shutdown_pool = 0;
static volatile int work_ready = 0;

static void worker_run_partition(int tid) {
    ThreadData *t = &td[tid];

    // --- ESTÁGIO 1: Construção do Histograma Local ---
    memset(t->hist_local, 0, t->nbins * sizeof(long long));
    long long start = (tid * t->nElements) / t->nthreads;
    long long end = ((tid + 1) * t->nElements) / t->nthreads;

    for (long long i = start; i < end; i++) {
        int b = find_bin(t->Input[i], t->Limits, t->nbins);
        t->hist_local[b]++;
    }

    pthread_barrier_wait(&barrier);

    // --- ESTÁGIO 2: Agregação (Apenas Thread 0) ---
    if (tid == 0) {
        t->Pos[0] = 0;
        for (int b = 1; b < t->nbins; b++) {
            long long sum = 0;
            for (int i = 0; i < t->nthreads; i++) {
                sum += td[i].hist_local[b - 1];
            }
            t->Pos[b] = t->Pos[b - 1] + sum;
        }

        for (int b = 0; b < t->nbins; b++) {
            long long current_offset = t->Pos[b];
            for (int i = 0; i < t->nthreads; i++) {
                thread_offsets[i][b] = current_offset;
                current_offset += td[i].hist_local[b];
            }
        }
    }

    pthread_barrier_wait(&barrier);

    // --- ESTÁGIO 3: Posicionamento ---
    for (long long i = start; i < end; i++) {
        long long val = t->Input[i];
        int b = find_bin(val, t->Limits, t->nbins);
        long long write_idx = thread_offsets[tid][b]++;
        t->Output[write_idx] = val;
    }
}

static void *worker_thread(void *arg) {
    int tid = *(int *)arg;
    while (1) {
        pthread_barrier_wait(&barrier);
        if (shutdown_pool) break;
        if (work_ready) worker_run_partition(tid);
        pthread_barrier_wait(&barrier);
    }
    return NULL;
}

static int pool_init(int nthreads, int nbins) {
    threads = malloc((nthreads - 1) * sizeof(pthread_t));
    td = malloc(nthreads * sizeof(ThreadData));
    thread_offsets = malloc(nthreads * sizeof(long long *));

    if (!td || !thread_offsets) return -1;

    pthread_barrier_init(&barrier, NULL, nthreads);

    for (int t = 0; t < nthreads; t++) {
        td[t].tid = t; 
        td[t].hist_local = calloc(nbins, sizeof(long long));
        thread_offsets[t] = calloc(nbins, sizeof(long long));
        if (!td[t].hist_local || !thread_offsets[t]) return -1;
    }

    for (int t = 1; t < nthreads; t++) {
        int *arg = malloc(sizeof(int));
        *arg = t;
        if (pthread_create(&threads[t - 1], NULL, worker_thread, arg) != 0)
            return -1;
    }
    return 0;
}

static void pool_destroy(int nthreads) {
    shutdown_pool = 1;
    pthread_barrier_wait(&barrier);

    for (int t = 1; t < nthreads; t++) pthread_join(threads[t - 1], NULL);

    for (int t = 0; t < nthreads; t++) {
        free(td[t].hist_local);
        free(thread_offsets[t]);
    }
    free(td);
    free(thread_offsets);
    free(threads);
    pthread_barrier_destroy(&barrier);
}

// -----------------------------------------------------------------------------
// Função Principal Solicitada
// -----------------------------------------------------------------------------

int parallel_multiPartition(const long long *Input, long long *Output, long long nElements, 
                            const long long *Limits, int nbins, long long *Pos, int nthreads) {
    if (nthreads == 1) {
        // Caminho serial puro
        long long *hist = calloc(nbins, sizeof(long long));
        for (long long i = 0; i < nElements; i++) {
            int b = find_bin(Input[i], Limits, nbins);
            hist[b]++;
        }
        Pos[0] = 0;
        for (int b = 1; b < nbins; b++) Pos[b] = Pos[b - 1] + hist[b - 1];
        
        long long *current_offsets = malloc(nbins * sizeof(long long));
        memcpy(current_offsets, Pos, nbins * sizeof(long long));

        for (long long i = 0; i < nElements; i++) {
            long long val = Input[i];
            int b = find_bin(val, Limits, nbins);
            Output[current_offsets[b]++] = val;
        }

        free(hist);
        free(current_offsets);
        return 0;
    }

    // Configura os dados globais para as threads
    for (int t = 0; t < nthreads; t++) {
        td[t].Input = Input;
        td[t].Output = Output;
        td[t].Limits = Limits;
        td[t].nElements = nElements;
        td[t].nbins = nbins;
        td[t].nthreads = nthreads;
        td[t].Pos = Pos;
    }

    work_ready = 1;
    pthread_barrier_wait(&barrier);
    worker_run_partition(0);
    pthread_barrier_wait(&barrier);
    work_ready = 0;

    return 0;
}

// -----------------------------------------------------------------------------
// Função de Verificação
// -----------------------------------------------------------------------------

void verifica_particoesLocais(const long long *Input, long long *Output, long long nElements,
                              const long long *Limits, int nbins, long long *Pos, int nthreads) {
    
    (void)Input;
    (void)nthreads;
    
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int ok = 1;

    for (int b = 0; b < nbins; b++) {
        long long start = Pos[b];
        long long end = (b == nbins - 1) ? nElements : Pos[b + 1];

        for (long long i = start; i < end; i++) {
            long long val = Output[i];
            int correct_bin = find_bin(val, Limits, nbins);
            if (correct_bin != b) {
                ok = 0;
                break;
            }
        }
        if (!ok) break;
    }

    if (ok) {
        printf("      ===> particionamento local CORRETO no rank %d\n", rank);
    } else {
        printf("      ===> particionamento local COM ERROS no rank %d\n", rank);
    }
}

// -----------------------------------------------------------------------------
// Programa Principal MPI
// -----------------------------------------------------------------------------

int main(int argc, char **argv) {
    int rank, nprocs;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    int usar_verify = 0;
    if (argc < 6 || argc > 7) {
        if (rank == 0) {
            fprintf(stderr, "Uso: mpirun -np n %s <nelements> <npivots> <nbins> <nthreads> <nr> [-Verify]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    if (argc == 7) {
        if (strcasecmp(argv[6], "-verify") != 0) {
            if (rank == 0) fprintf(stderr, "Parametro invalido: %s\n", argv[6]);
            MPI_Finalize();
            return 1;
        }
        usar_verify = 1;
    }

    long long nTotalElements = atoll(argv[1]);
    int npivots = atoi(argv[2]);
    int nbins = atoi(argv[3]);
    int nthreads = atoi(argv[4]);
    int nr = atoi(argv[5]);

    if (nTotalElements <= 0 || npivots < 2 || npivots < nbins || npivots > nTotalElements || 
        nbins <= 0 || nthreads <= 0 || nr <= 0 || nTotalElements % nprocs != 0) {
        if (rank == 0) {
            fprintf(stderr, "Parametros invalidos ou nelements nao eh divisivel por np.\n");
        }
        MPI_Finalize();
        return 1;
    }

    // Inicialização da semente conforme diretriz do trabalho
    srand(2025 * 100 + rank);

    long long local_nElements = nTotalElements / nprocs;
    int local_npivots = npivots / nprocs;
    int total_npivots_gathered = local_npivots * nprocs;

    // Alocações locais
    long long *local_Input = malloc(local_nElements * sizeof(long long));
    long long *local_Output = malloc(local_nElements * sizeof(long long));
    long long *local_Pos = malloc(nbins * sizeof(long long));
    long long *local_Pivots = malloc(local_npivots * sizeof(long long));
    long long *Limits = malloc((nbins + 1) * sizeof(long long));
    
    char *buffer_limpeza = malloc(TAMANHO_BUFFER_LIMPEZA);
    memset(buffer_limpeza, 0xAA, TAMANHO_BUFFER_LIMPEZA);

    // Alocações globais (Apenas no Rank 0 para o Baseline)
    long long *global_Input = NULL;
    long long *global_Output = NULL;
    long long *global_Pos = NULL;
    long long *global_Pivots = NULL;

    if (rank == 0) {
        global_Input = malloc(nTotalElements * sizeof(long long));
        global_Output = malloc(nTotalElements * sizeof(long long));
        global_Pos = malloc(nbins * sizeof(long long));
        global_Pivots = malloc(total_npivots_gathered * sizeof(long long));
    }

    if (pool_init(nthreads, nbins) != 0) {
        fprintf(stderr, "Erro na alocacao do pool rank %d.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    double total_part_ser = 0.0;
    double total_part_par = 0.0;

    // Cronômetros
    chronometer_t chrono_ser, chrono_par;

    if (rank == 0) {
        printf("\n=== MPI+Pthreads Multipartition ===\n");
        printf("  Total Elements : %lld\n", nTotalElements);
        printf("  MPI Processes  : %d\n", nprocs);
        printf("  Threads / Proc : %d\n", nthreads);
        printf("  Pivots         : %d\n", total_npivots_gathered);
        printf("  Bins           : %d\n", nbins);
        printf("  Rounds         : %d\n", nr);
        
        printf("\n  Round ;  T(part-ser) s ;  T(np:%d x nth:%d) s ;    Speedup\n", nprocs, nthreads);
        printf("  ----- ; -------------- ; ------------------- ; ----------\n");
    }

    for (int round = 1; round <= nr; round++) {
        // 1. Geração de Dados Aleatórios Locais
        for (long long i = 0; i < local_nElements; i++) {
            local_Input[i] = rand64();
        }

        MPI_Barrier(MPI_COMM_WORLD);

        // 2. Coleta de Pivôs para Geração de Limites Globais
        for (int i = 0; i < local_npivots; i++) {
            long long jitter = rand63() % local_nElements;
            local_Pivots[i] = local_Input[jitter];
        }

        MPI_Gather(local_Pivots, local_npivots, MPI_LONG_LONG,
                   global_Pivots, local_npivots, MPI_LONG_LONG,
                   0, MPI_COMM_WORLD);

        if (rank == 0) {
            build_limits_sp2_serial(global_Pivots, total_npivots_gathered, total_npivots_gathered, nbins, global_Pivots, Limits);
        }

        // 3. Compartilhando limites calculados
        MPI_Bcast(Limits, nbins + 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);

        // 4. Teste Serial (Rank 0 puxa todos os dados e particiona usando o chronos)
        MPI_Gather(local_Input, local_nElements, MPI_LONG_LONG,
                   global_Input, local_nElements, MPI_LONG_LONG,
                   0, MPI_COMM_WORLD);

        double t_ser = 0.0;
        if (rank == 0) {
            limpar_cache(buffer_limpeza, TAMANHO_BUFFER_LIMPEZA);
            
            chrono_reset(&chrono_ser);
            chrono_start(&chrono_ser);
            
            parallel_multiPartition(global_Input, global_Output, nTotalElements, Limits, nbins, global_Pos, 1);
            
            chrono_stop(&chrono_ser);
            t_ser = (double) chrono_gettotal(&chrono_ser) / 1000000000.0; // Converte nanossegundos para segundos
            total_part_ser += t_ser;
        }

        // Sincroniza processos antes do código paralelo
        MPI_Barrier(MPI_COMM_WORLD);
        limpar_cache(buffer_limpeza, TAMANHO_BUFFER_LIMPEZA);

        // 5. Teste Paralelo Híbrido (MPI + Pthreads)
        MPI_Barrier(MPI_COMM_WORLD);
        
        chrono_reset(&chrono_par);
        chrono_start(&chrono_par);
        
        parallel_multiPartition(local_Input, local_Output, local_nElements, Limits, nbins, local_Pos, nthreads);
        
        chrono_stop(&chrono_par);
        double t_par = (double) chrono_gettotal(&chrono_par) / 1000000000.0;

        // Verificação de Corretude
        if (usar_verify) {
            verifica_particoesLocais(local_Input, local_Output, local_nElements, Limits, nbins, local_Pos, nthreads);
        }

        // Reduce para obter o pior tempo paralelo entre os processos
        double max_t_par = 0.0;
        MPI_Reduce(&t_par, &max_t_par, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            total_part_par += max_t_par;
            double speedup = t_ser / max_t_par;
            printf("  %-5d ; %14.6f ; %19.6f ; %10.3f\n", round, t_ser, max_t_par, speedup);
        }
    }

    if (rank == 0) {
        printf("  ----- ; -------------- ; ------------------- ; ----------\n");
        double avg_ser = total_part_ser / nr;
        double avg_par = total_part_par / nr;
        double avg_speedup = avg_ser / avg_par;
        double meps_ser = (nTotalElements / 1e6) / avg_ser;
        double meps_par = (nTotalElements / 1e6) / avg_par;
        double eficiencia = (avg_speedup / (nprocs * nthreads)) * 100.0;

        printf("  AVG   ; %14.6f ; %19.6f ; %10.3f\n\n", avg_ser, avg_par, avg_speedup);
        
        printf("=== Summary ===\n");
        printf("  Avg T(part-ser)         : %.6f s (%.2f MEPS)\n", avg_ser, meps_ser);
        printf("  Avg T(np:%d x nth:%d)     : %.6f s (%.2f MEPS)\n", nprocs, nthreads, avg_par, meps_par);
        printf("  Avg Speedup             : %.3fx\n", avg_speedup);
        printf("  Parallel efficiency     : %5.1f%%\n", eficiencia);
        printf("  Global Correctness      : VERIFY VIA ARGS\n\n");
    }

    // Libera recursos
    pool_destroy(nthreads);
    free(local_Input);
    free(local_Output);
    free(local_Pos);
    free(local_Pivots);
    free(Limits);
    free(buffer_limpeza);

    if (rank == 0) {
        free(global_Input);
        free(global_Output);
        free(global_Pos);
        free(global_Pivots);
    }

    MPI_Finalize();
    return 0;
}