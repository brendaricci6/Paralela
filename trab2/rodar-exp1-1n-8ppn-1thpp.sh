#!/bin/bash
#SBATCH --job-name=exp1-1n-8ppn-1thpp
#SBATCH --output=saida-slurm-exp1-1n-8ppn-1thpp.txt
#SBATCH --error=saida-slurm-exp1-1n-8ppn-1thpp.txt
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --partition=w

# ---------------------------------------------------------------
# Experiência 1: 1 nodo, 8 processos MPI, 1 thread por processo
#   nelements=32000000  npivots=32000  nbins=512
#   nthreads=1  nr=10
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
NTHREADS=1
NR=10

mpirun --bind-to core \
       ./MPI+Pthreads-Multipartition \
       $NELEMENTS $NPIVOTS $NBINS $NTHREADS $NR -Verify

echo "========================================================"
echo "Fim: $(date)"
echo "========================================================"
