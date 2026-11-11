#!/bin/bash

s=$3 # size in gb
DIRECTORY=$1
OUT_DIR=$2

# Directory containing the files
# DIRECTORY="/cscratch/product_engagement_dataset/pando"
# Target size in bytes (200 GB)
TARGET_SIZE=$(($s * 1024 * 1024 * 1024))

# Initialize variables
total_size=0
selected_files=()

# Get a list of files in the directory and shuffle them
mapfile -t files < <(find "$DIRECTORY" -type f | shuf)

# Iterate through the shuffled list of files
for file in "${files[@]}"; do
    # Get the size of the current file
    file_size=$(stat -c%s "$file")
    
    # Add the file to the selected list and update the total size
    selected_files+=("$file")
    total_size=$((total_size + file_size))

    # Check if adding this file exceeds the target size
    if (( total_size > TARGET_SIZE )); then
        break
    fi
done

# Output the selected files and their total size
echo "Selected files (total size: $total_size bytes):"
out_dir="$OUT_DIR/${s}G"
mkdir "$out_dir"
for file in "${selected_files[@]}"; do
    echo "$file"
    ln "$file" "$out_dir"
done

echo "Total size in GB: $(echo "scale=2; $total_size / 1024 / 1024 / 1024" | bc)"

