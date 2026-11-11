from pyspark.sql import SparkSession
import sys

def main(input_path, output_path, k):
    spark = SparkSession.builder.appName("KmerInverseIndex").getOrCreate()
    sc = spark.sparkContext

    lines_rdd = sc.textFile(input_path + "/*.fastq")

    reads_rdd = (
        lines_rdd.zipWithIndex()
        .filter(lambda x: x[1] % 4 == 0 or x[1] % 4 == 1)
        .map(lambda x: x[0])
    )

    def pair_reads(partition):
        pending_id = None
        for line in partition:
            if pending_id is None:
                pending_id = line.strip()
            else:
                yield (pending_id, line.strip())
                pending_id = None

    id_seq_rdd = reads_rdd.mapPartitions(pair_reads)

    def build_inverse_index(record):
        read_id, seq = record
        if len(seq) < k:
            return []

        kmer_positions = {}
        for i in range(len(seq) - k + 1):
            kmer = seq[i:i+k]
            if kmer not in kmer_positions:
                kmer_positions[kmer] = []
            kmer_positions[kmer].append(i)

        return [(kmer, f"{read_id}\t{','.join(map(str, positions))}")
                for kmer, positions in kmer_positions.items()]

    inverse_index = id_seq_rdd.flatMap(build_inverse_index)

    inverse_index.saveAsTextFile(output_path)

    spark.stop()

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: inverse_index.py <input_path> <output_path> <k>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    k = int(sys.argv[3])

    main(input_path, output_path, k)
