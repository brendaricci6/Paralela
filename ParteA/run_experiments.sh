#!/bin/bash

# Configurações do experimento
EXEC="./parallel-histo"
PIVOTS=1024
BINS=32
ROUNDS=10  # Recomendado nr >= 10 para médias estáveis [cite: 71]
LOG_FILE="resultados_experimentos.txt"

# Limpa o arquivo de log se já existir
> $LOG_FILE

# Lista de tamanhos de vetor (4M, 8M, 16M) [cite: 15]
ELEMENTS_LIST=(4000000 8000000 16000000)

echo "Iniciando experimentos... Resultados serão salvos em $LOG_FILE"

# Coleta informações da CPU para a primeira página da planilha [cite: 23]
echo "=== INFORMAÇÕES DO SISTEMA ===" >> $LOG_FILE
lscpu >> $LOG_FILE
echo "------------------------------" >> $LOG_FILE

for N_ELEMENTS in "${ELEMENTS_LIST[@]}"; do
    echo "Rodando para $N_ELEMENTS elementos..."
    
    # Varia de 1 até 8 threads [cite: 14]
    for N_THREADS in {1..8}; do
        echo "  - Threads: $N_THREADS"
        echo "CONFIG: Elements=$N_ELEMENTS, Threads=$N_THREADS" >> $LOG_FILE
        
        # Executa o programa com a opção -tb2 (entrada balanceada) [cite: 4, 37]
        $EXEC $N_ELEMENTS $PIVOTS $BINS $N_THREADS $ROUNDS -tb2 >> $LOG_FILE
        
        echo -e "\n" >> $LOG_FILE
    done
done

echo "Experimentos finalizados!"