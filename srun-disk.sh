#!/bin/bash
#BSUB -P CSC380
#BSUB -W 2:00
#BSUB -nnodes 2
#BSUB -J cholla
#BSUB -o o.%J

module load gcc hdf5 cuda

OUTDIR="out.MW.${LSB_JOBID}"
set -x
mkdir -p ${OUTDIR}
mkdir -p ${OUTDIR}/hdf5
mkdir -p ${OUTDIR}/hdf5/raw
cd ${OUTDIR}
jsrun --smpiargs="-gpu" -n8 -a1 -c1 -g1 ../cholla ../MW_input.txt |& tee tee