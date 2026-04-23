#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define LONG_MIN 1000
#define LONG_MAX 100000

int main (int argc, char **argv){
  
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

    /*
        Passo 1 — Geração do input
      Gera nelements valores long long aleatórios cobrindo
      uniformemente todo o intervalo [LLONG_MIN, LLONG_MAX]. Um segundo
      array idêntico (data2) é criado por memcpy — os dois arrays são
      fisicamente separados na memória para garantir que cada pool de
      threads leia da RAM, não do cache aquecido pelo outro.
    */

    //alocação dinâmica de data e dataCopy
    long long *data = (long long *) malloc(nElements * sizeof(long long));
    long long *dataCopy = (long long *) malloc(nElements * sizeof(long long));

    //preenchimento dos vetores aleatórios
    for (long long i = 0; i < nElements; i++) {
        // Gera valores entre MY_LONG_MIN e MY_LONG_MAX
        data[i] = LONG_MIN + (rand() % (LONG_MAX - LONG_MIN + 1));
    }

    //copia os dados 
    memcpy(dataCopy, data, nElements * sizeof(long long));

    /* 
        Passo 2 — Construção dos limites (serial)
      Cronometrada independentemente. Produz limits[0..nbins] pelo modo
      escolhido. Este array é compartilhado por ambos os pools —
      garante que ambas as versões (1 thread e N threads) calculem o
      histograma sobre os mesmos intervalos.
    */

    free(data);
    free(dataCopy);  

}
