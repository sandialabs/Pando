#!/bin/bash

echo "Going into shared directory..."
cd /cscratch || { echo "No /cscratch available. Change this line to be your shared file system."; exit 1; }

echo "Installing the genome..."
wget https://s3-us-west-2.amazonaws.com/human-pangenomics/T2T/CHM13/assemblies/analysis_set/chm13v2.0.fa.gz
echo "Unzipping..."
gunzip chm13v2.0.fa.gz

echo "Creating a file for each chromosome..."
mkdir -p wgsim_in
cd wgsim_in
awk '/^>/{if(seq)print seq > fname; split($0,a," "); fname=substr(a[1],2)".fa"; print $0 > fname; seq=""; next} {seq=seq $0} END{if(seq)print seq > fname}' ../chm13v2.0.fa
echo "Done"

