from pyspark.sql import SparkSession
import sys

def extract_kmers(read, k):
    """Extract all k-mers of length k from a DNA read."""
    read = read.upper()
    length = len(read)
    for i in range(length - k + 1):
        kmer = read[i:i+k]
        if 'N' in kmer:
            continue  # skip ambiguous bases
        yield kmer

def main(input_path, output_path, k):
    spark = SparkSession.builder.appName("KmerCount").getOrCreate()
    sc = spark.sparkContext

    reads_rdd = sc.textFile(input_path + "/*.fastq")

    def kmer_mapper(partition):
        for read in partition:
            yield from extract_kmers(read, k)

    kmers_rdd = reads_rdd.mapPartitions(kmer_mapper)

    kmer_counts = kmers_rdd.map(lambda kmer: (kmer, 1)) \
                           .reduceByKey(lambda a, b: a + b)

    kmer_counts = kmer_counts.repartition(sc.defaultParallelism * 2)

    # Save output as text file with tab-separated kmer and count
    kmer_counts.map(lambda x: f"{x[0]}\t{x[1]}").saveAsTextFile(output_path)

    spark.stop()

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: kmer_count.py <input_path> <output_path> <k>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    k = int(sys.argv[3])

    main(input_path, output_path, k)

