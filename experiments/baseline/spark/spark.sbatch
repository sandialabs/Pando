#!/bin/bash


###################
## Slurm parameters

#SBATCH --ntasks-per-node=1
#SBATCH -o log/%j.out
#SBATCH --exclusive
#SBATCH -c 64
#####################

# SPARK_BIN="/ascldap/users/$USER/bqs/experiments/baseline/spark-4.0.1-bin-hadoop3/bin"

# Get list of allocated nodes
nodes=$(scontrol show hostnames $SLURM_JOB_NODELIST)
# MASTER_NODE="en$(scontrol show job $SLURM_JOB_ID | grep "NodeList=" | awk 'NR==2' | awk -F'=' '{print $2}' | tr -d ' ' | awk -F',' '{print $1}' | awk -F'[' '{print $2}' | awk -F'-' '{print $1}' | sed 's/[[:space:]]//g')"
# MASTER_NODE="en25"
MASTER_NODE="$(scontrol show hostnames $SLURM_NODELIST | head -n 1)"
export SPARK_HOME="/ascldap/users/$USER/bqs/experiments/baseline/spark/spark-4.0.1-bin-hadoop3"


# Start Spark master on master node
if [ "$(hostname)" == "$MASTER_NODE" ]; then
  echo "Starting Spark master on $MASTER_NODE"
  srun --nodes=1 --ntasks=1 -w "$MASTER_NODE" bash -lc "mkdir -p \"$SPARK_HOME/logs\"; exec \"$SPARK_HOME/bin/spark-class\" org.apache.spark.deploy.master.Master --host \"\$(hostname -s)\" --port 7077 --webui-port 8080 > \"$SPARK_HOME/logs/master-\$(hostname -s).out\" 2> \"$SPARK_HOME/logs/master-\$(hostname -s).err\"" &

  # Wait a moment for master to start
  sleep 5

fi

# Get the Spark master URL from the master's web UI or logs

# Default master URL format: spark://<hostname>:7077
SPARK_MASTER_URL="spark://$MASTER_NODE:7077"

# SPARK_MASTER_URL_FILE="file.txt"
# echo $SPARK_MASTER_URL > $SPARK_MASTER_URL_FILE
# echo "Spark master URL saved to $SPARK_MASTER_URL_FILE"

# Wait for the master URL file to appear (in case other nodes start workers before master is ready)
# while [ ! -f $SPARK_MASTER_URL_FILE ]; do
#   echo "Waiting for Spark master URL file..."
#   sleep 2
# done

# SPARK_MASTER_URL=$(cat $SPARK_MASTER_URL_FILE)
# echo "Using Spark master URL: $SPARK_MASTER_URL"

# Start Spark workers on all nodes
# Start Spark workers on all nodes except the master
# for node in $nodes; do
#   if [ "$node" != "$MASTER_NODE" ]; then
# #       echo "Starting Spark worker on $node"
# #       srun --nodes=1 --ntasks=1 -w "$node" bash -lc '
# #         echo "HOST=$(hostname)"
# #         echo "SPARK_HOME=$SPARK_HOME"
# #         echo "JAVA_HOME=$JAVA_HOME"
# #         env | sort
# #         '"$SPARK_HOME"'/sbin/start-worker.sh '"$SPARK_MASTER_URL"'
# #         sleep 10
# #         ps -ef | grep -i "org.apache.spark.deploy.worker.Worker" | grep -v grep || true
#  fi
# done
# wait
for node in $nodes; do
  if [ "$node" != "$MASTER_NODE" ]; then
    echo "Starting Spark worker on $node"
    srun --nodes=1 --ntasks=1 -w "$node" bash -lc "mkdir -p \"$SPARK_HOME/logs\"; exec \"$SPARK_HOME/bin/spark-class\" org.apache.spark.deploy.worker.Worker --host \"\$(hostname -s)\" --webui-port 8081 \"$SPARK_MASTER_URL\" >> \"$SPARK_HOME/logs/worker-\$(hostname -s).out\" 2>> \"$SPARK_HOME/logs/worker-\$(hostname -s).err\"" &
#     srun --nodes=1 --ntasks=1 -w "$node" bash -lc "exec \"$SPARK_HOME/bin/spark-class\" org.apache.spark.deploy.worker.Worker --host \"\$(hostname -s)\" --webui-port 8081 \"$SPARK_MASTER_URL\"" &
#     srun --nodes=1 --ntasks=1 -w "$node" bash -lc 'exec "$SPARK_HOME/bin/spark-class" org.apache.spark.deploy.worker.Worker --host "$(hostname -s)" --webui-port 8081 '"$SPARK_MASTER_URL"'' &
  fi
done
# for node in $nodes; do
#   
#   echo "Starting Spark worker on $node"
#   srun --nodes=1 --ntasks=1 -w $node $SPARK_HOME/sbin/start-worker.sh $SPARK_MASTER_URL &
# done

# Wait for all workers to start
sleep 10

echo "Spark cluster started. Holding job to keep cluster alive."

# Trap SIGTERM and SIGINT to clean up cluster on job cancel or end
cleanup() {
  echo "Stopping Spark workers..."
  for node in $nodes; do
    srun --nodes=1 --ntasks=1 -w $node $SPARK_HOME/sbin/stop-worker.sh &
  done
  wait

  if [ "$(hostname)" == "$MASTER_NODE" ]; then
    echo "Stopping Spark master..."
    $SPARK_HOME/sbin/stop-master.sh
  fi

  echo "Cleanup complete."
  exit 0
}

trap cleanup SIGTERM SIGINT

# Hold job to keep cluster alive
while true; do
  sleep 60
done
