#!/bin/bash

module load PrgEnv-cray
module load hdf5
module load gcc

export LD_LIBRARY_PATH="$CRAY_LD_LIBRARY_PATH:$LD_LIBRARY_PATH"
OUTDIR="out.paris.cufft.$(date +%m%d.%H%M%S)"
set -x
mkdir -p ${OUTDIR}
cd ${OUTDIR}
export MV2_USE_CUDA=1
export MV2_ENABLE_AFFINITY=0
export OMP_NUM_THREADS=16
srun -n1 -c$OMP_NUM_THREADS -N1 --exclusive -p v100 ../cholla.paris.cufft ../parameter_file.txt |& tee tee
