#!/usr/bin/env python3

import argparse
import sys


def convert_fastq_to_pando(fastq_file, start_id, output_file):
    read_id = start_id

    with open(fastq_file, "r") as inp, open(output_file, "w") as out:
        while True:
            header = inp.readline()

            if not header:
                break

            seq = inp.readline()
            plus = inp.readline()
            qual = inp.readline()

            if not seq or not plus or not qual:
                sys.exit(f"Error: Truncated FASTQ record in {fastq_file}")

            seq = seq.rstrip("\n")
            plus = plus.rstrip("\n")
            qual = qual.rstrip("\n")

            out.write("TAGS\n")
            out.write("read\n")
            out.write("VALUE\n")
            out.write(f"{read_id}\n")
            out.write(f"{seq}\n")
            out.write(f"{plus}\n")
            out.write(f"{qual}\n")
            out.write("END\n")

            read_id += 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-f", "--fastq-file", required=True)
    parser.add_argument("-s", "--start-id", type=int, required=True)
    parser.add_argument("-o", "--output-file", required=True)
    args = parser.parse_args()

    if args.start_id < 1:
        sys.exit("Error: start-id must be >= 1")

    convert_fastq_to_pando(args.fastq_file, args.start_id, args.output_file)


if __name__ == "__main__":
    main()
