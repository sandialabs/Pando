# Imports
from timeit import default_timer as timer
import sys
import os
import argparse
import time
# import subprocess

# Argument Parser
parser = argparse.ArgumentParser(description="")
parser.add_argument("-r", "--results_dir", type=str, required=False, help="Path to directory where to save results")
parser.add_argument("-d", "--data_dir", type=str, required=True, help="Path to the data directory")
parser.add_argument("-n", "--test_name", type=str, required=True, help="Name of the test run")
parser.add_argument("-m", "--mesh", type=str, required=True, help="IP,ID of the ParDBClient")
parser.add_argument("-f", "--filters", type=str, required=True, help="Text file with a list of filters on each their own lines")

# Parse arguments
args = parser.parse_args()
RESULTS_DIR = args.results_dir or "./"
DATA_DIR = args.data_dir
TEST_NAME = args.test_name
MESH = args.mesh
FILTER_FILE = args.filters

def wait_for_stable_mesh(client):
    num_neighbors = 0
    while True:
        time.sleep(10)
        n_t = client.num_neighbors()
        if n_t == num_neighbors:
            print("\tMesh stabilized")
            break
        else:
            num_neighbors = n_t

def wait_for_stable_db(client):
    while True:
        last_size = pando.db_size_total(client)    
        time.sleep(10)
        curr_size = pando.db_size_total(client)
        if curr_size != 0 and curr_size == last_size:
            print("\t\tBreaking out")
            break

time.sleep(10)

import pando
client = pando.ParDBClient(MESH)

print("Started Client with Mesh:", MESH)
print("\tWaiting for mesh to be stable...")
wait_for_stable_mesh(client)

print("\tAdding Filter Directory")
client.add_filter_dir('/ascldap/users/grgill/bqs/src/code/build/filters')

print("\tBegin Data Import")

for filename in os.listdir(DATA_DIR):
    filepath = os.path.join(DATA_DIR, filename)
    if os.path.isfile(filepath):
        client.add_db_file(filepath)

# pando.import_db_blocking(client, "/cscratch/longread/")

print("\t\tWaiting for 5 seconds...")
time.sleep(5)
print("\t\tBeginning Import Loop")

# wait_for_stable_db(client)

print("\tData Imported")

# print("\tExporting data...")
# client.export_db("/cscratch/longread/")

wait_for_stable_db(client)

print("\tInstalling Filters from", FILTER_FILE)
# Install all filters
with open(FILTER_FILE, "r") as readable:
   for line in readable:
       filter_name = line.strip()
       if "BTC_tx_vals" in filter_name:
           print("\t\tAdding Bitcoin exchange rates")
           client.add_db_file("../../src/code/test/data/exchange_rates/bitcoin_exchange_rate")
           print("\t\tEnsuring mesh is stable...")
           wait_for_stable_db(client)
       # elif "shortread" in filter_name:
       #    client.add_db_file("../data/fastq_pando.txt") 
       print("\t\tInstalling Filter:", filter_name)
       client.install_filter(filter_name)  # Strip newline and carriage return
       time.sleep(3)

print("\tWaiting 10 seconds before processing...")
time.sleep(10)

print("\tBegin Data Processing")
start = timer()
client.process()  # Async
time.sleep(1)
while client.processing():
    time.sleep(10) 
end = timer()
time_taken = str(end - start)
print("\tEnd Data Processing: ", time_taken)
# Save results
filename = os.path.join(RESULTS_DIR, TEST_NAME) + ".csv" 
with open(filename, "a") as file:
    file.write(time_taken + ",")
print("\tPython Script Complete")
