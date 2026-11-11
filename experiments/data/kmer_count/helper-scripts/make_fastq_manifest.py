#!/usr/bin/env python3

import argparse
import os
import sys
from concurrent.futures import ProcessPoolExecutor


def count_fastq_reads(path):
    line_count = 0

    with open(path, "r") as f:
        for _ in f:
            line_count += 1

    if line_count % 4 != 0:
        raise RuntimeError(f"FASTQ line count is not divisible by 4: {path}")

    return path, line_count // 4


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-d", "--directory", required=True)
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("-j", "--jobs", type=int, default=8)
    args = parser.parse_args()

    fastq_files = []

    for root, _, files in os.walk(args.directory):
        for name in files:
            if name.endswith(".fastq") and "TRUE" not in name:
                fastq_files.append(os.path.abspath(os.path.join(root, name)))

    fastq_files.sort()

    if not fastq_files:
        sys.exit("No FASTQ files found")

    with ProcessPoolExecutor(max_workers=args.jobs) as executor:
        results = list(executor.map(count_fastq_reads, fastq_files))

    read_counts = dict(results)

    next_id = 1

    with open(args.output, "w") as out:
        for idx, path in enumerate(fastq_files, start=1):
            read_count = read_counts[path]
            start_id = next_id
            end_id = start_id + read_count - 1
            out.write(f"{idx}\t{path}\t{read_count}\t{start_id}\t{end_id}\n")
            next_id = end_id + 1

    print(f"FASTQ files: {len(fastq_files)}")
    print(f"Total reads: {next_id - 1}")


if __name__ == "__main__":
    main()
