#!/bin/bash

########################################################################################
# Main reproducibility driver.
#
# This script calls the individual scripts in ./repro-scripts in the same high-level
# order as the original monolithic reproducibility script.
########################################################################################

HOME_DIR=$(pwd)
REPRO_SCRIPTS="$HOME_DIR/run-scripts"

NUM_FULL_POWER=1
NUM_SCAL=1

fasta_files=(
    chr1.fa chr10.fa chr11.fa chr12.fa chr13.fa chr14.fa chr15.fa chr16.fa chr17.fa chr18.fa
    chr19.fa chr2.fa chr20.fa chr21.fa chr22.fa chr3.fa chr4.fa chr5.fa chr6.fa chr7.fa
    chr8.fa chr9.fa chrM.fa chrX.fa chrY.fa
)

########################################################################################
# Downloading DNA Data
########################################################################################

echo "Checking for existence of human genome in /cscratch/wgsim_in..."

missing=false
for f in "${fasta_files[@]}"; do
    if [[ ! -f "/cscratch/wgsim_in/$f" ]]; then
        echo "Missing: $f"
        missing=true
    fi
done

if $missing; then
    echo "Files missing... clearing /cscratch/wgsim_in in 5 seconds..."
    echo 5 && sleep 1
    echo 4 && sleep 1
    echo 3 && sleep 1
    echo 2 && sleep 1
    echo 1 && sleep 1
    echo "Removed /cscratch/wgsim_in"
    rm -rf /cscratch/wgsim_in
    echo "Downloading human genome..."
    cd "$HOME_DIR/data/kmer_count/helper-scripts"
    ./download_human_genome.sh
else
    echo "All *.fa files found... skipping this step."
fi

cd "$HOME_DIR"

########################################################################################
# Product Engagement Dataset
########################################################################################

for i in {1..1}; do
    echo "Generating product engagement dataset..."
    bash "$REPRO_SCRIPTS/generate-engagement.sh"

    echo "Preparing to run arbitrary join jobs..."
    sleep 5

    echo "======= Product Engagement Scalability Experiments ======="

    for k in $(seq 1 "$NUM_SCAL"); do
        echo "Beginning product engagement scalability iteration $k of $NUM_SCAL..."

        echo -e "\n======= BQS Product Engagement Weak Scalability =======\n"
        bash "$REPRO_SCRIPTS/bqs-weak-scalability-product-engagement.sh"

        echo -e "\n======= BQS Product Engagement Strong Scalability =======\n"
        bash "$REPRO_SCRIPTS/bqs-strong-scalability-product-engagement.sh"

        echo -e "\n======= Hadoop Product Engagement Weak Scalability =======\n"
        bash "$REPRO_SCRIPTS/hadoop-weak-scalability-product-engagement.sh"

        echo -e "\n======= Hadoop Product Engagement Strong Scalability =======\n"
        bash "$REPRO_SCRIPTS/hadoop-strong-scalability-product-engagement.sh"

        echo -e "\n======= Spark Product Engagement Weak Scalability =======\n"
        bash "$REPRO_SCRIPTS/spark-weak-scalability-product-engagement.sh"

        echo -e "\n======= Spark Product Engagement Strong Scalability =======\n"
        bash "$REPRO_SCRIPTS/spark-strong-scalability-product-engagement.sh"
    done

    echo "======= Product Engagement Full Power Experiments ======="

    for k in $(seq 1 "$NUM_FULL_POWER"); do
        echo "Beginning product engagement full power iteration $k of $NUM_FULL_POWER..."

        echo "============= BQS ============="
        bash "$REPRO_SCRIPTS/bqs-full-product-engagement.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "============= Hadoop ============="
        bash "$REPRO_SCRIPTS/hadoop-full-product-engagement.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "============= Spark ============="
        bash "$REPRO_SCRIPTS/spark-full-product-engagement.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60
    done
done

########################################################################################
# K-mer Counting Dataset
########################################################################################

for i in {1..1}; do
    echo "Generating k-mer count dataset..."
    bash "$REPRO_SCRIPTS/generate-kmer-count.sh"

    echo "Preparing to run kmer count jobs..."
    sleep 5

    echo "======= K-mer Count Scalability Experiments ======="

    for k in $(seq 1 "$NUM_SCAL"); do
        echo "Beginning k-mer count scalability iteration $k of $NUM_SCAL..."

        echo -e "\n======= BQS K-mer Count Weak Scalability =======\n"
        bash "$REPRO_SCRIPTS/bqs-weak-scalability-kmer-count.sh"

        echo -e "\n======= BQS K-mer Count Strong Scalability =======\n"
        bash "$REPRO_SCRIPTS/bqs-strong-scalability-kmer-count.sh"

        echo -e "\n======= Hadoop K-mer Count Weak Scalability =======\n"
        bash "$REPRO_SCRIPTS/hadoop-weak-scalability-kmer-count.sh"

        echo -e "\n======= Hadoop K-mer Count Strong Scalability =======\n"
        bash "$REPRO_SCRIPTS/hadoop-strong-scalability-kmer-count.sh"

        echo -e "\n======= Spark K-mer Count Weak Scalability =======\n"
        bash "$REPRO_SCRIPTS/spark-weak-scalability-kmer-count.sh"

        echo -e "\n======= Spark K-mer Count Strong Scalability =======\n"
        bash "$REPRO_SCRIPTS/spark-strong-scalability-kmer-count.sh"
    done

    echo "======= K-mer Count Full Power Experiments ======="

    for k in $(seq 1 "$NUM_FULL_POWER"); do
        echo "Beginning k-mer count full power iteration $k of $NUM_FULL_POWER..."

        echo "Running a kmer count job with BQS..."
        bash "$REPRO_SCRIPTS/bqs-full-kmer-count.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "Running a kmer count job with Hadoop..."
        bash "$REPRO_SCRIPTS/hadoop-full-kmer-count.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "Running a kmer count job with Spark..."
        bash "$REPRO_SCRIPTS/spark-full-kmer-count.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "Switching to k-mer inverse indexing..."
        sleep 1

        echo "Running a kmer inverse indexing job with BQS..."
        bash "$REPRO_SCRIPTS/bqs-full-kmer-inv-index.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "Running a kmer inverse indexing job with Hadoop..."
        bash "$REPRO_SCRIPTS/hadoop-full-kmer-inv-index.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60

        echo "Running a kmer inverse indexing job with Spark..."
        bash "$REPRO_SCRIPTS/spark-full-kmer-inv-index.sh"

        echo "Waiting for 60 seconds before running next experiment..."
        sleep 60
    done
done
