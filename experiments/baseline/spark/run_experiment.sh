#!/bin/bash     

export JAVA_HOME=/usr/lib/jvm/java-17-openjdk
export PATH="$JAVA_HOME/bin:$PATH"

if [[ $# -ne 5 ]]; then
    echo "Usage: $0 EXPERIMENT_NAME SPARK_FILE DATA_DIR RESULTS_DIR NODE_COUNT"
    echo "Ex: $0 count_kmer /ascldap/users/$(USER)/bqs/experiments/baseline/spark/kmer_count/kmer_count.py /cscratch/longreads /cscratch/results/kmer_count 8"
    exit 1
fi

TEST_NAME=$1
SPARK_FILE=$2
DATA_DIR=$3
RESULTS_DIR=$4
NODE_COUNT=$5
SCRIPT_DIR=$(pwd)
export SPARK_DIR="/ascldap/users/$USER/bqs/experiments/baseline/spark/spark-4.0.1-bin-hadoop3"

export USER=$(whoami)  # Set USER variable if needed
# export SPARK_DIR="spark-4.0.1-bin-hadoop3"

# Get the count of available idle processing nodes in the specified partition
AVAILABLE_NODE_COUNT=$(sinfo -N -p compute | grep idle | awk '{print $1}' | wc -l)
AVAILABLE_NODES=$(sinfo -N -p compute | grep idle | awk '{print $1}')
# Check if available nodes are less than requested nodes
if [ "$AVAILABLE_NODE_COUNT" -lt "$NODE_COUNT" ]; then
    echo "Error: Requested $NODE_COUNT nodes when only $AVAILABLE_NODE_COUNT nodes are available."
    exit 1
fi

echo "Removing log files in 5 seconds. CTRL+C to save."
echo 5 && sleep 1
echo 4 && sleep 1
echo 3 && sleep 1
echo 2 && sleep 1
echo 1 && sleep 1
echo "Removing log files"

rm -f $SPARK_DIR/logs/*
rm -rf $SPARK_DIR/work/*

echo "Submitting slurm job"

# Submit batch job
SLURM_OUTPUT=$(sbatch -N "$NODE_COUNT" --mem 400000 spark.sbatch)
JOB_ID=$(echo "$SLURM_OUTPUT" | awk '{print $4}')
sleep 1
NODE_LIST=$(scontrol show job $JOB_ID | grep "NodeList=" | awk -F'=' '{print $2}' | awk NR==2 | tr -d ' ')
echo "Batch job submitted with $NODE_COUNT nodes. Job ID: $JOB_ID"
echo "Nodes used $NODE_LIST"

MASTER_NODE=$(scontrol show hostnames "$NODE_LIST" | head -n 1)

echo "Master node: $MASTER_NODE"
echo "Waiting 15 seconds to make sure daemons are running..."
sleep 5 && echo "10 seconds"
sleep 5 && echo "5 seconds"
sleep 5


echo "Beginning Experiment..."
# TIME_VAL=$( { time $SCRIPT_DIR/$SPARK_DIR/bin/spark-submit --master spark://$MASTER_NODE:7077 --executor-cores 8 --executor-memory 8G --total-executor-cores 384 --driver-memory 4G $SPARK_FILE $DATA_DIR $RESULTS_DIR 15; } 2>&1 )
# TIME_VAL=$( { time "$SPARK_DIR/bin/spark-submit" --master "spark://$MASTER_NODE:7077" --executor-cores 8 --executor-memory 8G --total-executor-cores 384 --driver-memory 4G "$SPARK_FILE" --input "$DATA_DIR" --output-dag "$RESULTS_DIR" --shuffle-partitions 4096 --bitmap-partitions 4096 --persist false --output-compression gzip; } 2>&1 )
# set +e; TIME_VAL=$( { time "$SPARK_DIR/bin/spark-submit" --master "spark://$MASTER_NODE:7077" --executor-cores 8 --executor-memory 8G --total-executor-cores 384 --driver-memory 4G "$SPARK_FILE" --input "$DATA_DIR" --output-dag "$RESULTS_DIR" --shuffle-partitions 4096 --bitmap-partitions 4096 --persist false --output-compression gzip; } 2>&1 ); SPARK_STATUS=$?; set -e; echo "$TIME_VAL"; if [ "$SPARK_STATUS" -ne 0 ]; then echo "spark-submit failed with status $SPARK_STATUS; leaving Slurm job $JOB_ID alive for debugging"; exit "$SPARK_STATUS"; fi
# set +e; TIME_VAL=$( { time "$SPARK_DIR/bin/spark-submit" --master "spark://$MASTER_NODE:7077" --executor-cores 8 --executor-memory 8G --total-executor-cores 384 --driver-memory 4G "$SPARK_FILE" --input "/cscratch/transaction_samples/*.csv" --output-dag "$RESULTS_DIR" --header true --shuffle-partitions 4096 --bitmap-partitions 4096 --persist false --output-compression gzip; } 2>&1 ); SPARK_STATUS=$?; set -e; echo "$TIME_VAL"; if [ "$SPARK_STATUS" -ne 0 ]; then echo "spark-submit failed with status $SPARK_STATUS; leaving Slurm job $JOB_ID alive for debugging"; exit "$SPARK_STATUS"; fi
set +e; TIME_VAL=$( { time "$SPARK_DIR/bin/spark-submit" --master "spark://$MASTER_NODE:7077" --executor-cores 8 --executor-memory 8G --total-executor-cores 384 --driver-memory 4G "$SPARK_FILE" --input "file:///cscratch/transaction_samples" --output-dag "$RESULTS_DIR" --header true --shuffle-partitions 4096 --bitmap-partitions 4096 --persist false --output-compression gzip; } 2>&1 ); SPARK_STATUS=$?; set -e; echo "$TIME_VAL"; if [ "$SPARK_STATUS" -ne 0 ]; then echo "spark-submit failed with status $SPARK_STATUS; leaving Slurm job $JOB_ID alive for debugging"; exit "$SPARK_STATUS"; fi
echo "Experiment Complete"

echo $TIME_VAL >> tmp.txt

# Extract the real time
real_time=$(echo "$TIME_VAL" | grep '^real' | awk '{print $2}')

# Extract minutes and seconds
minutes=$(echo "$real_time" | cut -d'm' -f1)
seconds=$(echo "$real_time" | cut -d'm' -f2 | cut -d's' -f1)

# Convert to total seconds
total_seconds=$(echo "$minutes * 60 + $seconds" | bc)

echo "Time Taken: $total_seconds"

echo "Terminating slurm cluster. Ctrl+C within 5 seconds to keep Slurm cluster..."
echo 5 && sleep 1
echo 4 && sleep 1
echo 3 && sleep 1
echo 2 && sleep 1
echo 1 && sleep 1
echo "Sending termination signal."
scancel $JOB_ID
echo "Job terminated."


