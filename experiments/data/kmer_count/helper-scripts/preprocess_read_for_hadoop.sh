#!/bin/bash

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <input_fastq> <output_csv>"
    exit 1
fi

input_fastq="$1"
output_csv="$2"

awk 'NR % 4 == 1 { id = $0 } NR % 4 == 2 { print id "," $0 }' "$input_fastq" > "$output_csv"
