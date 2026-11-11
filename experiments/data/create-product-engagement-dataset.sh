#!/bin/bash

# Output two CSV files
# Table 1:
#    USER_ID, PRODUCT_ID, ENGAGEMENT_SCORE
# Table 2:
#    PRODUCT_ID, PRODUCT_SPECS

NUM_PRODUCT_IDS=100000000
PRODUCT_SPEC_MAX=1000000
NUM_USERS=100000
OUT_DIR="/scratch/product_engagement_dataset"
PRODUCT_TABLE="$OUT_DIR/product_table"
USER_TABLE="$OUT_DIR/user_table"
NUM_FILES=100

mkdir -p "$OUT_DIR"

echo "generating CSV tables"
for i in $(seq 1 $NUM_FILES);
do
    # Generate Table 1 (USER_ID, PRODUCT_ID, ENGAGEMENT_SCORE)
    # USER_IDs are incrementing integers, PRODUCT_ID is a product ID from table 2, and ENGAGEMENT_SCORE is an integer between 1 and 100
    shuf -r -i 1-$NUM_PRODUCT_IDS -n $NUM_USERS --random-source <(yes $i) | paste -d, - <(shuf -r -i 1-100 -n $NUM_USERS --random-source <(yes $i)) | awk '{print NR "," $0}' > "${USER_TABLE}.${i}.csv" &

    # Generate Table 2 (PRODUCT_ID, PRODUCT_SPEC)
    # The PRODUCT_IDs are just incrementing integers, and the PRODUCT_SPEC is simply a random integer
    shuf -r -i 1-$PRODUCT_SPEC_MAX -n $NUM_PRODUCT_IDS --random-source <(yes $i) | awk '{print NR "," $0}' > "${PRODUCT_TABLE}.${i}.csv" &
done

wait
echo "done generating CSV tables, converting to pando format"

# Convert to pando ingestable format
for i in $(seq 1 $NUM_FILES);
do
    awk '{print "TAGS\nproduct_engagement_table1\nVALUE\n" $0 "\nEND"}' "${USER_TABLE}.${i}.csv" > "${USER_TABLE}.${i}.pando" &
    awk '{print "TAGS\nproduct_engagement_table2\nVALUE\n" $0 "\nEND"}' "${PRODUCT_TABLE}.${i}.csv" > "${PRODUCT_TABLE}.${i}.pando" &
done

wait


