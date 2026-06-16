#!/bin/bash
#SBATCH --job-name=exp2-2n-4ppn-2thpp
#SBATCH --output=saida-slurm-exp2-2n-4ppn-2thpp.txt
#SBATCH --error=saida-slurm-exp2-2n-4ppn-2thpp.txt
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --partition=w

# ---------------------------------------------------------------
# Experiência 2: 2 nodos, 4 processos MPI por nodo, 2 threads/proc
#   nelements=32000000  npivots=32000  nbins=512
#   nthreads=2  nr=10   (total: 8 processos MPI)
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
NTHREADS=2
NR=10

mpirun --bind-to core \
       ./MPI+Pthreads-Multipartition \
       $NELEMENTS $NPIVOTS $NBINS $NTHREADS $NR -Verify

echo "========================================================"
echo "Fim: $(date)"
echo "========================================================"
