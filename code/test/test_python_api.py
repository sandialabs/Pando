#!/usr/bin/env python3

import json
from glob import glob
import os
import pickle
import sys
import time
import shutil
from subprocess import Popen
from threading import Thread

import pathlib
tests_bin_dir = pathlib.Path(__file__).parent.resolve()

sys.path.append(os.path.join(tests_bin_dir, '../pybind11/'))
import pando

data_dir = os.path.join(tests_bin_dir, '../../test/data')
filter_dir = os.path.join(tests_bin_dir, '../filters/')
btc_path = os.path.join(data_dir, 'simple_bitcoin.txt')
eth_path = os.path.join(data_dir, 'simple_ethereum.txt')

class Test():
    def setup_method(self):
        self.db1_addr = "127.0.0.1,0"
        self.db2_addr = "127.0.0.1,3"
        self.pardb_proc1 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), self.db1_addr, self.db1_addr])
        self.pardb_proc2 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), self.db2_addr, self.db1_addr])
        c1 = pando.ParDBClient(self.db1_addr)
        c2 = pando.ParDBClient(self.db2_addr)
        while len(c1.neighbors()) < 2:
            time.sleep(0.05)
        while len(c2.neighbors()) < 2:
            time.sleep(0.05)

    def teardown_method(self):
        self.pardb_proc1.kill()
        self.pardb_proc2.kill()
        time.sleep(0.2)

    def test_entry_init(self):
        e = pando.DBEntry()
        assert e.value() == ""

    def test_entry_add_to_value(self):
        e = pando.DBEntry()
        e.add_to_value("foo")
        assert e.value() == "foo\n"

    def test_entry_tags(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        assert client1.db_size() + client2.db_size() == 0
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 2
        for entry in client1.get_entries():
            assert entry.tags() == set(["BTC", "block"])
        for entry in client2.get_entries():
            assert entry.tags() == set(["BTC", "block"])

    def test_entry_get_key(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client1.add_db_file(btc_path)
        client2 = pando.ParDBClient(self.db2_addr)
        time.sleep(0.2)

        assert client1.db_size() + client2.db_size() == 2
        for entry in client1.get_entries():
            key = entry.get_key()
            assert isinstance(key, pando.dbkey_t)
        for entry in client2.get_entries():
            key = entry.get_key()
            assert isinstance(key, pando.dbkey_t)


    def test_entry_has_tag(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client1.add_db_file(btc_path)
        client2 = pando.ParDBClient(self.db2_addr)
        time.sleep(0.2)

        assert client1.db_size() + client2.db_size() == 2

        i = 0
        for entry in client1.get_entries():
            assert entry.has_tag("BTC") == True
            assert entry.has_tag("block") == True
            i += 1
        for entry in client2.get_entries():
            assert entry.has_tag("BTC") == True
            assert entry.has_tag("block") == True
            i += 1
        assert i == 2

    def test_dbkey_a_b_c(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client1.add_db_file(btc_path)
        client2 = pando.ParDBClient(self.db2_addr)
        time.sleep(0.2)

        assert client1.db_size() + client2.db_size() == 2

        for entry in client1.get_entries():
            key = entry.get_key()
            assert hasattr(key, "a") == True
            assert hasattr(key, "b") == True
            assert hasattr(key, "c") == True
        for entry in client2.get_entries():
            key = entry.get_key()
            assert hasattr(key, "a") == True
            assert hasattr(key, "b") == True
            assert hasattr(key, "c") == True

    def test_db_add_filter_dir(self):
        client = pando.ParDBClient(self.db1_addr)
        client.add_filter_dir(filter_dir)

    def test_db_install_filter(self):
        client = pando.ParDBClient(self.db1_addr)
        client.add_filter_dir(filter_dir)
        client.install_filter('BTC_block_to_tx')

    def test_db_process(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        client1.add_filter_dir(filter_dir)
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        client1.install_filter('BTC_block_to_tx')
        client1.process()
        time.sleep(0.5)
        assert client1.db_size() + client2.db_size() == 7

    def test_db_process_blocking(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client1.add_filter_dir(filter_dir)
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        client1.install_filter('BTC_block_to_tx')
        pando.process_blocking(client1)
        assert pando.db_size_total(client1) == 7

    def test_db_add_db_file(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 2

    def test_db_size(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 2
        client1.add_db_file(eth_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 4

    def test_db_entries(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 2
        i = 0
        for entry in client1.get_entries():
            json.loads(entry.value())
            i += 1
        for entry in client2.get_entries():
            json.loads(entry.value())
            i += 1
        assert i == 2

    def test_neighbors(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)

        assert len(client1.neighbors()) == 2

    def test_processing(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)

        for _ in range(10000):
            client1.add_db_file(btc_path)

        # Make sure it starts out not processing
        assert client1.processing() == False
        assert client2.processing() == False

        # Start a new thread that kicks off before processing occurs. Make sure
        # at some point, it says that processing is happening
        def _target():
            while True:
                if client1.processing():
                    break
                else:
                    time.sleep(0.1)
        t = Thread(target=_target)
        t.start()

        client1.process()

        t.join()

    def test_pickle_entry_value(self):
        e = pando.DBEntry()
        e.set_value("my value")

        pickled = pickle.dumps(e)
        unpickled = pickle.loads(pickled)

        assert unpickled.value() == "my value"

    def test_pickle_entry(self):
        # Load a couple pardbs with data
        client1 = pando.ParDBClient(self.db1_addr)
        client1.add_db_file(btc_path)
        client2 = pando.ParDBClient(self.db2_addr)
        time.sleep(0.2)

        assert client1.db_size() + client2.db_size() == 2
        entries = []
        entries.extend(client1.get_entries())
        entries.extend(client2.get_entries())

        entries = [entries[0]]

        for entry in entries:
            # Make sure this doesn't fail
            pickle_s = pickle.dumps(entry)
            unpickled = pickle.loads(pickle_s)

            assert sorted(unpickled.tags()) == sorted(entry.tags())
            assert unpickled.get_key() == entry.get_key()
            assert unpickled.value() == entry.value()

    def test_par_query(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        assert client1.db_size() + client2.db_size() == 0

        expected_size = 2
        client1.add_db_file(btc_path)

        while True:
            time.sleep(0.2)
            if client1.db_size() == 0 or client2.db_size() == 0:
                client1.add_db_file(btc_path)
                expected_size += 2
                time.sleep(0.2)
            else:
                break
        assert client1.db_size() + client2.db_size() == expected_size
        assert client1.db_size() > 0
        assert client2.db_size() > 0

        entries = pando.query_all(client1, "BTC")
        assert len(entries) == expected_size

    def test_clear_and_installed_filters(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        assert client1.db_size() + client2.db_size() == 0

        client1.add_filter_dir(filter_dir)
        client1.install_filter('BTC_block_to_tx')

        time.sleep(0.2)

        assert len(client1.installed_filters()) == 1
        assert len(client2.installed_filters()) == 1

        client1.clear_filters()

        time.sleep(0.2)

        assert len(client1.installed_filters()) == 0
        assert len(client2.installed_filters()) == 0

    def test_import_export(self):
        d = "export"

        # Make sure the directory didn't exist beforehand
        shutil.rmtree(d, ignore_errors=True)

        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        assert client1.db_size() + client2.db_size() == 0
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 2
        os.mkdir(d)
        client1.export_db(d)
        time.sleep(0.5)

        fns = glob(f"{d}/*")
        assert len(fns) == 2

        self.pardb_proc1.kill()
        self.pardb_proc2.kill()

        # Now create a new mesh and test importing there
        db3_addr = "127.0.0.1,7"
        db4_addr = "127.0.0.1,10"
        pardb_proc3 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), db3_addr, db3_addr])
        pardb_proc4 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), db4_addr, db3_addr])
        
        client3 = pando.ParDBClient(db3_addr)
        client4 = pando.ParDBClient(db4_addr)

        while len(client3.neighbors()) < 2:
            time.sleep(0.05)
        while len(client4.neighbors()) < 2:
            time.sleep(0.05)

        assert len(client3.neighbors()) == 2
        assert len(client4.neighbors()) == 2
        assert client3.db_size() + client4.db_size() == 0

        client3.import_db(d)
        time.sleep(0.5)

        assert client3.db_size() + client4.db_size() == 2

        # Kill the pardbs
        pardb_proc3.kill()
        pardb_proc4.kill()

        # Start another two pardbs to test the blocking import
        db5_addr = "127.0.0.1,14"
        db6_addr = "127.0.0.1,18"
        pardb_proc5 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), db5_addr, db5_addr])
        pardb_proc6 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), db6_addr, db5_addr])
        client5 = pando.ParDBClient(db5_addr)
        client6 = pando.ParDBClient(db6_addr)

        while len(client5.neighbors()) < 2:
            time.sleep(0.05)
        while len(client6.neighbors()) < 2:
            time.sleep(0.05)
        assert len(client5.neighbors()) == 2
        assert len(client6.neighbors()) == 2
        assert client5.db_size() + client6.db_size() == 0

        # Now, test the blocking import
        pando.import_db_blocking(client5, d)
        assert pando.db_size_total(client5) == 2

        # Clean up the exported directory
        shutil.rmtree(d)

        # Kill the pardbs
        pardb_proc5.kill()
        pardb_proc6.kill()

    def test_import_export_with_tag(self):
        d = "export_with_tag"

        # Make sure the directory didn't exist beforehand
        shutil.rmtree(d, ignore_errors=True)

        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        assert client1.db_size() + client2.db_size() == 0
        client1.add_db_file(btc_path)
        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 2
        os.mkdir(d)
        client1.export_db_with_tag(d, 'block')
        time.sleep(0.5)

        fns = glob(f"{d}/*")
        assert len(fns) == 2

        self.pardb_proc1.kill()
        self.pardb_proc2.kill()

        # Now create a new mesh and test importing there
        db3_addr = "127.0.0.1,7"
        db4_addr = "127.0.0.1,10"
        pardb_proc3 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), db3_addr, db3_addr])
        pardb_proc4 = Popen([os.path.join(tests_bin_dir, "../src/pando_pardb"), db4_addr, db3_addr])
        
        client3 = pando.ParDBClient(db3_addr)
        client4 = pando.ParDBClient(db4_addr)

        while len(client3.neighbors()) < 2:
            time.sleep(0.05)
        while len(client4.neighbors()) < 2:
            time.sleep(0.05)

        assert len(client3.neighbors()) == 2
        assert len(client4.neighbors()) == 2
        assert client3.db_size() + client4.db_size() == 0

        client3.import_db(d)
        time.sleep(0.5)

        assert client3.db_size() + client4.db_size() == 2

        # Kill the pardbs
        pardb_proc3.kill()
        pardb_proc4.kill()


        # Clean up the exported directory
        shutil.rmtree(d)

    def test_add_entry(self):
        client1 = pando.ParDBClient(self.db1_addr)
        client2 = pando.ParDBClient(self.db2_addr)
        assert client1.db_size() + client2.db_size() == 0

        k = pando.dbkey_t()
        k.a = 1
        k.b = 1
        k.c = 1
        e = pando.DBEntry()
        e.set_key(k)
        e.add_tag("TEST1")
        e.add_tag("TEST2")
        e.set_value("VAL")

        client1.add_entry(e)

        time.sleep(0.2)
        assert client1.db_size() + client2.db_size() == 1
