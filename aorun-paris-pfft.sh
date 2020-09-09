#!/bin/bash

module use /home/users/twhite/share/modulefiles
module load pfft-ompi hdf5

export LD_LIBRARY_PATH="$CRAY_LD_LIBRARY_PATH:$LD_LIBRARY_PATH"
OUTDIR="out.paris.pfft-amd-ompi.$(date +%m%d.%H%M%S)"
set -x
mkdir -p ${OUTDIR}
cd ${OUTDIR}
export OMP_NUM_THREADS=16
srun -n4 -c$OMP_NUM_THREADS -N1 --exclusive -p amdMI60 ../cholla.paris.pfft-amd-ompi ../parameter_file.txt |& tee tee
