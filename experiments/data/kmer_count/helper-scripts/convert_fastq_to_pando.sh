#!/bin/bash
SCRIPT_DIR=$(pwd)

in_dir=$1
out_dir=$2

mkdir -p $out_dir 

find $in_dir -maxdepth 1 -type f -name '*.fastq' ! -name '*TRUE*' -exec ln -s {} $out_dir \;

AVAILABLE_NODE_COUNT=$(sinfo -N -p compute | grep idle | awk '{print $1}' | wc -l)

cd $out_dir 
mkdir -p temp && cd temp

files=($out_dir/*.fastq)
file_count=${#files[@]}
NUM_FASTQ_FILES=$file_count

####
total_partitions=$AVAILABLE_NODE_COUNT

# Calculate base files per partition and remainder
files_per_partition=$(( NUM_FASTQ_FILES / total_partitions ))
remainder=$(( NUM_FASTQ_FILES % total_partitions ))

echo "Available nodes: $AVAILABLE_NODE_COUNT"
echo "Total files: $NUM_FASTQ_FILES"
echo "Files per partition: $files_per_partition"

# Create partition directories
for i in $(seq 1 $total_partitions); do
  mkdir -p "temp$i"
done

idx=0
for partition in $(seq 1 $total_partitions); do
  # Add one extra file to the first 'remainder' partitions
  count=$files_per_partition
  if [ $partition -le $remainder ]; then
    count=$((count + 1))
  fi

  # Move 'count' files to current partition directory
  for (( j=0; j<count; j++ )); do
    if [ $idx -lt $NUM_FASTQ_FILES ]; then
      mv "${files[$idx]}" "temp${partition}/"
      idx=$((idx + 1))
    fi
  done
done

####


cd $out_dir/temp
DIR_LIST=$(ls -d temp*)

job_ids=()

for DIR in $DIR_LIST; do
  job_output=$(sbatch --nodes=1 --ntasks=1 --job-name=convert_pando_$DIR --parsable --wrap="python3 $SCRIPT_DIR/convert_fastq_to_pando.py -d $out_dir/temp/$DIR")
  job_ids+=("$job_output")
done


echo "Submitted jobs: ${job_ids[*]}"

# Function to check if jobs are still running or pending
jobs_running() {
  squeue -h -j "$(IFS=,; echo "${job_ids[*]}")" | grep -q .
}

# Wait until all jobs complete
echo "Waiting for jobs to finish..."
while jobs_running; do
  sleep 10
done

echo "All jobs completed."

echo "Moving pando files"
mkdir -p $out_dir/pando
find $out_dir/temp -type f -name '*_pando_*' -exec mv -t $out_dir/pando {} +

rm -r $out_dir/temp

echo "Pando found at $out_dir/pando"



