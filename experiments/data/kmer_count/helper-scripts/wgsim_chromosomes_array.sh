#!/bin/bash
set -euo pipefail

OutputFname="reads"
r1Len=10000
r2Len=10000
BaseErr=0.01
IndelFrac=0.10
Coverage=30
MutRate=0.0

input_directory="/cscratch/wgsim_in"
OutputLoc="/cscratch/wgsim_out"
file_list="/cscratch/wgsim_file_list.txt"

MAX_PARALLEL=${MAX_PARALLEL:-8}

if [[ ! -d "$input_directory" ]]; then
    echo "Error: $input_directory is not a directory."
    exit 1
fi

mkdir -p "$OutputLoc"

find "$input_directory" -maxdepth 1 -type f | sort > "$file_list"
num_files=$(wc -l < "$file_list")

if [[ "$num_files" -eq 0 ]]; then
    echo "No input files found in $input_directory"
    exit 1
fi

job_id=$(sbatch \
    --parsable \
    --array=1-"$num_files"%"$MAX_PARALLEL" \
    --nodes=1 \
    --ntasks=1 \
    --job-name=wgsim_chr \
    --output="${OutputLoc}/wgsim_%A_%a.out" \
    --error="${OutputLoc}/wgsim_%A_%a.err" \
    --export=ALL,OutputLoc="$OutputLoc",r1Len="$r1Len",r2Len="$r2Len",BaseErr="$BaseErr",IndelFrac="$IndelFrac",Coverage="$Coverage",MutRate="$MutRate",FILE_LIST="$file_list" \
    --wrap='
        set -euo pipefail

        file=$(sed -n "${SLURM_ARRAY_TASK_ID}p" "$FILE_LIST")

        if [[ -z "$file" || ! -f "$file" ]]; then
            echo "Task $SLURM_ARRAY_TASK_ID: invalid file: $file"
            exit 1
        fi

        base_name=$(basename "$file")
        name_without_extension="${base_name%.*}"
        size=$(stat -c%s "$file")
        TotalReads=$((Coverage * size / (r1Len + r2Len)))

        ./wgsim \
            -R "$IndelFrac" \
            -e "$BaseErr" \
            -1 "$r1Len" \
            -2 "$r2Len" \
            -N "$TotalReads" \
            "$file" \
            "${OutputLoc}/${name_without_extension}_R1_L${r1Len}.fastq" \
            "${OutputLoc}/${name_without_extension}_R2_L${r2Len}.fastq" \
            "${OutputLoc}/${name_without_extension}_R1_L${r1Len}_TRUE.fastq" \
            "${OutputLoc}/${name_without_extension}_R2_L${r2Len}_TRUE.fastq" \
            1>"${OutputLoc}/${name_without_extension}_logfile.txt" \
            2>"${OutputLoc}/${name_without_extension}_err.txt"
    ')

echo "Submitted wgsim array job: $job_id"
echo "Input files: $num_files"
echo "Max concurrent nodes: $MAX_PARALLEL"
