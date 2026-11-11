#!/bin/python3
import argparse
import random
import sys

# Argument Parser
parser = argparse.ArgumentParser(description="")
parser.add_argument("-d", "--directory", type=str, required=False, help="Path to directory containing wgsim output, exclusive with -f")
parser.add_argument("-f", "--fastq_file", type=str, required=False, help="Path to a single fastq file, exclusive with -d")

# Parse arguments
args = parser.parse_args()
DIR = args.directory or None
FILE = args.fastq_file or None

if (DIR and FILE) or (not DIR and not FILE):
    sys.exit("Error: Invalid Arguments, either pass in a fastq file or a directory of wgsim output, not both")

def read_entries_from_file(filename):
    entries = []
    current_entry = []

    with open(filename, 'r') as file:
        for line in file:
            line = line.strip()  # Remove leading/trailing whitespace
            if line.startswith('@'):
                # If current_entry is not empty, save it to entries
                if current_entry:
                    entries.append('\n'.join(current_entry))
                    current_entry = []  # Reset for the next entry
            current_entry.append(line)

        # Add the last entry if it exists
        if current_entry:
            entries.append('\n'.join(current_entry))

    return entries


def convert_to_pando(entries: list):
    tags = "TAGS\nread\nVALUE\n"
    for i in range(len(entries)):
        entries[i] = tags + entries[i] + "\nEND\n"


def write_to_file(entries: list):
    if FILE:
        out_file = FILE.split(".")[0] + ".txt"
    else:
        r = random.randint(10**9, 10**10 - 1)
        out_file = "fastq_pando_" + str(r) + ".txt"
    with open(out_file, "w") as out:
        for entry in entries:
            out.write(entry)


if DIR:
    import os
    fastq_files = []
    for root, dirs, files in os.walk(DIR):
        for file in files:
            if file.endswith('.fastq'):
                with open(os.path.join(root, file), 'r') as f:
                    if "TRUE" not in f.read():
                        fastq_files.append(os.path.join(root, file))
    total_entries = []
    for file in fastq_files:
        entries = read_entries_from_file(file)
        convert_to_pando(entries)
        total_entries.extend(entries)
    write_to_file(total_entries)


if FILE:
    entries = read_entries_from_file(FILE)
    convert_to_pando(entries)
    write_to_file(entries)



