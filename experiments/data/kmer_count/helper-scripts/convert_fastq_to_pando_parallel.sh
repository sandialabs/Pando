#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

in_dir=$1
out_dir=$2

MAX_PARALLEL=${MAX_PARALLEL:-8}
COUNT_JOBS=${COUNT_JOBS:-8}

mkdir -p "$out_dir"
mkdir -p "$out_dir/pando"
mkdir -p "$out_dir/logs"

manifest="$out_dir/fastq_manifest.tsv"

python3 "$SCRIPT_DIR/make_fastq_manifest.py" \
  -d "$in_dir" \
  -o "$manifest" \
  -j "$COUNT_JOBS"

num_files=$(wc -l < "$manifest")

if [ "$num_files" -eq 0 ]; then
  echo "No FASTQ files found"
  exit 1
fi

job_id=$(sbatch \
  --parsable \
  --array=1-"$num_files"%"$MAX_PARALLEL" \
  --nodes=1 \
  --ntasks=1 \
  --job-name=convert_pando \
  --output="$out_dir/logs/convert_pando_%A_%a.out" \
  --error="$out_dir/logs/convert_pando_%A_%a.err" \
  "$SCRIPT_DIR/convert_one_manifest_entry.sh" \
  "$manifest" \
  "$out_dir" \
  "$SCRIPT_DIR")

echo "Submitted job array: $job_id"
echo "Waiting for jobs to finish..."

while squeue -h -j "$job_id" | grep -q .; do
  sleep 10
done

echo "Pando found at $out_dir/pando"
