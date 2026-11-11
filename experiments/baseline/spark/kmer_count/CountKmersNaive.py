from pyspark.sql import SparkSession
from pyspark.sql.functions import explode, col
from pyspark.sql.types import StringType, ArrayType
import sys

def extract_kmers(sequence, k):
    """Extract all k-mers from a given sequence."""
    seq_len = len(sequence)
    if seq_len < k:
        return []
    return [sequence[i:i+k] for i in range(seq_len - k + 1)]

def main(input_path, output_path, k):
    spark = SparkSession.builder.appName("KmerCount").getOrCreate()

    # Register UDF to extract k-mers
    from pyspark.sql.functions import udf
    extract_kmers_udf = udf(lambda seq: extract_kmers(seq, k), ArrayType(StringType()))

    # Read input sequences (one per line)
    reads_df = spark.read.text(input_path).toDF("sequence")

    # Extract k-mers as array column
    kmers_df = reads_df.withColumn("kmers", extract_kmers_udf(col("sequence")))

    # Explode k-mers into individual rows
    exploded_kmers = kmers_df.select(explode(col("kmers")).alias("kmer"))

    # Count occurrences of each k-mer
    kmer_counts = exploded_kmers.groupBy("kmer").count()

    # Save results as CSV or text
    kmer_counts.write.mode("overwrite").csv(output_path)

    spark.stop()

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: kmer_count.py <input_path> <output_path> <k>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    k = int(sys.argv[3])

    main(input_path, output_path, k)
