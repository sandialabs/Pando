#!/bin/bash
set -euo pipefail

manifest=$1
out_dir=$2
script_dir=$3

line=$(sed -n "${SLURM_ARRAY_TASK_ID}p" "$manifest")

idx=$(echo "$line" | cut -f1)
fastq=$(echo "$line" | cut -f2)
start_id=$(echo "$line" | cut -f4)

base=$(basename "$fastq")
base="${base%.fastq}"

python3 "$script_dir/convert_fastq_to_pando_numbered.py" \
  -f "$fastq" \
  -s "$start_id" \
  -o "$out_dir/pando/${idx}_${base}_pando.txt"
