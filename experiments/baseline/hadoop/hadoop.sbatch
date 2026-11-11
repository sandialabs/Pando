#!/bin/bash

###################
## Slurm parameters

#SBATCH --ntasks-per-node=1
#SBATCH -o log/%j.out
#SBATCH --exclusive
#SBATCH -c 64
#####################

set -eo pipefail

SCRIPT_PATH=$(scontrol show job "$SLURM_JOBID" | awk -F= '/Command=/{print $2}')
SCRIPT_DIR=$(dirname "$SCRIPT_PATH")

export HADOOP_HOME="$SCRIPT_DIR/hadoop-3.4.1"
export HADOOP_CONF_DIR="${HADOOP_HOME}/etc/hadoop"
export HADOOP_WORKERS="$SCRIPT_DIR/workers.$SLURM_JOBID"
export HADOOP_COMMON_HOME="$HADOOP_HOME"
export HADOOP_HDFS_HOME="$HADOOP_HOME"
export HADOOP_YARN_HOME="$HADOOP_HOME"
export HADOOP_MAPRED_HOME="$HADOOP_HOME"
export JAVA_HOME="/usr/lib/jvm/java-17-openjdk"

all_nodes=$(scontrol show hostnames "$SLURM_JOB_NODELIST")
first_alloc_node=$(printf '%s\n' "$all_nodes" | head -n 1)

namenode="$first_alloc_node"
printf '%s\n' "$all_nodes" > "$HADOOP_WORKERS"

CONFIG_MARKER_STR="# SLURM SPECIFIC CONFIG"

cleanup() {
    set +e

    echo "Stopping Hadoop on worker nodes"
    srun --ntasks="$SLURM_NNODES" --ntasks-per-node=1 bash -lc "
        export HADOOP_HOME='$HADOOP_HOME'
        export HADOOP_CONF_DIR='$HADOOP_CONF_DIR'
        export HADOOP_COMMON_HOME='$HADOOP_COMMON_HOME'
        export HADOOP_HDFS_HOME='$HADOOP_HDFS_HOME'
        export HADOOP_YARN_HOME='$HADOOP_YARN_HOME'
        export HADOOP_MAPRED_HOME='$HADOOP_MAPRED_HOME'
        export JAVA_HOME='$JAVA_HOME'
        '$HADOOP_HOME/bin/yarn' --daemon stop nodemanager || true
        '$HADOOP_HOME/bin/hdfs' --daemon stop datanode || true
        rm -rf /tmp/hadoop* /scratch/hadoop-*-dir /tmp/jetty* || true
    " || true

    echo "Stopping Hadoop on allocated head: $namenode"
    "$HADOOP_HOME/bin/yarn" --daemon stop resourcemanager || true
    "$HADOOP_HOME/bin/hdfs" --daemon stop namenode || true

    sleep 2
    exit 0
}
trap cleanup SIGTERM SIGINT

echo "Namenode: $namenode"
echo "Workers:"
cat "$HADOOP_WORKERS"
echo "Using JAVA_HOME=$JAVA_HOME"

sed -i "/$CONFIG_MARKER_STR/,\$d" "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "$CONFIG_MARKER_STR" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export JAVA_HOME=$JAVA_HOME" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_HOME=$HADOOP_HOME" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_CONF_DIR=$HADOOP_CONF_DIR" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_COMMON_HOME=$HADOOP_COMMON_HOME" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_HDFS_HOME=$HADOOP_HDFS_HOME" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_YARN_HOME=$HADOOP_YARN_HOME" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_MAPRED_HOME=$HADOOP_MAPRED_HOME" >> "$HADOOP_CONF_DIR/hadoop-env.sh"
echo "export HADOOP_SSH_OPTS='-o BatchMode=yes -o StrictHostKeyChecking=no -o ConnectTimeout=10s'" >> "$HADOOP_CONF_DIR/hadoop-env.sh"

touch "$HADOOP_CONF_DIR/yarn-env.sh"
sed -i "/$CONFIG_MARKER_STR/,\$d" "$HADOOP_CONF_DIR/yarn-env.sh"
echo "$CONFIG_MARKER_STR" >> "$HADOOP_CONF_DIR/yarn-env.sh"
echo "export JAVA_HOME=$JAVA_HOME" >> "$HADOOP_CONF_DIR/yarn-env.sh"
echo "export HADOOP_HOME=$HADOOP_HOME" >> "$HADOOP_CONF_DIR/yarn-env.sh"
echo "export HADOOP_CONF_DIR=$HADOOP_CONF_DIR" >> "$HADOOP_CONF_DIR/yarn-env.sh"
echo 'export YARN_OPTS="$YARN_OPTS --add-opens=java.base/java.lang=ALL-UNNAMED --add-opens=java.base/java.lang.invoke=ALL-UNNAMED --add-opens=java.base/java.lang.reflect=ALL-UNNAMED --add-opens=java.base/java.io=ALL-UNNAMED --add-opens=java.base/java.net=ALL-UNNAMED --add-opens=java.base/java.util=ALL-UNNAMED --add-opens=java.base/java.util.concurrent=ALL-UNNAMED"' >> "$HADOOP_CONF_DIR/yarn-env.sh"

echo "Cleaning previous run on worker nodes"
srun --ntasks="$SLURM_NNODES" --ntasks-per-node=1 bash -lc "
    export HADOOP_HOME='$HADOOP_HOME'
    export HADOOP_CONF_DIR='$HADOOP_CONF_DIR'
    export HADOOP_COMMON_HOME='$HADOOP_COMMON_HOME'
    export HADOOP_HDFS_HOME='$HADOOP_HDFS_HOME'
    export HADOOP_YARN_HOME='$HADOOP_YARN_HOME'
    export HADOOP_MAPRED_HOME='$HADOOP_MAPRED_HOME'
    export JAVA_HOME='$JAVA_HOME'
    '$HADOOP_HOME/bin/yarn' --daemon stop nodemanager || true
    '$HADOOP_HOME/bin/hdfs' --daemon stop datanode || true
    rm -rf /tmp/hadoop* /scratch/hadoop-*-dir /tmp/jetty* || true
