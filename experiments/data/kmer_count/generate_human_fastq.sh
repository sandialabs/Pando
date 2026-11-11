#!/bin/bash

cd helper-scripts
# ./download_human_genome.sh
# ./wgsim_chromosomes.sh
# find /cscratch/wgsim_out -maxdepth 1 -type f -name '*.fastq' ! -name '*TRUE*' -exec ln -s {} /cscratch/fastq/raw \;
# ./helper-scripts/convert_fastq_to_pando.sh /cscratch/fastq/raw /cscratch/fastq/pando
./convert_fastq_to_pando_parallel.sh /cscratch/fastq/raw /cscratch/fastq/pando

mkdir -p /cscratch/fastq/raw
find /cscratch/wgsim_out -maxdepth 1 -type f -name '*.fastq' ! -name '*TRUE*' -exec ln -s {} /cscratch/fastq/raw \;


