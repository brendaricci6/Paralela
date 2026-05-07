#define _POSIX_C_SOURCE 199309L

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TAMANHO_CACHE_LLC (16 * 1024 * 1024)
#define TAMANHO_BUFFER_LIMPEZA (3 * TAMANHO_CACHE_LLC)

//funções de operações randômicas
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

//inverte valores de duas variáveis
static inline void ll_swap(long long *a, long long *b) {
    long long t = *a;
    *a = *b;
    *b = t;
}

//captura o tempo do sistema
static double obter_tempo_atual(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

//percorre o buffer de 64 em 64 bytes fazendo limpeza de cache
static void limpar_cache(char *buffer, size_t tamanho) {
    volatile long long soma = 0;

    for (size_t i = 0; i < tamanho; i += 64)
        soma += buffer[i];

    (void)soma;
}

//comparação para o qsort
static int comparar_long_long(const void *a, const void *b) {
    long long x = *(const long long *)a;
    long long y = *(const long long *)b;

    if (x < y) return -1;
    if (x > y) return 1;

    return 0;
}

// funções de teste de dados --------------------------------------------------------------------------------------------

//gera massa de testes 
static void gen_test_data_balanced2(long long *data, long long nelements, int nbins){
    if (nelements <= 0 || nbins <= 0)
        return;
    //preenche vetor com números sequenciais 
    for (long long i = 0; i < nelements; i++)
        data[i] = i;

    //embaralha
    for (long long i = nelements - 1; i > 0; i--) {

        long long j =
            (long long)(rand63() %
            (unsigned long long)(i + 1));

        ll_swap(&data[i], &data[j]);
    }
    //garante que os números fiquem uniformemente distribuídos
    for (long long i = 0; i < nelements; i++)
        data[i] %= nbins;
}

//define os limites de cada bin do histograma 
static void build_limits_sp2_serial(
    const long long *Input,
    long long n,
    int npivots,
    int nbins,
    long long *pivots,
    long long *limits)
{
    long long stride = n / npivots;

    if (stride <= 0)
        stride = 1;

    for (int i = 0; i < npivots; i++) {

        long long jitter =
            (long long)(rand63() %
            (unsigned long long)stride);

        long long idx = i * stride + jitter;

        if (idx >= n)
            idx = n - 1;

        pivots[i] = Input[idx];
    }

    qsort(pivots, npivots, sizeof(long long), comparar_long_long);

    limits[0] = LLONG_MIN;
    limits[nbins] = LLONG_MAX;

    for (int i = 1; i < nbins; i++) {

        int idx =
            (int)(((long long)i * (npivots - 1)) / nbins);

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

//decide em qual bin um determinado valor deve entrar
static inline int find_bin(long long v,
                           const long long *limits,
                           int nbins)
{
    if (v >= limits[nbins - 1])
        return nbins - 1;

    if (v < limits[1])
        return 0;

    int left = 0;
    int right = nbins - 1;

    while (left <= right) {

        int mid = left + (right - left) / 2;

        if (v >= limits[mid]) {

            if (v < limits[mid + 1])
                return mid;

            left = mid + 1;
        }
        else {
            right = mid - 1;
        }
    }

    return 0;
}

// criação de threads

typedef struct {
    int tid;
    const long long *data;
    const long long *limits;
    long long nelements;
    int nbins;
    int nthreads;
    long long *hist_local;
} ThreadData;

static pthread_t *threads = NULL;
static ThreadData *td = NULL;

static pthread_barrier_t barrier;

static volatile int shutdown_pool = 0;
static volatile int work_ready = 0;

static long long *global_hist = NULL;

static void worker_run(int tid){
    ThreadData *t = &td[tid];

    memset(t->hist_local, 0, t->nbins * sizeof(long long));

    long long start = (tid * t->nelements) / t->nthreads;

    long long end = ((tid + 1) * t->nelements) / t->nthreads;

    for (long long i = start; i < end; i++) {

        int b = find_bin(t->data[i], t->limits, t->nbins);

        t->hist_local[b]++;
    }
}

static void *worker_thread(void *arg){
    int tid = *(int *)arg;

    while (1) {

        pthread_barrier_wait(&barrier);

        if (shutdown_pool)
            break;

        if (work_ready)
            worker_run(tid);

        pthread_barrier_wait(&barrier);
    }

    return NULL;
}

static int pool_init(int nthreads, int nbins){
    threads = malloc((nthreads - 1) * sizeof(pthread_t));

    td = malloc(nthreads * sizeof(ThreadData));

    if (!td)
        return -1;

    pthread_barrier_init(&barrier, NULL, nthreads);

    for (int t = 0; t < nthreads; t++) {
        td[t].tid = t; 
        td[t].hist_local = calloc(nbins, sizeof(long long));

        if (!td[t].hist_local)
            return -1;
    }

    for (int t = 1; t < nthreads; t++) {

        int *arg = malloc(sizeof(int));
        *arg = t;

        if (pthread_create(&threads[t - 1], NULL, worker_thread, arg) != 0)
            return -1;
    }

    return 0;
}

static void pool_destroy(int nthreads)
{
    shutdown_pool = 1;

    pthread_barrier_wait(&barrier);

    for (int t = 1; t < nthreads; t++)
        pthread_join(threads[t - 1], NULL);

    for (int t = 0; t < nthreads; t++)
        free(td[t].hist_local);

    free(td);
    free(threads);

    pthread_barrier_destroy(&barrier);
}


//FUNÇÃO PRINCIPAL
int parallel_histogram( const long long *data, long long nelements, const long long *limits, int nbins, long long *hist, int nthreads){
    memset(hist, 0, nbins * sizeof(long long));

    if (nthreads == 1) {

        for (long long i = 0; i < nelements; i++) {
            int b =
                find_bin(data[i], limits, nbins);
            hist[b]++;
        }

        return 0;
    }

    global_hist = hist;

    for (int t = 0; t < nthreads; t++) {
        td[t].data = data;
        td[t].limits = limits;
        td[t].nelements = nelements;
        td[t].nbins = nbins;
        td[t].nthreads = nthreads;
    }

    work_ready = 1;

    pthread_barrier_wait(&barrier);
    worker_run(0);
    pthread_barrier_wait(&barrier);
    work_ready = 0;

    for (int t = 0; t < nthreads; t++) {
        for (int b = 0; b < nbins; b++)
            hist[b] += td[t].hist_local[b];
    }

    return 0;
}

//verifica se o resultado esta correto
static int verify_histogram(const long long *data,long long nelements,const long long *limits,int nbins,const long long *hist_1thr,const long long *hist_nthr){
    int s1_ok = 1;

    for (int b = 0; b < nbins; b++) {

        if (hist_1thr[b] != hist_nthr[b]) {

            fprintf(stderr,
                    "  VERIFY FAIL stage1: "
                    "bin %d  1thr=%lld  Nthr=%lld\n",
                    b,
                    hist_1thr[b],
                    hist_nthr[b]);

            s1_ok = 0;
        }
    }

    if (!s1_ok)
        return 0;

    long long *recount =
        calloc(nbins, sizeof(long long));

    if (!recount)
        return 0;

    for (long long i = 0; i < nelements; i++) {

        long long v = data[i];

        int b = 0;

        while (b < nbins - 1 &&
               v >= limits[b + 1])
            b++;

        recount[b]++;
    }

    int s2_ok = 1;

    for (int b = 0; b < nbins; b++) {

        if (recount[b] != hist_nthr[b]) {

            fprintf(stderr,
                    "  VERIFY FAIL stage2: "
                    "bin %d recount=%lld "
                    "Nthr=%lld\n",
                    b,
                    recount[b],
                    hist_nthr[b]);

            s2_ok = 0;
        }
    }

    free(recount);

    if (!s2_ok)
        return 0;

    long long total = 0;

    for (int b = 0; b < nbins; b++)
        total += hist_nthr[b];

    if (total != nelements) {

        fprintf(stderr,
                "  VERIFY FAIL stage3: "
                "sum=%lld expected=%lld\n",
                total,
                nelements);

        return 0;
    }

    return 1;
}



int main(int argc, char **argv)
{
    int usar_tb2 = 0;

    if (argc != 6 && argc != 7) {

        fprintf(stderr, "Uso:\n" "%s <nelements> " "<npivots> "
                "<nbins> "
                "<nthreads> "
                "<nr> [-tb2]\n",
                argv[0]);

        return 1;
    }

    if (argc == 7) {

        if (strcmp(argv[6], "-tb2") != 0) {

            fprintf(stderr,
                    "Parametro invalido: %s\n",
                    argv[6]);

            return 1;
        }

        usar_tb2 = 1;
    }

    long long nelements = atoll(argv[1]);

    int npivots = atoi(argv[2]);
    int nbins = atoi(argv[3]);
    int nthreads = atoi(argv[4]);
    int nr = atoi(argv[5]);

    if (nelements <= 0 ||
        npivots < 2 ||
        npivots < nbins ||
        npivots > nelements ||
        nbins <= 0 ||
        nthreads <= 0 ||
        nr <= 0) {

        fprintf(stderr,
                "Parametros invalidos.\n");

        return 1;
    }

    srand((unsigned)time(NULL));

    long long *data =
        malloc(nelements * sizeof(long long));

    long long *data2 =
        malloc(nelements * sizeof(long long));

    long long *pivots =
        malloc(npivots * sizeof(long long));

    long long *limits =
        malloc((nbins + 1) * sizeof(long long));

    long long *hist_1thr =
        malloc(nbins * sizeof(long long));

    long long *hist_nthr =
        malloc(nbins * sizeof(long long));

    char *buffer_limpeza =
        malloc(TAMANHO_BUFFER_LIMPEZA);

    if (!data || !data2 || !pivots ||
        !limits || !hist_1thr ||
        !hist_nthr || !buffer_limpeza) {

        fprintf(stderr,
                "Erro de alocacao.\n");

        return 1;
    }

    memset(buffer_limpeza,
           0xAA,
           TAMANHO_BUFFER_LIMPEZA);

    if (pool_init(nthreads, nbins) != 0) {

        fprintf(stderr,
                "Erro no pool.\n");

        return 1;
    }

    double total_bl = 0.0;
    double total_1t = 0.0;
    double total_nt = 0.0;

    int ok_global = 1;

    printf("\n=== Parallel Histogram — "
           "Scalability Test "
           "(Persistent Thread Pool) ===\n");

    printf("  Elements : %lld"
           "  |  Pivots : %d"
           "  |  Bins : %d"
           "  |  Threads : %d"
           "  |  Rounds : %d"
           "  |  Input : %s\n",
           nelements,
           npivots,
           nbins,
           nthreads,
           nr,
           usar_tb2
             ? "uniform random in [0,nbins) (-tb2)"
             : "random int64");

    printf("  LLC size : %d MiB"
           "  |  Eviction buffer : %d MiB\n\n",
           TAMANHO_CACHE_LLC / (1024 * 1024),
           TAMANHO_BUFFER_LIMPEZA / (1024 * 1024));

    for (int round = 1; round <= nr; round++) {

        if (usar_tb2)
            gen_test_data_balanced2(
                data,
                nelements,
                nbins);
        else {

            for (long long i = 0;
                 i < nelements;
                 i++)
                data[i] = rand64();
        }

        memcpy(data2,
               data,
               nelements * sizeof(long long));

        double t0 = obter_tempo_atual();

        build_limits_sp2_serial(
            data,
            nelements,
            npivots,
            nbins,
            pivots,
            limits);

        double tbl =
            obter_tempo_atual() - t0;

        total_bl += tbl;

        limpar_cache(buffer_limpeza,
                      TAMANHO_BUFFER_LIMPEZA);

        t0 = obter_tempo_atual();

        parallel_histogram(
            data,
            nelements,
            limits,
            nbins,
            hist_1thr,
            1);

        double t1 =
            obter_tempo_atual() - t0;

        total_1t += t1;

        limpar_cache(buffer_limpeza,
                      TAMANHO_BUFFER_LIMPEZA);

        t0 = obter_tempo_atual();

        parallel_histogram(
            data2,
            nelements,
            limits,
            nbins,
            hist_nthr,
            nthreads);

        double tn =
            obter_tempo_atual() - t0;

        total_nt += tn;

        int ok =
            verify_histogram(
                data,
                nelements,
                limits,
                nbins,
                hist_1thr,
                hist_nthr);

        if (!ok)
            ok_global = 0;

        double speedup = t1 / tn;

        if (round == 1) {

            printf("  --- Round 1: "
                   "first 8 partitions ---\n");

            printf("   Bin  ;"
                   "          Lo (inclusive)"
                   "  ;"
                   "          Hi (exclusive)"
                   "  ;"
                   "         Count\n");

            int maxshow =
                (nbins < 8)
                ? nbins
                : 8;

            for (int b = 0;
                 b < maxshow;
                 b++) {

                printf("%6d  ;"
                       "%24lld  ;"
                       "%24lld  ;"
                       "%14lld\n",
                       b,
                       limits[b],
                       limits[b + 1],
                       hist_1thr[b]);
            }

            printf("\n");
        }

        if (round == 1) {

            printf("  Round ;"
                   "  T(bl_ser) s ;"
                   "  T(1 thr) s ;"
                   "   T(N thr) s ;"
                   "    Speedup ; OK?\n");

            printf("  ----- ;"
                   "------------ ;"
                   "------------ ;"
                   "------------ ;"
                   "------------ ;"
                   " ----\n");
        }

        printf("  %-5d ;"
               " %11.6f ;"
               " %11.6f ;"
               " %11.6f ;"
               " %11.3f ; %s\n",
               round,
               tbl,
               t1,
               tn,
               speedup,
               ok ? "OK" : "FAIL");
    }

    printf("  ----- ;"
           "------------ ;"
           "------------ ;"
           "------------ ;"
           "------------ ;"
           " ----\n");

    double avg_bl = total_bl / nr;
    double avg_1t = total_1t / nr;
    double avg_nt = total_nt / nr;

    double avg_speedup =
        avg_1t / avg_nt;

    printf("  AVG   ;"
           " %11.6f ;"
           " %11.6f ;"
           " %11.6f ;"
           " %11.3f ; %s\n",
           avg_bl,
           avg_1t,
           avg_nt,
           avg_speedup,
           ok_global ? "OK" : "FAIL");

    double meps1 =
        (nelements / 1e6) / avg_1t;

    double mepsn =
        (nelements / 1e6) / avg_nt;

    double eficiencia =
        (avg_speedup / nthreads) * 100.0;

    printf("\n=== Summary ===\n");

    printf("  Avg build_limits serial"
           "   : %.6f ; s\n",
           avg_bl);

    printf("  Avg time  (1 thread )"
           "     : %.6f ; s ; %.2f ; MEPS\n",
           avg_1t,
           meps1);

    printf("  Avg time  (%d threads)"
           "    : %.6f ; s ; %.2f ; MEPS\n",
           nthreads,
           avg_nt,
           mepsn);

    printf("  Avg histogram speedup"
           "     : %.3fx\n",
           avg_speedup);

    printf("\n");

    printf("  Parallel efficiency:\n");

    printf("    with nthreads "
           "  (%2d) : %5.1f%%\n",
           nthreads,
           eficiencia);

    printf("\n");

    printf("  Overall correctness"
           "       : %s\n\n",
           ok_global
             ? "PASS"
             : "FAIL");

    pool_destroy(nthreads);

    free(data);
    free(data2);
    free(pivots);
    free(limits);
    free(hist_1thr);
    free(hist_nthr);
    free(buffer_limpeza);

    return 0;
}