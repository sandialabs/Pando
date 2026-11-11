#!/usr/bin/env python3

import argparse
import os
import sys
import tempfile
import subprocess


def extract_ids(path, out):
    errors = 0
    count = 0
    line_no = 0

    with open(path, "r", errors="replace") as f:
        while True:
            line = f.readline()
            if not line:
                break

            line_no += 1

            if line.rstrip("\n") != "VALUE":
                continue

            read_id = f.readline()
            line_no += 1

            if not read_id:
                print(f"{path}:{line_no}: missing read id after VALUE", file=sys.stderr)
                errors += 1
                break

            read_id = read_id.strip()

            if not read_id.isdigit() or int(read_id) < 1:
                print(f"{path}:{line_no}: invalid read id: {read_id!r}", file=sys.stderr)
                errors += 1
                continue

            out.write(f"{read_id}\n")
            count += 1

    return count, errors


def validate_sorted_ids(id_file):
    sorted_file = id_file + ".sorted"

    subprocess.run(
        ["sort", "-n", id_file, "-o", sorted_file],
        check=True,
    )

    expected = 1
    count = 0
    errors = 0
    max_report = 100

    with open(sorted_file, "r") as f:
        for line in f:
            rid = int(line.strip())
            count += 1

            if rid < expected:
                print(f"duplicate id: {rid}", file=sys.stderr)
                errors += 1
            elif rid > expected:
                print(f"missing id(s): expected {expected}, got {rid}", file=sys.stderr)
                errors += 1
                expected = rid + 1
            else:
                expected += 1

            if errors >= max_report:
                print(f"stopping after {max_report} errors", file=sys.stderr)
                break

    try:
        os.remove(sorted_file)
    except OSError:
        pass

    return count, errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "directory",
        nargs="?",
        default="/cscratch/fastq/pando/pando",
    )
    args = parser.parse_args()

    if not os.path.isdir(args.directory):
        sys.exit(f"not a directory: {args.directory}")

    files = []

    for root, _, names in os.walk(args.directory):
        for name in names:
            if name.endswith(".txt"):
                files.append(os.path.join(root, name))

    files.sort()

    if not files:
        sys.exit(f"no .txt files found in {args.directory}")

    total_ids = 0
    total_errors = 0

    with tempfile.NamedTemporaryFile(
        mode="w",
        delete=False,
        dir=args.directory,
        prefix=".ids_",
        suffix=".txt",
    ) as tmp:
        id_file = tmp.name

        for path in files:
            count, errors = extract_ids(path, tmp)
            total_ids += count
            total_errors += errors

    if total_errors == 0:
        count, errors = validate_sorted_ids(id_file)
        total_errors += errors

    try:
        os.remove(id_file)
    except OSError:
        pass

    print(f"files: {len(files)}")
    print(f"ids: {total_ids}")

    if total_errors:
        print(f"failed: {total_errors} error(s)")
        sys.exit(1)

    print(f"passed: ids are exactly 1..{total_ids}")


if __name__ == "__main__":
    main()
