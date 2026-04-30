#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>


//função que gera 64 bits aleatórios
long long rand64(){
    return ((long long)rand() << 48) ^
            ((long long)rand() << 32) ^
            ((long long) rand () << 16) ^
            ((long long) rand());

}

int main (int argc, char **argv){

    srand(time(NULL));
  
    //num de elementos no array de entrada
    int long long nElements = atoll(argv[1]);
    //num de pivos 
    int nPivots = atoi(argv[2]);
    //num de bins do histograma
    int nBins = atoi(argv[3]); 
    //num threads
    int nThreads = atoi(argv[4]);
    //num repeticoes
    int nRepeticoes = atoi(argv[5]);

    //verificação dos valores lidos

    if (nElements < 0){
        printf ("nElements inválido"); 
        return 1; 
    }
    if ((nPivots < 2) || (nPivots < nBins) || (nPivots > nElements)){
        printf ("nPivots inválido"); 
        return 1; 
    }
    if (nBins <= 0){
        printf ("nBins inválido"); 
        return 1; 
    }
    if (nRepeticoes <= 0){
        printf ("nRepeticoes inválido"); 
        return 1; 
    }

    long long llc_size = 16 * 1024 * 1024; 
    long long evict_size = 3 * llc_size;
    char *evict_buffer = malloc(evict_size);
    memset(evict_buffer, 0, evict_size);

    // Arrays para guardar os resultados de cada thread
    long long *hist_ser = malloc(nBins * sizeof(long long));
    long long *hist_par = malloc(nBins * sizeof(long long));
    long long *limits = malloc((nBins + 1) * sizeof(long long));

    for (int r = 0; r < nRepeticoes; r++) {
        //alocação dinâmica de data e dataCopy
        long long *data = (long long *) malloc(nElements * sizeof(long long));
        long long *dataCopy = (long long *) malloc(nElements * sizeof(long long));

        for (long long i = 0; i < nElements; i++) {
        //gera um valor que pode ser qualquer número entre LLONG_MIN e LLONG_MAX
        data[i] = rand64(); 
        }

        //copia os dados 
        memcpy(dataCopy, data, nElements * sizeof(long long));

        

        free(data);
        free(dataCopy);
    }

}
