from pyspark.sql import SparkSession
from pyspark.sql.functions import split, size, col

def main():
    import sys

    if len(sys.argv) != 3:
        print("Usage: arbitrary_join.py <input_path> <output_path> <join_percentage>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    join_percentage = sys.arv[3]

    spark = SparkSession.builder.appName("Arbitrary Joins").getOrCreate()
    sc = spark.sparkContext

    lines = sc.textFile(input_path)

    parsed = lines.map(lambda line: line.split(",")).cache()

    table1 = (
        parsed
        .filter(lambda parts: len(parts) == 3)
        .map(lambda parts: (parts[1], (parts[0], int(parts[2]))))
        .filter(lambda x: x[1][1] <= join_percentage)
        .map(lambda x: (x[0], x[1][0]))
    )

    table2 = (
        parsed
        .filter(lambda parts: len(parts) == 2)
        .map(lambda parts: (parts[0], parts[1]))
    )

    joined = table1.join(table2)

    output = joined.map(lambda x: f"{x[1][0]}\t{x[0]}\t{x[1][1]}")

    output.saveAsTextFile(output_path)

    spark.stop()

if __name__ == "__main__":
    main()
