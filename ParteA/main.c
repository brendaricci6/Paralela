//suporte a v0.2

#define _POSIX_C_SOURCE 199309L
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Tamanho aproximado da L3 cache (16MB padrão para garantir limpeza/evicção em
// processadores comuns)
#define TAMANHO_CACHE_LLC (16 * 1024 * 1024)
#define TAMANHO_BUFFER_LIMPEZA (3 * TAMANHO_CACHE_LLC)

//!MUDANÇA! rand63()
static inline unsigned long long rand63(void){
    return ((unsigned long long)(unsigned)rand())
         | ((unsigned long long)(unsigned)rand() << 21)
         | ((unsigned long long)(unsigned)rand() << 42);
}

//!MUDANÇA! rand64()
static inline long long rand64(void){
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

//!NUDANÇA! função obrigatória
static inline void ll_swap(long long *a, long long *b){
    long long t = *a;
    *a = *b;
    *b = t;
}

//!MUDANÇA! função obrigatória
static void gen_test_data_balanced2(long long *data, long long num_elementos, int nbins){
    if (num_elementos <= 0 || nbins <= 0)
        return;

    for (long long i = 0; i < num_elementos; i++)
        data[i] = i;

    for (long long i = num_elementos - 1; i > 0; i--) {
        long long j =
            (long long)(rand63() % (unsigned long long)(i + 1));
        ll_swap(&data[i], &data[j]);
    }

    for (long long i = 0; i < num_elementos; i++)
        data[i] %= nbins;
}

//struct p enviar os dados para as threads filhas 
struct DadosThread {
  const long long *dados;
  const long long *limites;
  int num_bins;
  long long inicio;
  long long fim;
  long long *histograma_local;
};

//obtém o tempo atual em s
double obter_tempo_atual() {
  struct timespec tempo;
  clock_gettime(CLOCK_MONOTONIC, &tempo);
  return tempo.tv_sec + tempo.tv_nsec * 1e-9;
}
/*
// Gera um número de 64-bits cobrindo todo o intervalo possível de 'long long'
// (positivos e negativos)
long long gerar_numero_aleatorio() {
  unsigned long long p1 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long p2 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long p3 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long p4 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long resultado = (p1 << 48) | (p2 << 32) | (p3 << 16) | p4;
  return (long long)resultado;
}*/

// Garante que o Cache está frio (limpa o cache L3 lendo um buffer gigante)
void limpar_cache(char *buffer, size_t tamanho) {
  long long soma = 0;
  // Pula em saltos de 64 bytes (o tamanho padrão de 1 linha de cache)
  for (size_t i = 0; i < tamanho; i += 64) {
    soma += buffer[i];
  }
  // "volatile" impede que o compilador otimize e ignore este cálculo
  volatile long long lixo = soma;
  (void)lixo;
}

// Função comparadora para ordenar os pivôs do menor para o maior com 'qsort'
int comparar_long_long(const void *a, const void *b) {
  long long num1 = *(const long long *)a;
  long long num2 = *(const long long *)b;
  if (num1 < num2)
    return -1;
  if (num1 > num2)
    return 1;
  return 0;
}


/*
// Passo 2 - Constrói os limites (Array que define onde cada "caixa/bin" do
// histograma começa e termina)
void construir_limites(const long long *dados, long long num_elementos,
                       int num_pivos, int num_bins, long long *limites) {
  long long *pivos = (long long *)malloc(num_pivos * sizeof(long long));
  if (!pivos) {
    fprintf(stderr, "Erro ao alocar memória para os pivôs.\n");
    exit(1);
  }

  // Amostragem de pivôs usando o conceito de Jitter (deslocamento aleatório)
  long long salto = num_elementos / num_pivos;
  for (int i = 0; i < num_pivos; i++) {
    long long desvio_aleatorio = rand() % salto;
    pivos[i] = dados[i * salto + desvio_aleatorio];
  }

  // Ordena os pivôs para podermos fatiar em limites
  qsort(pivos, num_pivos, sizeof(long long), comparar_long_long);

  // Seleciona os limites espaçados igualmente e aplica o "empurrão suave" se
  // precisar
  for (int i = 0; i <= num_bins; i++) {
    int indice = (int)(((long long)i * (num_pivos - 1)) / num_bins);
    limites[i] = pivos[indice];
  }

  // Regra do Empurrão Suave: força limites adjacentes a serem estritamente
  // crescentes
  for (int k = 1; k <= num_bins; k++) {
    if (limites[k] <= limites[k - 1]) {
      limites[k] = limites[k - 1] + 1;
    }
  }

  free(pivos);
} */

//!MUDANÇA! anexo 5
static void build_limits_sp2_serial(const long long *Input, long long n, int npivots, int nbins, long long *pivots, long long *limits){
    long long stride = n / npivots;

    if (stride <= 0)
        stride = 1;

    //usa rand63()
    for (int i = 0; i < npivots; i++) {
        long long jitter =
            (long long)(rand63() % (unsigned long long)stride);

        pivots[i] = Input[i * stride + jitter];
    }

    qsort(pivots, npivots, sizeof(long long), comparar_ll);

    //novos extremos 
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


// Busca binária super rápida para descobrir em qual caixa o número deve entrar
static inline int encontrar_bin(long long valor, const long long *limites, int num_bins) {
  // Se for maior que o último limite, cai na última caixa
  if (valor >= limites[num_bins - 1])
    return num_bins - 1;
  // Se for menor que o primeiro limite válido, cai na primeira caixa
  if (valor < limites[1])
    return 0;

  int esquerda = 0;
  int direita = num_bins - 1;

  while (esquerda <= direita) {
    int meio = esquerda + (direita - esquerda) / 2;
    if (valor >= limites[meio]) {
      if (valor < limites[meio + 1]) {
        return meio; // Encontrou a caixa correta
      } else {
        esquerda = meio + 1;
      }
    } else {
      direita = meio - 1;
    }
  }
  return 0; // Garantia contra falhas (fallback)
}

// thread percorre sua fatia e soma no seu histograma privado
void *tarefa_da_thread(void *argumentos) {
  struct DadosThread *dados_thread = (struct DadosThread *)argumentos;
  for (long long i = dados_thread->inicio; i < dados_thread->fim; i++) {
    int bin_correto = encontrar_bin(
        dados_thread->dados[i], dados_thread->limites, dados_thread->num_bins);
    dados_thread->histograma_local[bin_correto]++;
  }
  return NULL;
}

// função histograma Paralelo
int parallel_histogram(const long long *dados, long long num_elementos,
                       const long long *limites, int num_bins,
                       long long *histograma_saida, int num_threads) {
  for (int i = 0; i < num_bins; i++) {
    histograma_saida[i] = 0;
  }

  // Se1 thread, roda serial normal 
  if (num_threads == 1) {
    for (long long i = 0; i < num_elementos; i++) {
      int bin_correto = encontrar_bin(dados[i], limites, num_bins);
      histograma_saida[bin_correto]++;
    }
    return 0;
  }

  // Execução paralela
  pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
  struct DadosThread *info_threads =
      (struct DadosThread *)malloc(num_threads * sizeof(struct DadosThread));
  if (!threads || !info_threads)
    return -1;

  // Calcula o tamanho da fatia de cada thread e se tem alguma sobra
  long long tamanho_fatia = num_elementos / num_threads;
  long long sobra = num_elementos % num_threads;
  long long atual_inicio = 0;

  // Dispara N-1 threads (deixaremos a última fatia para a "Main" thread)
  for (int i = 0; i < num_threads - 1; i++) {
    info_threads[i].dados = dados;
    info_threads[i].limites = limites;
    info_threads[i].num_bins = num_bins;
    info_threads[i].histograma_local =
        (long long *)calloc(num_bins, sizeof(long long));
    if (!info_threads[i].histograma_local)
      return -1;

    info_threads[i].inicio = atual_inicio;
    // Distribui a "sobra" entre as primeiras threads
    long long extra = (i < sobra) ? 1 : 0;
    info_threads[i].fim = atual_inicio + tamanho_fatia + extra;
    atual_inicio = info_threads[i].fim;

 //!MUDANÇA!
    for (int t = 0; t < nthreads - 1; t++) {

        //!MUDANÇA! checa pthread_create
        if (pthread_create(
                &threads[t],
                NULL,
                worker,
                &info[t]) != 0)
            return -1;
    }

    worker(&info[nthreads - 1]);

    for (int t = 0; t < nthreads - 1; t++)
        pthread_join(threads[t], NULL);

    for (int t = 0; t < nthreads; t++) {

        for (int b = 0; b < nbins; b++)
            hist[b] += info[t].hist_local[b];

        free(info[t].hist_local);
    }

    free(threads);
    free(info);

    return 0;
}

//!MUDANÇA! verify_histogram oficial 
static int verify_histogram(
    const long long *data,
    long long num_elementos,
    const long long *limits,
    int nbins,
    const long long *hist_1thr,
    const long long *hist_nthr)
{
    int s1_ok = 1;

    for (int b = 0; b < nbins; b++) {

        if (hist_1thr[b] != hist_nthr[b]) {

            fprintf(stderr,
                    "VERIFY FAIL stage1: bin %d "
                    "1thr=%lld Nthr=%lld\n",
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

    for (long long i = 0; i < num_elementos; i++) {

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
                    "VERIFY FAIL stage2: "
                    "bin %d recount=%lld Nthr=%lld\n",
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

    if (total != num_elementos) {

        fprintf(stderr,
                "VERIFY FAIL stage3: "
                "sum=%lld expected=%lld\n",
                total,
                num_elementos);

        return 0;
    }

    return 1;
}

int main(int argc, char **argv) {

    //!MUDANÇA! para identificar o -tb2
    int usar_tb2 = 0;

    if (argc != 6 && argc != 7) {

        fprintf(stderr,
                "Uso: %s "
                "<nelementos> <npivos> "
                "<nbins> <nthreads> <nr> [-tb2]\n",
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

  long long num_elementos = atoll(argv[1]); // n elementos
  int num_pivos = atoi(argv[2]);            // n pivots
  int num_bins = atoi(argv[3]);             // nbins
  int num_threads = atoi(argv[4]);          // nthreads
  int num_repeticoes = atoi(argv[5]);       // nr

  // Checagem das restrições do enunciado
  if (num_elementos <= 0) {
    printf("Quantidade de elementos inválida!\n");
    return 1;
  }
  if (num_pivos < 2 || num_pivos < num_bins || num_pivos > num_elementos) {
    printf("Pivôs inválidos!\n");
    return 1;
  }
  if (num_bins <= 0) {
    printf("Número de Bins inválido!\n");
    return 1;
  }
  if (num_threads <= 0) {
    printf("Número de Threads inválido!\n");
    return 1;
  }
  if (num_repeticoes <= 0) {
    printf("Número de Repetições inválido!\n");
    return 1;
  }

  // Cria o buffer de limpeza
  char *buffer_limpeza = (char *)malloc(TAMANHO_BUFFER_LIMPEZA);
  if (!buffer_limpeza) {
    printf("Erro alocando buffer para limpar cache.\n");
    return 1;
  }
  memset(buffer_limpeza, 0xAA, TAMANHO_BUFFER_LIMPEZA);

  //aloca memórias necessárias
  long long *dados = (long long *)malloc(num_elementos * sizeof(long long));

  long long *dados2 = (long long *)malloc(num_elementos * sizeof(long long)); // data2 (cópia)

  long long *limites = (long long *)malloc((num_bins + 1) * sizeof(long long));

  long long *histograma_serial = (long long *)malloc(num_bins * sizeof(long long));

  long long *histograma_paralelo = (long long *)malloc(num_bins * sizeof(long long));

  if (!dados || !dados2 || !limites || !histograma_serial ||
      !histograma_paralelo) {
    printf("Erro crasso de alocação de memória.\n");
    return 1;
  }

  srand((unsigned)time(NULL));

  // Variáveis para somar médias
  double total_tempo_limites = 0, total_tempo_serial = 0, total_tempo_paralelo = 0;
  int resultado_global_ok = 1;

    //!MUDANÇA! cabeçalho novo
    printf("\n=== Parallel Histogram — "
           "Scalability Test "
           "(Persistent Thread Pool) ===\n");

    printf("  Elements : %lld"
           "  |  Pivots : %d"
           "  |  Bins : %d"
           "  |  Threads : %d"
           "  |  Rounds : %d"
           "  |  Input : %s\n",
           num_elementos,
           num_pivos,
           num_bins,
           num_threads,
           num_repeticoes,
           usar_tb2
               ? "uniform random in [0,nbins) (-tb2)"
               : "random int64");

    printf("  LLC size : %d MiB"
           "  |  Eviction buffer : %d MiB\n\n",
           TAMANHO_CACHE_LLC / (1024 * 1024),
           TAMANHO_BUFFER_LIMPEZA / (1024 * 1024));

    printf("  Round ;  T(bl_ser) s ; "
           " T(1 thr) s ;  T(N thr) s ; "
           " Speedup ; OK?\n");

    printf("  ----- ;------------ ;"
           "------------ ;------------ ;"
           "---------- ; ----\n");


  // Roda os testes as X repetições pedidas
  for (int rodada = 1; rodada <= num_repeticoes; rodada++) {

    // Passo 1: Geração dos números aleatórios
    //!MUDANÇA! desgraça do-tb2
    if (usar_tb2) {
        gen_test_data_balanced2(data, num_elementos, nbins);
        }
        else {
            //!MUDANÇA!rand64()
            for (long long i = 0; i < num_elementos; i++)
                data[i] = rand64();
        }

    // Clona rapidamente o array para a variável isolada data2
    memcpy(dados2, dados, num_elementos * sizeof(long long));

    // Passo 2: Construção dos limites e medição de tempo
    double inicio_limites = obter_tempo_atual();
    construir_limites(dados, num_elementos, num_pivos, num_bins, limites);
    double tempo_limites = obter_tempo_atual() - inicio_limites;
    total_tempo_limites += tempo_limites;

    // Passo 3: Força a limpeza do L3 cache
    limpar_cache(buffer_limpeza, TAMANHO_BUFFER_LIMPEZA);

    // Passo 4: Executa o histograma com 1 Thread em cima de 'dados'
    double inicio_serial = obter_tempo_atual();
    parallel_histogram(dados, num_elementos, limites, num_bins,
                       histograma_serial, 1);
    double tempo_serial = obter_tempo_atual() - inicio_serial;
    total_tempo_serial += tempo_serial;

    // Limpa o cache DE NOVO para a corrida das Threads ser justa
    limpar_cache(buffer_limpeza, TAMANHO_BUFFER_LIMPEZA);

    // Passo 5: Executa com N threads em cima de 'dados2' (cópia isolada)
    double inicio_paralelo = obter_tempo_atual();
    parallel_histogram(dados2, num_elementos, limites, num_bins, histograma_paralelo, num_threads);
    double tempo_paralelo = obter_tempo_atual() - inicio_paralelo;
    total_tempo_paralelo += tempo_paralelo;





    // Passo 6: Verificação de Corretude (confirma se as threads não corromperam
    // dados)
    int rodada_ok = 1;
    for (int b = 0; b < num_bins; b++) {
      if (histograma_serial[b] != histograma_paralelo[b]) {
        rodada_ok = 0;
        resultado_global_ok = 0;
        break;
      }
    }

    //!MUDANÇA! faz verificação
        int ok =
            verify_histogram(
                data,
                num_elementos,
                limits,
                num_bins,
                hist1,
                histn);

        if (!ok)
            ok_global = 0;

    // Calcula a aceleração tida pelas threads
    double speedup = tempo_serial / tempo_paralelo;

    // Imprime a tabela dessa rodada
    printf("%5d | %9.4f | %8.4f | %8.4f | %7.2f | %s\n", rodada, tempo_limites,
           tempo_serial, tempo_paralelo, speedup, rodada_ok ? "OK" : "FAIL");

    // Regra do enunciado: "Apenas no round 1, exibir os primeiros 8 Bins"
    if (rodada == 1) {
      printf("\n--- Rodada 1: Primeiros 8 Bins ---\n");
      int limite_mostrar = num_bins < 8 ? num_bins : 8;
      for (int b = 0; b < limite_mostrar; b++) {
        printf("Bin %2d: [%20lld, %20lld) | Contagem: %10lld\n", b, limites[b],
               limites[b + 1], histograma_serial[b]);
      }
      printf("----------------------------------\n\n");
    }
  }

  // Calcula as médias finais depois de rodar as N repetições
  double media_limites = total_tempo_limites / num_repeticoes;
  double media_serial = total_tempo_serial / num_repeticoes;
  double media_paralelo = total_tempo_paralelo / num_repeticoes;
  double media_speedup = media_serial / media_paralelo;
  double eficiencia = (media_speedup / num_threads) * 100.0;

  // MEPS: Milhões de Elementos Por Segundo
  double meps_serial = (num_elementos / 1e6) / media_serial;
  double meps_paralelo = (num_elementos / 1e6) / media_paralelo;

  printf("  ----- ;------------ ;"
           "------------ ;------------ ;"
           "---------- ; ----\n");

    printf("  AVG   ;"
           " %11.6f ;"
           " %11.6f ;"
           " %11.6f ;"
           " %8.3f ; %s\n",
           media_limites,
           media_serial,
           media_paralelo,
           media_speedup,
           ok_global ? "OK" : "FAIL");

  // Passo 7: Sumário Final
  //MUDANÇA
  
    printf("\n=== Summary ===\n");

    printf("  Avg build_limits serial" 
           "   : %.6f ; s\n",
           media_limites);

    printf("  Avg time (1 thread)"
           "      : %.6f ; s ; %.2f ; MEPS\n",
           media_serial,
           meps1);

    printf("  Avg time (%d threads)"
           "     : %.6f ; s ; %.2f ; MEPS\n",
           num_threads,
           media_paralelo,
           mepsn);

    printf("  Avg histogram speedup"
           "     : %.3fx\n",
           speedup);

    printf("\n");

    printf("  Parallel efficiency:\n");
    printf("    with nthreads (%d)"
           " : %.1f%%\n",
           num_threads,
           eficiencia);

    printf("\n");

    printf("  Overall correctness"
           "       : %s\n\n",
           ok_global ? "PASS" : "FAIL");

  // Limpeza da casa
  free(dados);
  free(dados2);
  free(limites);
  free(histograma_serial);
  free(histograma_paralelo);
  free(buffer_limpeza);

  return 0;
}