#!/bin/bash

###################
## Slurm parameters

#SBATCH -o log/%j.out
#SBATCH --exclusive

set -euo pipefail

####################
## Cluster parameters

### Network selection:
# Default: resolve the node's normal hostname.
# Optional overrides, in priority order:
#   PANDO_BIND_IP_FILE=/ip
#   PANDO_INTERFACE=eth0
#   PANDO_HOST_SUFFIX=.100g
export PANDO_BIND_IP_FILE="${PANDO_BIND_IP_FILE:-}"
export PANDO_INTERFACE="${PANDO_INTERFACE:-}"
export PANDO_HOST_SUFFIX="${PANDO_HOST_SUFFIX:-}"

### Container and filesystem settings
export PANDO_RUNTIME="${PANDO_RUNTIME:-singularity}"

# Empty means automatically choose a usable directory.
export PANDO_SCRATCH="${PANDO_SCRATCH:-}"
export PANDO_CSCRATCH="${PANDO_CSCRATCH:-}"

export PANDO_EXTRA_BIND="${PANDO_EXTRA_BIND:-}"

####################
## Script and image locations

# Set PANDO_SCRIPT_DIR explicitly if automatic discovery is unsuitable.
if [[ -n "${PANDO_SCRIPT_DIR:-}" ]]; then
    SCRIPT_DIR="$PANDO_SCRIPT_DIR"
else
    SCRIPT_PATH=$(
        scontrol show job "$SLURM_JOB_ID" |
            sed -n 's/^[[:space:]]*Command=//p'
    )

    if [[ -z "$SCRIPT_PATH" ]]; then
        echo "Error: cannot determine script location; set PANDO_SCRIPT_DIR." >&2
        exit 1
    fi

    SCRIPT_DIR=$(dirname "$SCRIPT_PATH")
fi

SCRIPT_DIR=$(cd "$SCRIPT_DIR" && pwd)
PANDO_INTERNAL="$SCRIPT_DIR/pando.internal"
PANDO_PARDB_BIN="${PANDO_PARDB_BIN:-$SCRIPT_DIR/../sifs/pando_pardb.sif}"

####################
## Task layout

# Assumes equally sized nodes and an exclusive whole-node allocation.
# Each process should receive complete cores totaling four hardware threads.
hardware_threads_per_task=4

# Inspect online CPUs on the batch node.
topology=$(LC_ALL=C lscpu -b -p=CPU,CORE,SOCKET)

hardware_threads=$(
    printf '%s\n' "$topology" |
        awk -F, '!/^#/ && NF >= 3 {n++} END {print n+0}'
)

physical_cores=$(
    printf '%s\n' "$topology" |
        awk -F, '!/^#/ && NF >= 3 {print $3 "," $2}' |
        sort -u |
        wc -l
)

# Verify that every physical core has the same number of online threads.
thread_counts=$(
    printf '%s\n' "$topology" |
        awk -F, '
            !/^#/ && NF >= 3 {count[$3 "," $2]++}
            END {for (core in count) print count[core]}
        ' |
        sort -nu
)

if (( physical_cores < 1 || hardware_threads < 1 )) ||
    [[ -z "$thread_counts" || "$thread_counts" == *$'\n'* ]]; then
    echo "Error: missing or nonuniform CPU topology." >&2
    exit 1
fi

threads_per_core="$thread_counts"
cpus_per_node="$SLURM_CPUS_ON_NODE"

if (( hardware_threads_per_task % threads_per_core != 0 )); then
    echo "Error: cannot assign $hardware_threads_per_task hardware threads using complete cores." >&2
    echo "Online hardware threads per core: $threads_per_core" >&2
    exit 1
fi

if (( cpus_per_node == hardware_threads )); then
    # Slurm counts hardware threads.
    cpus_per_task=$hardware_threads_per_task
elif (( cpus_per_node == physical_cores )); then
    # Slurm counts physical cores.
    cpus_per_task=$((hardware_threads_per_task / threads_per_core))
else
    echo "Error: Slurm CPU allocation does not match the whole-node topology." >&2
    echo "Slurm CPUs=$cpus_per_node cores=$physical_cores hardware threads=$hardware_threads" >&2
    exit 1
fi

tasks_per_node=$((cpus_per_node / cpus_per_task))
total_tasks=$((tasks_per_node * SLURM_JOB_NUM_NODES))

if (( tasks_per_node < 1 )); then
    echo "Error: insufficient CPUs per node for one task." >&2
    exit 1
fi

####################
## Pando connection information

node=$(scontrol show hostnames "$SLURM_JOB_NODELIST" | sed -n '1p')

# Resolve the first node's address using the same logic as the workers.
ip=$(
    srun --mpi=none --exact \
        --nodes=1 --ntasks=1 --ntasks-per-node=1 \
        --cpus-per-task=1 --nodelist="$node" \
        bash "$PANDO_INTERNAL" --print-ip
)

if [[ -z "$ip" ]]; then
    echo "Error: no mesh-entry IP address found for $node." >&2
    exit 1
fi

PANDO_MESH_ENTRY="$ip,0"

echo START
date
echo "PANDO_MESH_ENTRY=$PANDO_MESH_ENTRY"
echo "Nodes=$SLURM_JOB_NUM_NODES"
echo "Physical cores per node=$physical_cores"
echo "Hardware threads per node=$hardware_threads"
echo "Hardware threads per core=$threads_per_core"
echo "Target hardware threads per task=$hardware_threads_per_task"
echo "Slurm CPUs per node=$cpus_per_node"
echo "Slurm CPUs per task=$cpus_per_task"
echo "Tasks per node=$tasks_per_node"
echo "Total tasks=$total_tasks"

### Node-local cleanup (best effort)
# These paths are unchanged; cleanup does not cover fallback scratch paths.
srun --mpi=none --exact \
    --nodes="$SLURM_JOB_NUM_NODES" \
    --ntasks="$SLURM_JOB_NUM_NODES" \
    --ntasks-per-node=1 --cpus-per-task=1 \
    bash -c 'sudo -n rm -rf /tmp/elga.ipc* /scratch/bigspace*' \
    || echo "Warning: cleanup failed on one or more nodes; continuing." >&2

### Application launch
# Bind to complete cores, including their available sibling threads.
# --overlap permits the optional analysis step to share these resources.
srun --mpi=none --overlap --exact --unbuffered \
    --nodes="$SLURM_JOB_NUM_NODES" \
    --ntasks="$total_tasks" \
    --ntasks-per-node="$tasks_per_node" \
    --cpus-per-task="$cpus_per_task" \
    --distribution=block:block \
    --cpu-bind=verbose,cores \
    bash "$PANDO_INTERNAL" "$PANDO_MESH_ENTRY" "$PANDO_PARDB_BIN" &
pardb_pid=$!

### Optional analysis launch
# Ensure Pando is ready before connecting, or implement retries in analysis.py.
# Assumes analysis.py is in SCRIPT_DIR; bind additional data paths as needed.
# srun --mpi=none --overlap --exact --unbuffered \
#     --nodes=1 --ntasks=1 \
#     --cpus-per-task="$cpus_per_task" \
#     --cpu-bind=cores \
#     "$PANDO_RUNTIME" exec \
#         --bind "$SCRIPT_DIR:/pando-analysis" \
#         --pwd /pando-analysis \
#         "$SCRIPT_DIR/../sifs/pando_python.sif" \
#         python3 -u ./analysis.py "$ip"

# Wait for Pando to exit; this does not shut it down.
pando_status=0
wait "$pardb_pid" || pando_status=$?

echo FINISH
date
exit "$pando_status"
