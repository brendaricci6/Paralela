#!/bin/bash
#SBATCH --job-name=histo4M         # Nome do job na fila
#SBATCH --partition=debug         # Fila correta do cluster
#SBATCH --nodes=1                  # Roda em 1 nodo só
#SBATCH --exclusive                # Nodo exclusivo — sem interferência de outros usuários
#SBATCH --output=slurm_%j.out      # Saída padrão (stdout)
#SBATCH --error=slurm_%j.err       # Saída de erros (stderr)
# Submeter com: sbatch run_experiments4_slurm.sh

# Informações do ambiente Slurm (útil para debug)
echo "SLURM_JOB_NAME: "        $SLURM_JOB_NAME
echo "SLURM_NODELIST: "        $SLURM_NODELIST
echo "SLURM_JOB_NODELIST: "    $SLURM_JOB_NODELIST
echo "SLURM_JOB_CPUS_PER_NODE: " $SLURM_JOB_CPUS_PER_NODE
echo "Rodando no host: "       $(hostname)

# -------------------------------------------------------
# Configurações do experimento
# -------------------------------------------------------
EXEC="./parallel-histo"
PIVOTS=1024
BINS=32
ROUNDS=10          # nr >= 10 para médias estáveis
LOG_FILE="resultados_experimentos161M.txt"

# Limpa o arquivo de log se já existir
> $LOG_FILE

# Lista de tamanhos de vetor
ELEMENTS_LIST=(16000000)

echo "Iniciando experimentos... Resultados serão salvos em $LOG_FILE"

# Coleta informações da CPU
echo "=== INFORMAÇÕES DO SISTEMA ===" >> $LOG_FILE
lscpu >> $LOG_FILE
echo "------------------------------" >> $LOG_FILE

for N_ELEMENTS in "${ELEMENTS_LIST[@]}"; do
    echo "Rodando para $N_ELEMENTS elementos..."

    # Cada nodo Xeon tem 2 processadores x 4 núcleos = 8 núcleos (sem HT)
    for N_THREADS in {1..4}; do
        echo "  - Threads: $N_THREADS"
        echo "CONFIG: Elements=$N_ELEMENTS, Threads=$N_THREADS" >> $LOG_FILE

        $EXEC $N_ELEMENTS $PIVOTS $BINS $N_THREADS $ROUNDS -tb2 >> $LOG_FILE

        echo -e "\n" >> $LOG_FILE
    done
done

echo "Experimentos 4M finalizados!"
echo "O tempo total dessa shell foi de $SECONDS segundos"

# Imprime infos finais do job Slurm
squeue -j $SLURM_JOBID