" || true

cat << EOL > "$HADOOP_CONF_DIR/yarn-site.xml"
<?xml version="1.0"?>
<configuration>
    <property>
        <name>yarn.resourcemanager.hostname</name>
        <value>$namenode</value>
    </property>
    <property>
        <name>yarn.resourcemanager.address</name>
        <value>$namenode:8032</value>
    </property>
    <property>
        <name>yarn.nodemanager.resource.memory-mb</name>
        <value>524288</value>
    </property>
    <property>
        <name>yarn.nodemanager.resource.cpu-vcores</name>
        <value>64</value>
    </property>
    <property>
        <name>yarn.scheduler.maximum-allocation-mb</name>
        <value>524288</value>
    </property>
    <property>
        <name>yarn.scheduler.minimum-allocation-mb</name>
        <value>1024</value>
    </property>
    <property>
        <name>yarn.nodemanager.aux-services</name>
        <value>mapreduce_shuffle</value>
    </property>
    <property>
        <name>yarn.nodemanager.aux-services.mapreduce.shuffle.class</name>
        <value>org.apache.hadoop.mapred.ShuffleHandler</value>
    </property>
</configuration>
EOL

cat << EOL > "$HADOOP_CONF_DIR/hdfs-site.xml"
<?xml version="1.0" encoding="UTF-8"?>
<?xml-stylesheet type="text/xsl" href="configuration.xsl"?>
<configuration>
    <property>
        <name>dfs.namenode.rpc-address</name>
        <value>$namenode:8020</value>
    </property>
    <property>
        <name>dfs.namenode.name.dir</name>
        <value>file:///scratch/hadoop-namenode-dir</value>
    </property>
    <property>
        <name>dfs.datanode.data.dir</name>
        <value>file:///scratch/hadoop-datanode-dir</value>
    </property>
</configuration>
EOL

cat << EOL > "$HADOOP_CONF_DIR/core-site.xml"
<?xml version="1.0" encoding="UTF-8"?>
<?xml-stylesheet type="text/xsl" href="configuration.xsl"?>
<configuration>
    <property>
        <name>fs.defaultFS</name>
        <value>hdfs://$namenode:8020</value>
    </property>
</configuration>
EOL

cat << EOL > "$HADOOP_CONF_DIR/mapred-site.xml"
<?xml version="1.0" encoding="UTF-8"?>
<?xml-stylesheet type="text/xsl" href="configuration.xsl"?>
<configuration>
    <property>
        <name>mapreduce.framework.name</name>
        <value>yarn</value>
    </property>
    <property>
        <name>yarn.app.mapreduce.am.env</name>
        <value>HADOOP_MAPRED_HOME=${HADOOP_HOME}</value>
    </property>
    <property>
        <name>mapreduce.map.env</name>
        <value>HADOOP_MAPRED_HOME=${HADOOP_HOME}</value>
    </property>
    <property>
        <name>mapreduce.reduce.env</name>
        <value>HADOOP_MAPRED_HOME=${HADOOP_HOME}</value>
    </property>
</configuration>
EOL

echo "Formatting namenode on allocated node $namenode"
"$HADOOP_HOME/bin/hdfs" namenode -format -force

echo "Starting namenode on allocated node $namenode"
"$HADOOP_HOME/bin/hdfs" --daemon start namenode

echo "Starting datanodes on workers"
srun --ntasks="$SLURM_NNODES" --ntasks-per-node=1 bash -lc "
    export HADOOP_HOME='$HADOOP_HOME'
    export HADOOP_CONF_DIR='$HADOOP_CONF_DIR'
    export HADOOP_COMMON_HOME='$HADOOP_COMMON_HOME'
    export HADOOP_HDFS_HOME='$HADOOP_HDFS_HOME'
    export HADOOP_YARN_HOME='$HADOOP_YARN_HOME'
    export HADOOP_MAPRED_HOME='$HADOOP_MAPRED_HOME'
    export JAVA_HOME='$JAVA_HOME'
    mkdir -p /scratch/hadoop-datanode-dir
    '$HADOOP_HOME/bin/hdfs' --daemon start datanode
"

echo "Starting YARN"
"$HADOOP_HOME/bin/yarn" --daemon start resourcemanager

srun --ntasks="$SLURM_NNODES" --ntasks-per-node=1 bash -lc "
    export HADOOP_HOME='$HADOOP_HOME'
    export HADOOP_CONF_DIR='$HADOOP_CONF_DIR'
    export HADOOP_COMMON_HOME='$HADOOP_COMMON_HOME'
    export HADOOP_HDFS_HOME='$HADOOP_HDFS_HOME'
    export HADOOP_YARN_HOME='$HADOOP_YARN_HOME'
    export HADOOP_MAPRED_HOME='$HADOOP_MAPRED_HOME'
    export JAVA_HOME='$JAVA_HOME'
    '$HADOOP_HOME/bin/yarn' --daemon start nodemanager
"

sleep 10
"$HADOOP_HOME/bin/hdfs" dfsadmin -report
"$HADOOP_HOME/bin/hdfs" dfs -mkdir -p "/user/$USER/input/" || true
"$HADOOP_HOME/bin/hdfs" dfs -rm -r "/user/$USER/output/" || true

echo "CREATING FILE at $SCRIPT_DIR/hadoop_setup_complete"
touch "$SCRIPT_DIR/hadoop_setup_complete"

sleep infinity
