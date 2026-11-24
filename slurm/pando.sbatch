#!/bin/bash

###################
## Slurm parameters

#SBATCH -o log/%j.out
#SBATCH --exclusive
#SBATCH -c 4

set -euo pipefail

#####################

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

### Script and image locations
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

### Task layout
# Assumes equally sized nodes and an exclusive whole-node allocation.
cpus_per_task="${SLURM_CPUS_PER_TASK:-4}"
cpus_per_node="$SLURM_CPUS_ON_NODE"
tasks_per_node=$((cpus_per_node / cpus_per_task))
total_tasks=$((tasks_per_node * SLURM_JOB_NUM_NODES))

if (( tasks_per_node < 1 )); then
    echo "Error: insufficient CPUs per node for one task." >&2
    exit 1
fi

### Pando connection information
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
echo "CPUs per node=$cpus_per_node"
echo "CPUs per task=$cpus_per_task"
echo "Tasks per node=$tasks_per_node"
echo "Total tasks=$total_tasks"

### Node-local cleanup (best effort)
srun --mpi=none --exact \
    --nodes="$SLURM_JOB_NUM_NODES" \
    --ntasks="$SLURM_JOB_NUM_NODES" \
    --ntasks-per-node=1 --cpus-per-task=1 \
    bash -c 'sudo -n rm -rf /tmp/elga.ipc* /scratch/bigspace*' \
    || echo "Warning: cleanup failed on one or more nodes; continuing." >&2

### Application launch
srun --mpi=none --overlap --exact --unbuffered \
    --nodes="$SLURM_JOB_NUM_NODES" \
    --ntasks="$total_tasks" \
    --ntasks-per-node="$tasks_per_node" \
    --cpus-per-task="$cpus_per_task" \
    bash "$PANDO_INTERNAL" "$PANDO_MESH_ENTRY" "$PANDO_PARDB_BIN" &
pardb_pid=$!

### Optional analysis launch
# Ensure Pando is ready before connecting, or implement retries in analysis.py.
# srun --mpi=none --overlap --exact --unbuffered \
#     --nodes=1 --ntasks=1 --cpus-per-task=4 \
#     "${PANDO_RUNTIME:-singularity}" exec \
#         "$SCRIPT_DIR/../sifs/pando_python.sif" \
#         python3 -u "$SCRIPT_DIR/analysis.py" "$ip"

# Wait for Pando to exit; this does not shut it down.
wait "$pardb_pid"

echo FINISH
date
