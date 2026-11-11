#!/bin/bash

###################
## Slurm parameters

#SBATCH --ntasks-per-node=16
#SBATCH -o log/%j.out
#SBATCH --exclusive
#SBATCH -c 4

#####################
## Cluster parameters

### Pando connection information
echo "outputting"
node=$(echo $SLURM_JOB_NODELIST | cut -d '-' -f1 - | cut -d ',' -f1 - | tr -d '[')
#FIXME this is environment specific
ip=$(host "$node.100g" | awk '{print $4}')
PANDO_MESH_ENTRY="$ip,0"

SCRIPT_PATH=$(scontrol show job $SLURM_JOBID | awk -F= '/Command=/{print $2}')
SCRIPT_DIR=$(dirname $SCRIPT_PATH)
PANDO_PARDB_BIN=$SCRIPT_DIR/../../src/sifs/pando_pardb.sif

PANDO_BIND_IP_FILE=/opt/installer/ip
echo REMOVING FILES
srun bash -c 'sudo rm -rf /tmp/elga.ipc* /scratch/bigspace*'
srun bash -c 'rm -rf /tmp/elga.ipc* /scratch/bigspace*'
echo CHECKING SCRATCH SPACE
srun bash -c 'ls /scratch'
srun bash -c 'ls /tmp'

echo START
date
echo $PANDO_MESH_ENTRY 
echo $PANDO_PARDB_BIN
echo $PANDO_BIND_IP_FILE
srun --unbuffered bash pardb.internal $PANDO_MESH_ENTRY $PANDO_PARDB_BIN $PANDO_BIND_IP_FILE

echo FINISH
date

