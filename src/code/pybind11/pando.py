import time

from glob import glob
from pando_api import *
from multiprocessing import Pool

def _query_target(addr, tags):
    c_t = ParDBClient(addr)
    return c_t.query(tags)

def query_all(client, tags):
    if not isinstance(tags, list):
        tags = [tags]

    neighbors = client.neighbors()
    with Pool(32) as pool:
        entry_lists = pool.starmap(_query_target, zip(neighbors, tags*len(neighbors)))

    entries = []

    for entry_list in entry_lists:
        entries.extend(entry_list)

    return entries

def _is_processing_target(addr):
    c_t = ParDBClient(addr)
    return c_t.processing()

def process_blocking(client):
    client.process()
    time.sleep(1)

    # Wait until the one client is done processing. Then, we can be
    # semi-confident the rest will be at least close to done
    while client.processing():
        time.sleep(5)

    neighbor_addrs = client.neighbors()
    while True:
        with Pool(32) as pool:
            proc_list = pool.map(_is_processing_target, neighbor_addrs)

        if not any(proc_list):
            break
        else:
            time.sleep(15)

def _db_size_target(addr):
    c_t = ParDBClient(addr)
    return c_t.db_size()

def db_size_total(client):
    neighbor_addrs = client.neighbors()
    with Pool(32) as pool:
        sizes = pool.map(_db_size_target, neighbor_addrs)

    return sum(sizes)

def import_db_blocking(client, path):
    fns = glob(f"{path}/*")
    size_t_size = 8

    num_entries = 0
    for fn in fns:
        with open(fn, 'rb') as f:
            num_entries_bytes = f.read(size_t_size)
            num_entries_in_file = int.from_bytes(num_entries_bytes, 'little')
            num_entries += num_entries_in_file

    initial_db_size = db_size_total(client)

    print("Initial_db_size:", initial_db_size)
    print("num_entries:", num_entries)

    expected_db_size = initial_db_size + num_entries
    print("expected:", expected_db_size)

    client.import_db(path)

    time.sleep(2)


    while db_size_total(client) != expected_db_size:
        time.sleep(10)
