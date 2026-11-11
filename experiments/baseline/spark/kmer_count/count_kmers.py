from pyspark.sql import SparkSession
import sys

def extract_kmers_from_read(read, k):
    length = len(read)
    for i in range(length - k + 1):
        yield read[i:i+k]

def main(input_path, output_path, k):
    spark = SparkSession.builder.appName("KmerCount").getOrCreate()
    sc = spark.sparkContext

    # Read all lines from FASTQ files
    lines_rdd = sc.textFile(input_path + "/*.fastq")

    # Extract sequence lines (line index 1 in each 4-line group)
    reads_rdd = lines_rdd.zipWithIndex() \
                         .filter(lambda x: x[1] % 4 == 1) \
                         .map(lambda x: x[0])

    def kmer_mapper(partition):
        for read in partition:
            # No upper() call here to save CPU cycles; assume input is uppercase or consistent
            # If needed, uncomment the next line:
            # read = read.upper()
            length = len(read)
            for i in range(length - k + 1):
                yield read[i:i+k]

    kmers_rdd = reads_rdd.mapPartitions(kmer_mapper)

    kmer_counts = kmers_rdd.map(lambda kmer: (kmer, 1)) \
                           .reduceByKey(lambda a, b: a + b)

#     # Repartition output to improve parallelism on write
#     kmer_counts = kmer_counts.repartition(sc.defaultParallelism * 2)

#     # Save output as tab-separated text file
#     kmer_counts.map(lambda x: f"{x[0]}\t{x[1]}").saveAsTextFile(output_path)
    kmer_counts.count()  # Force spark to evaluate since it is lazy

    spark.stop()

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: kmer_count.py <input_path> <output_path> <k>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] 
    k = int(sys.argv[3])

    main(input_path, output_path, k)
