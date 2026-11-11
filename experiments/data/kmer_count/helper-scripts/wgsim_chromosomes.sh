#!/bin/bash

OutputFname="reads"
r1Len=10000
r2Len=10000
BaseErr=0.01
IndelFrac=0.10
Coverage=30
MutRate=0.0

input_directory="/cscratch/wgsim_in"
OutputLoc="/cscratch/wgsim_out/"

if [[ ! -d "$input_directory" ]]; then
    echo "Error: $input_directory is not a directory."
    exit 1
fi

mkdir -p "$OutputLoc"

find "$input_directory" -maxdepth 1 -type f | sort > /cscratch/wgsim_file_list.txt
num_files=$(wc -l < /cscratch/wgsim_file_list.txt)

if [[ "$num_files" -eq 0 ]]; then
    echo "No input files found in $input_directory"
    exit 1
fi

export OutputLoc OutputFname r1Len r2Len BaseErr IndelFrac Coverage MutRate

srun -N "$num_files" -n "$num_files" --ntasks-per-node=1 bash -c 'file=$(sed -n "$((SLURM_PROCID + 1))p" /cscratch/wgsim_file_list.txt); if [[ -z "$file" || ! -f "$file" ]]; then echo "Task $SLURM_PROCID: invalid file: $file"; exit 1; fi; base_name=$(basename "$file"); name_without_extension="${base_name%.*}"; size=$(stat -c%s "$file"); TotalReads=$((Coverage * size / (r1Len + r2Len))); ./wgsim -R "$IndelFrac" -e "$BaseErr" -1 "$r1Len" -2 "$r2Len" -N "$TotalReads" "$file" "${OutputLoc}${name_without_extension}_R1_L${r1Len}.fastq" "${OutputLoc}${name_without_extension}_R2_L${r2Len}.fastq" "${OutputLoc}${name_without_extension}_R1_L${r1Len}_TRUE.fastq" "${OutputLoc}${name_without_extension}_R2_L${r1Len}_TRUE.fastq" 1>"${OutputLoc}${name_without_extension}_logfile.txt" 2>"${OutputLoc}${name_without_extension}_err.txt"'

rm -f /cscratch/wgsim_file_list.txt




# #!/bin/bash

# OutputLoc="/cscratch/wgsim_out/"
# OutputFname="reads"

# r1Len=150
# r2Len=150
# #This can require some manual configuration to acheive the coverage you want
# #coverage = totalReads * readLength * 2(or r1Len+r2Len if they are different) / length of genome
# #alternatively if not using paired end reads scrap the '* 2' part
# #this is setup for chromosome 21 (length ~48mil) 10x coverage (10 = )
# BaseErr=0.01
# IndelFrac=0.10
# Coverage=50
# MutRate=0.0
# #---------


# # Check if a directory is provided as an argument
# # if [ "$#" -ne 1 ]; then
# #     echo "Usage: $0 <directory>"
# #     exit 1
# # fi

# # # Assign the input directory
# # input_directory="$1"
# input_directory="/cscratch/wgsim_in"

# # Check if the provided argument is a directory
# if [ ! -d "$input_directory" ]; then
#     echo "Error: $input_directory is not a directory."
#     exit 1
# fi

# mkdir -p $OutputLoc

# # Iterate through each file in the directory
# for file in "$input_directory"/*; do
#     if [ -f "$file" ]; then  # Check if it's a regular file
#         base_name=$(basename "$file")
#         name_without_extension="${base_name%.*}"
#         size=$(stat -c%s "$file")
#         TotalReads=$(($Coverage * $size / ($r1Len + $r2Len)))
#         ./wgsim -R $IndelFrac -e $BaseErr -1 $r1Len -2 $r2Len -N $TotalReads $file "${OutputLoc}${name_without_extension}_R1_L${r1Len}.fastq" "${OutputLoc}${name_without_extension}_R2_L${r2Len}.fastq" "${OutputLoc}${name_without_extension}_R1_L${r1Len}_TRUE.fastq" "${OutputLoc}${name_without_extension}_R2_L${r1Len}_TRUE.fastq" 1>"${OutputLoc}${name_without_extension}_logfile.txt" 2>"${OutputLoc}${name_without_extension}_err.txt"
#         
#     fi
# done

