# Pando

Pando is a cross-blockchain analysis system.

## Cloning the Repo
This repository uses git submodules, therefore, use the following when
cloning:

```bash
git clone ssh://repo/pando-ldrd/pando.git
cd pando
git submodule update --init
```
## Building Images

Pando runs through a number of singularity containers created through either `docker` or `podman`. 
To build Pando, make sure you have installed `singularity` and either `docker` or `podman`.

Then, to build all containers, run:

```bash
./build_images.sh
```

## Running in Dev Mode

For developers working on core Pando code or developing Pando filters, enter the development singularity container by running:

```bash
apptainer run -B $(pwd) -B /scratch ./sifs/pando_dev.sif
```

Then, make any desired changes to filters and/or source code, then compile by running:

```bash
cd code/build
cmake ..
make -j 32
```

Since we use test-driven development, the best way to test your code is by writing a formal test case. These can be found in `code/test`. Choose one that looks similar to what you are trying to test, copy it, and modify it for your needs. To run the test case, run:

```bash
cd code/build
./tests-bin/<test-file> . ../test
```

Or, to run all test cases, you run:

```bash
make test
```

## Running for Production (via Slurm)

This assumes you have a cluster of machines with Pando installed on each.
It assumes you have a shared drive located in `/cscratch`.

### Starting the DB

To start a Pando cluster, go to the `slurm` directory in the `pando` repo.
Run the following command to start a cluster across 5 different machines, each with the default (currently 16) number of DBs per machine, for a total of 80 individual databases.

```bash
sbatch -N 5 pardb.sbatch
```

To view the database, you can run the `pando_top` sif file, giving it an argument of the IP address and ID of any Pando node.
To find a "Pando node", run `squeue`, view the nodelist for the `pardb.sb`, and get the IP address of one of those nodes.
Example Usage:

```bash
$ squeue
             JOBID PARTITION     NAME     USER ST       TIME  NODES NODELIST(REASON)
             24   compute pardb.sb  username  R       4:11      3 node[94-46]

$ host node94
node94 has address 10.0.0.94

./sifs/pando_top.sif 10.0.0.94,0
```


### Ingesting Data

There are a few different ways to ingest data into Pando:

1. Ingest a blockchain for the first time, or update the data for a previously snapshotted blockchain (see [ingestion instructions](ingestion.md))
2. Importing previously exported (snapshotted) data.  
    1.1. This can be done either from the Python API or with the `import_db` command on the `pando_client`.  
    1.2. It expects an argument of the directory full of files with data in the format exported via the `export_db` command
3. Adding a file manually via the `add_db_file` command

### Jupyter-Notebook Analysis

You can use the jupyter notebook interface to run filters and perform operations on the data.
Start a jupyter notebook by launching the `pando_jupyter.sif` file, and browsing to the URL in a browser (NOTE: there is a good chance you will have to forward port 8888 to a desktop machine. To do this, you can use `ssh -L8888:localhost:8888 <hostname>`)

TODO: Update this with some actual documentation

NOTE: the easiest way to see the commands available to you is to use jupyter notebook's tab completion

Some example code that could be part of the jupyter notebook:

```python
import pando_api
c = pando_api.ParDBClient("10.0.0.94,0")
c.add_filter_dir("/filters")
c.install_filter("BTC_block_to_tx")
c.install_filter("BTC_block_to_txtime")
c.install_filter("exchange_rates")
c.install_filter("BTC_tx_vals")
c.add_db_file("/home/user/pando/code/test/data/exchange_rates/bitcoin_exchange_rate")
c.process()
```

## Building a New Filter

To build a new filter, the easiest way is to use another filter as a template and modify it for your own purposes. Filters are stored in `code/filters`. There are a few key functions that your filter needs to implement to work with Pando. These are described below.

### should_run

This tells Pando whether a filter should run on a specific entry or not. Typically, we will use this function to check for the presence of tags and return a value based on these tags. `should_run` sees only the entry's key and tags: its value is `NULL` (`access->value`), since Pando does not fetch an entry's value until some filter is going to run on it. Use the value in the run function instead.

### run function

The function that actually runs on an entry. This should perform a small amount of functionality on a single entry. Other entries can be queried by `key`, entry values can be updated, and tags can be added and removed. New entries can also be created here.

## Pando Info

Pando is built to run as a distributed system for processing many blockchains all at once. There is no standard format for data, but instead, the user defines how data should be processed via filters.

## Key Terms

- Entry -- A basic data unit in Pando is a entry. These store some piece of either raw or processed data, and have the ability to be further processed. Some examples of what an entry could store are: a single block, a single transaction, or an exchange rate of Bitcoin for a given date.
- Tag -- To manage when a filter should run and when it is waiting on other data, we allow each entry to have a list of tags. These tags provide some basic information about an entry, and can be used for a variety of reasons.
One main use of tags is to determine what type of data an entry contains. Some filters may be targeted to run on raw blocks, while others may require a different, specific piece of data. Tags can be used to indicate what type of data is stored in the given entry. Furthermore, tags can be added to an entry when it depends on another entry's data that does not exist. For example, a filter may need to query data that existed in a previous transaction. However, if that transaction has not been parsed or ingested yet, the filter may need to wait and try again sometime in the future. For this reason, filters run continuously, only stopping once the internal database has not changed when any filter ran on any entry.
- Key -- Each entry is indexed by a key to support rapid querying of data. The keys are defined by the user, and usually represent a concatenation of a few distinguising pieces of information about an entry. For example, some keys may be the combination of a transaction ID and the blockchain it represents, allowing for rapid querying of the entry by this unique information. The keys determine where the entries are stored within the distributed mesh of Pando nodes. Distributing the data allows for processing to be easily run in parallel across multiple machines.
- Filter -- The basic parsing structure of Pando is called a filter. Filters run on entries and are designed to perform a small unit of computation. We require filters to have the ability to run out of order or when data it depends on potentially does not exist. This requirement allows Pando to run continuously as new data is ingested and other filters are run which change the internal database.

