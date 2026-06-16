#!/bin/bash
#SBATCH --job-name=exp3-4n-2ppn-4thpp
#SBATCH --output=saida-slurm-exp3-4n-2ppn-4thpp.txt
#SBATCH --error=saida-slurm-exp3-4n-2ppn-4thpp.txt
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=4
#SBATCH --exclusive
#SBATCH --partition=w

# ---------------------------------------------------------------
# Experiência 3: 4 nodos, 2 processos MPI por nodo, 4 threads/proc
#   nelements=32000000  npivots=32000  nbins=512
#   nthreads=4  nr=10   (total: 8 processos MPI)
# ---------------------------------------------------------------

echo "========================================================"
echo "SLURM Job: $SLURM_JOB_ID"
echo "Nodelist : $SLURM_JOB_NODELIST"
echo "Nodes    : $SLURM_JOB_NUM_NODES"
echo "Tasks    : $SLURM_NTASKS"
echo "CPUs/task: $SLURM_CPUS_PER_TASK"
echo "Data/hora: $(date)"
echo "========================================================"

lscpu
echo "========================================================"

cd $SLURM_SUBMIT_DIR

NELEMENTS=32000000
NPIVOTS=32000
NBINS=512
NTHREADS=4
NR=10

mpirun --bind-to core \
       ./MPI+Pthreads-Multipartition \
       $NELEMENTS $NPIVOTS $NBINS $NTHREADS $NR -Verify

echo "========================================================"
echo "Fim: $(date)"
echo "========================================================"
