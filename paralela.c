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

// Estrutura para enviar os dados para as threads trabalhadoras (workers)
struct DadosThread {
  const long long *dados;
  const long long *limites;
  int num_bins;
  long long inicio;
  long long fim;
  long long *histograma_local;
};

// Função auxiliar para obter o tempo atual em segundos
double obter_tempo_atual() {
  struct timespec tempo;
  clock_gettime(CLOCK_MONOTONIC, &tempo);
  return tempo.tv_sec + tempo.tv_nsec * 1e-9;
}

// Gera um número de 64-bits cobrindo todo o intervalo possível de 'long long'
// (positivos e negativos)
long long gerar_numero_aleatorio() {
  unsigned long long p1 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long p2 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long p3 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long p4 = (unsigned long long)(rand() & 0xFFFF);
  unsigned long long resultado = (p1 << 48) | (p2 << 32) | (p3 << 16) | p4;
  return (long long)resultado;
}

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
}

// Busca binária super rápida (O(log n)) para descobrir em qual caixa (bin) um
// número deve entrar
static inline int encontrar_bin(long long valor, const long long *limites,
                                int num_bins) {
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

// O que cada thread faz: percorre sua fatia e soma no seu histograma privado
void *tarefa_da_thread(void *argumentos) {
  struct DadosThread *dados_thread = (struct DadosThread *)argumentos;
  for (long long i = dados_thread->inicio; i < dados_thread->fim; i++) {
    int bin_correto = encontrar_bin(
        dados_thread->dados[i], dados_thread->limites, dados_thread->num_bins);
    dados_thread->histograma_local[bin_correto]++;
  }
  return NULL;
}

// Passo 4 e 5 - Histograma Paralelo
// Essa é a função principal exigida no enunciado.
int parallel_histogram(const long long *dados, long long num_elementos,
                       const long long *limites, int num_bins,
                       long long *histograma_saida, int num_threads) {
  // Zera o histograma global de saída antes de começar
  for (int i = 0; i < num_bins; i++) {
    histograma_saida[i] = 0;
  }

  // Se o usuário pediu 1 thread, roda serial normal (sem sobrecarga de
  // Pthreads)
  if (num_threads == 1) {
    for (long long i = 0; i < num_elementos; i++) {
      int bin_correto = encontrar_bin(dados[i], limites, num_bins);
      histograma_saida[bin_correto]++;
    }
    return 0;
  }

  // Execução paralela (Pool de threads)
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

    // Cria a thread de verdade
    pthread_create(&threads[i], NULL, tarefa_da_thread, &info_threads[i]);
  }

  // A thread PRINCIPAL ('Main') não fica dormindo. Ela vai atuar como "Worker"
  // na última fatia.
  int indice_main = num_threads - 1;
  info_threads[indice_main].dados = dados;
  info_threads[indice_main].limites = limites;
  info_threads[indice_main].num_bins = num_bins;
  info_threads[indice_main].histograma_local =
      (long long *)calloc(num_bins, sizeof(long long));
  if (!info_threads[indice_main].histograma_local)
    return -1;

  info_threads[indice_main].inicio = atual_inicio;
  long long extra_main = (indice_main < sobra) ? 1 : 0;
  info_threads[indice_main].fim = atual_inicio + tamanho_fatia + extra_main;

  // Roda direto a função da thread na Main thread
  tarefa_da_thread(&info_threads[indice_main]);

  // Barreira de Sincronização: Espera as outras threads terminarem
  for (int i = 0; i < num_threads - 1; i++) {
    pthread_join(threads[i], NULL);
  }

  // Redução: Soma todos os pequenos histogramas privados em um grandão final
  for (int i = 0; i < num_threads; i++) {
    for (int b = 0; b < num_bins; b++) {
      histograma_saida[b] += info_threads[i].histograma_local[b];
    }
    free(
        info_threads[i]
            .histograma_local); // Libera a memória do histograma daquela thread
  }

  free(threads);
  free(info_threads);
  return 0;
}

int main(int argc, char **argv) {
  // Validação de segurança dos argumentos do programa
  if (argc != 6) {
    printf("Uso: %s <num_elementos> <num_pivos> <num_bins> <num_threads> "
           "<num_repeticoes>\n",
           argv[0]);
    return 1;
  }

  long long num_elementos = atoll(argv[1]); // nelements
  int num_pivos = atoi(argv[2]);            // npivots
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

  // Cria o buffer que esfria a cache (pre-faulting para evitar que o SO afete o
  // tempo)
  char *buffer_limpeza = (char *)malloc(TAMANHO_BUFFER_LIMPEZA);
  if (!buffer_limpeza) {
    printf("Erro alocando buffer para limpar cache.\n");
    return 1;
  }
  memset(buffer_limpeza, 0xAA, TAMANHO_BUFFER_LIMPEZA);

  // Alocações de memória principais (Arrays fisicamente separados exigidos)
  long long *dados = (long long *)malloc(num_elementos * sizeof(long long));
  long long *dados2 =
      (long long *)malloc(num_elementos * sizeof(long long)); // data2 (cópia)
  long long *limites = (long long *)malloc((num_bins + 1) * sizeof(long long));
  long long *histograma_serial =
      (long long *)malloc(num_bins * sizeof(long long));
  long long *histograma_paralelo =
      (long long *)malloc(num_bins * sizeof(long long));

  if (!dados || !dados2 || !limites || !histograma_serial ||
      !histograma_paralelo) {
    printf("Erro crasso de alocação de memória.\n");
    return 1;
  }

  srand((unsigned)time(NULL));

  // Variáveis para somar médias
  double total_tempo_limites = 0, total_tempo_serial = 0,
         total_tempo_paralelo = 0;
  int resultado_global_ok = 1;

  // Cabeçalho idêntico ao do professor
  printf("\nRound | T(bl-ser) | T(1 thr) | T(N thr) | Speedup | OK/FAIL\n");
  printf("-----------------------------------------------------------\n");

  // Roda os testes as X repetições pedidas
  for (int rodada = 1; rodada <= num_repeticoes; rodada++) {

    // Passo 1: Geração dos números aleatórios
    for (long long i = 0; i < num_elementos; i++) {
      dados[i] = gerar_numero_aleatorio();
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
    parallel_histogram(dados2, num_elementos, limites, num_bins,
                       histograma_paralelo, num_threads);
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

  // Passo 7: Sumário Final
  printf("\n=== Sumario Final (%d rounds) ===\n", num_repeticoes);
  printf("- Tempo medio de build_limits : %.4f s\n", media_limites);
  printf("- Tempo medio (1 thread)      : %.4f s (%.2f MEPS)\n", media_serial,
         meps_serial);
  printf("- Tempo medio (%d threads)     : %.4f s (%.2f MEPS)\n", num_threads,
         media_paralelo, meps_paralelo);
  printf("- Speedup medio               : %.2f\n", media_speedup);
  printf("- Eficiencia paralela         : %.2f%%\n", eficiencia);
  printf("- Corretude Global            : %s\n\n",
         resultado_global_ok ? "OK" : "FAIL");

  // Limpeza da casa
  free(dados);
  free(dados2);
  free(limites);
  free(histograma_serial);
  free(histograma_paralelo);
  free(buffer_limpeza);

  return 0;
}