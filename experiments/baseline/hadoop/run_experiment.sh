#!/bin/bash

if [[ $# -ne 5 ]]; then
  echo "Usage: $0 EXPERIMENT_NAME JAR_FILE DATA_DIR RESULTS_DIR NODE_COUNT"
  echo "Ex: $0 count_kmer /ascldap/users/$(USER)/MaxReduce/experiments/baseline/hadoop-3.4.1/share/hadoop/mapreduce/hadoop-mapreduce-examples-3.4.1.jar /cscratch/hadoop-test/input.txt results 32"
  exit 1
fi

TEST_NAME=$1
JAR_FILE=$2
DATA_DIR=$3
RESULTS_DIR=$4
NODE_COUNT=$5
SCRIPT_DIR=$(pwd)
export USER=$(whoami)  # Set USER variable if needed
export HADOOP_HOME="$SCRIPT_DIR/hadoop-3.4.1"
export HADOOP_CONF_DIR=${HADOOP_HOME}/etc/hadoop
export HADOOP_WORKERS="/tmp/workers"

# Get the count of available idle processing nodes in the specified partition
AVAILABLE_NODE_COUNT=$(sinfo -N -p compute | grep idle | awk '{print $1}' | wc -l)
AVAILABLE_NODES=$(sinfo -N -p compute | grep idle | awk '{print $1}')
# Check if available nodes are less than requested nodes
if [ "$AVAILABLE_NODE_COUNT" -lt "$NODE_COUNT" ]; then
    echo "Error: Requested $NODE_COUNT nodes when only $AVAILABLE_NODE_COUNT nodes are available."
    exit 1
fi

# Submit batch job
rm -rf hadoop-3.4.1/logs/*  
rm -r $SCRIPT_DIR/hadoop.log
SLURM_OUTPUT=$(sbatch -N "$NODE_COUNT" --mem 400000 hadoop.sbatch)
# SLURM_OUTPUT=$(sbatch -w en[195,209-214,216-234,236-255,269-274,276-277] --mem 490000 hadoop.sbatch)
JOB_ID=$(echo "$SLURM_OUTPUT" | awk '{print $4}')
sleep 1
NODE_LIST=$(scontrol show job $JOB_ID | grep "NodeList=" | awk -F'=' '{print $2}' | awk NR==2 | tr -d ' ')
echo "Batch job submitted with $NODE_COUNT nodes. Job ID: $JOB_ID"
echo "Nodes used $NODE_LIST"
sleep 15
scp en$(scontrol show job $JOB_ID | grep "NodeList=" | awk 'NR==2' | awk -F'=' '{print $2}' | tr -d ' ' | awk -F',' '{print $1}    ' | awk -F'[' '{print $2}' | awk -F'-' '{print $1}' | sed 's/[[:space:]]//g'):/tmp/workers /tmp/

cleanup() {
  echo "Canceling Job $JOB_ID"
  #parallel-ssh -h <(scontrol show hostnames)
  scancel $JOB_ID
  rm -r hadoop_setup_complete
#   srun -m $NODE_LIST bash -c  "sudo rm -rf /tmp/hadoop* /tmp/*jni* /tmp/hsperfdata_* /tmp/jetty*"
  ./hadoop-3.4.1/bin/hadoop fs -rm -r -skipTrash /user
  ./hadoop-3.4.1/bin/hadoop fs -rm -r -skipTrash /tmp
  #./hadoop-3.4.1/sbin/stop-dfs.sh
  #./hadoop-3.4.1/sbin/stop-yarn.sh
  exit 0
}

trap cleanup SIGINT

echo "Waiting for Hadoop setup to complete, estimated time: 1m30s"
while [ ! -f $SCRIPT_DIR/hadoop_setup_complete ]; do
    echo "    Waiting..."
    sleep 10
done

sleep 5

echo "Wait complete. May your patience be rewarded."

sleep 1

echo "Clearing old files..."
./hadoop-3.4.1/bin/hadoop fs -ls /user/$USER/output/
./hadoop-3.4.1/bin/hadoop fs -rm -r /user/$USER/output/
./hadoop-3.4.1/bin/hdfs dfs -rm -r /user/$USER/output/
./hadoop-3.4.1/bin/hdfs dfs -mkdir -p /user/$USER/input/
./hadoop-3.4.1/bin/hadoop fs -rm -r /user/
./hadoop-3.4.1/bin/hadoop fs -mkdir -p /user/$USER/input

echo "Loading input..."
./hadoop-3.4.1/bin/hdfs dfs -put $DATA_DIR/* /user/$USER/input/
# ./hadoop-3.4.1/bin/hdfs dfs -rm -r /user/$USER/output/
# ./hadoop-3.4.1/bin/hdfs dfs -mkdir -p /user/$USER/new_output/
# ./hadoop-3.4.1/bin/hadoop fs -mkdir -p /user/$USER/new_output/


echo "Beginning Experiment..."
# TIME_VAL=$( { time ./hadoop-3.4.1/bin/yarn --workers jar $SCRIPT_DIR/$JAR_FILE $TEST_NAME /user/$USER/input/ /user/$USER/output/; } )
TIME_VAL=$( { time ./hadoop-3.4.1/bin/yarn jar "$SCRIPT_DIR/$JAR_FILE" /user/$USER/input/ /user/$USER/output/ > $SCRIPT_DIR/hadoop.log 2>&1; } 2>&1)
# TIME_VAL=$({ time ./hadoop-3.4.1/bin/yarn --workers jar "$SCRIPT_DIR/$JAR_FILE" "$TEST_NAME" /user/$USER/input/ /user/$USER/output/ > $SCRIPT_DIR/hadoop.log 2>&1; } 2>&1)
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

echo "Fetching results..."
rm -rf $RESULTS_DIR/output/
mkdir -p "$RESULTS_DIR/output/"
./hadoop-3.4.1/bin/hadoop fs -get /user/$USER/output/ $RESULTS_DIR/
printf "%s," "$total_seconds" >> "$RESULTS_DIR/time.csv"

cleanup
