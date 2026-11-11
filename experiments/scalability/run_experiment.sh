#!/bin/bash

# if [[ $# -ne 5 ]]; then
#   echo "Usage: $0 EXPERIMENT_NAME FILTER_LIST_IN_TXT_FILE DATA_DIR RESULTS_DIR NODE_COUNT"
#   echo "Ex: $0 shuffle_scalability filters/shuffle.txt /cscratch/btc-export results 8"
#   exit 1
# fi

TEST_NAME="kmer_inv_idx"
# FILTERS="filters/dagify.txt"
FILTERS="filters/kmer_index_frag.txt"
# DATA_DIR="/cscratch/eth-pre-dag-full/"
# DATA_DIR="/cscratch/fastq/pando/pando"
# DATA_DIR="/cscratch/pando-few"
DATA_DIR="/cscratch/pando-one"
RESULTS_DIR="/cscratch/results/"
NODE_COUNT=8

mkdir -p "$RESULTS_DIR"
touch "$RESULTS_DIR/$TEST_NAME.csv"

AVAILABLE_NODE_COUNT=$(sinfo -N -p compute | grep idle | awk '{print $1}' | wc -l) 
AVAILABLE_NODES=$(sinfo -N -p compute | grep idle | awk '{print $1}')              

# Check if available nodes are less than requested nodes
if [ "$AVAILABLE_NODE_COUNT" -lt "$NODE_COUNT" ]; then
    echo "Error: Requested $NODE_COUNT nodes when only $AVAILABLE_NODE_COUNT nodes are available."
    exit 1
fi


# Calculate half the number of available nodes
# HALF_NODE_COUNT=$(($AVAILABLE_NODE_COUNT / 2))
# Get the first half of the available nodes
# FIRST_HALF_NODES=$(echo "$AVAILABLE_NODES" | head -n "$HALF_NODE_COUNT")

# Get the second half of the available nodes
# SECOND_HALF_NODES=$(echo "$AVAILABLE_NODES" | tail -n +"$((HALF_NODE_COUNT + 1))")

# srun -N "$HALF_NODE_COUNT" bash -c 'rm -rf /scratch/bigspace* /tmp/elga*' --nodelist="$FIRST_HALF_NODES" &
# srun -N "$((NODE_COUNT - HALF_NODE_COUNT))" bash -c 'rm -rf /scratch/bigspace* /tmp/elga*' --nodelist="$SECOND_HALF_NODES" &


# Submit batch job
SLURM_OUTPUT=$(sbatch -N "$NODE_COUNT" --mem 360000 pardb.sbatch)
JOB_ID=$(echo "$SLURM_OUTPUT" | awk '{print $4}')
echo "Batch job submitted with $NODE_COUNT nodes. Job ID: $JOB_ID"

sleep 1

# Get mesh
FIRST_NODE="en$(scontrol show job $JOB_ID | grep "NodeList=" | awk 'NR==2' | awk -F'=' '{print $2}' | tr -d ' ' | awk -F',' '{print $1}' | awk -F'[' '{print $2}' | awk -F'-' '{print $1}' | sed 's/[[:space:]]//g')"
HOST_OUTPUT=$(host "$FIRST_NODE.100g")
IP_ADDRESS=$(echo "$HOST_OUTPUT" | awk '{print $NF}')
MESH="${IP_ADDRESS},0"

echo "MESH: $MESH"

echo "Beginning Experiment..."
echo "Params: $TEST_NAME $MESH $FILTERS $DATA_DIR $RESULTS_DIR"
# singularity run -B /cscratch -B /scratch ./../../src/sifs/pando_python.sif python3 dagify-script.py -n $TEST_NAME -m $MESH -f $FILTERS -d $DATA_DIR -r $RESULTS_DIR
singularity run -B /cscratch -B /scratch ./../../src/sifs/pando_python.sif python3 eth-tx-val.py -n $TEST_NAME -m $MESH -f $FILTERS -d $DATA_DIR -r $RESULTS_DIR

echo "Experiment Complete"
echo "Canceling Job $JOB_ID"
scancel $JOB_ID

# HERE WE WANT TO RUN THE SNAKEFILE, MOVE THE OUTPUT FILES (OTHERWISE SNAKE WILL SEE THEY ALREADY EXIST AND TERMINATE AFTER FIRST RUN)
# RUN THE FILE, MOVE THE OUTPUT, RUN THE FILE. 
# snakemake --cores all --config experiment_name="$1" filters="$2" nodes="$3"

