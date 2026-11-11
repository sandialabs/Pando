#!python
#cython: language_level=3

import csv
from io import StringIO

from cpython.bytes cimport PyBytes_FromString

from filter_lib cimport *
cimport Pando

cdef filter_done = "python_ETH_dagify:done"
cdef filter_fail = "python_ETH_dagify:fail"
cdef unsigned int ETH_ROARING_BITMAP_KEY = 18
cdef unsigned int ETH_DAG_KEY = 41

cdef inline str cstr_to_str(const char* s):
    if s == NULL:
        return ""
    return PyBytes_FromString(s).decode("utf-8")


cdef str py_value_to_str(object v):
    if callable(v):
        v = v()

    if isinstance(v, bytes):
        return v.decode("utf-8")

    return str(v)


cdef inline unsigned int addr_low32(str addr):
    if addr.startswith("0x"):
        addr = addr[2:]

    return <unsigned int>(int(addr, 16) & 0xFFFFFFFF)


cdef inline unsigned int addr_b_key_from_low32(unsigned int low32):
    return low32 >> 16


cdef inline unsigned int addr_c_key_from_low32(unsigned int low32):
    return low32 & 0xFFFF


cdef inline unsigned int addr_lower_16(str addr):
    return addr_low32(addr) & 0xFFFF


cdef str csv_fields_to_row(list fields):
    cdef object sio = StringIO()
    cdef object writer = csv.writer(sio, lineterminator="")
    writer.writerow(fields)
    return sio.getvalue()


cdef list bitmap_values_for_wallet_key(const DBAccess* access, unsigned int b_key, unsigned int c_key):
    cdef object bitmap_key
    cdef object bitmap_value
    cdef list values = []

    bitmap_key = dbkey_t(ETH_ROARING_BITMAP_KEY, b_key, c_key)

    try:
        bitmap_value = Pando.get_entry_by_key(access, bitmap_key)
    except Exception:
        return values

    if bitmap_value is None:
        return values

    values.append(py_value_to_str(bitmap_value))

    return values


cdef long find_next_block_in_bitmap(list bitmap_values, str wallet_addr, unsigned int current_block):
    cdef str target
    cdef object entry_value
    cdef str raw_line
    cdef str line
    cdef Py_ssize_t colon_pos
    cdef Py_ssize_t pipe_pos

    cdef str kv_key
    cdef str values_str
    cdef str bitmap_wallet
    cdef str rb_key_str
    cdef str token

    cdef unsigned int rb_key
    cdef unsigned int rb_value
    cdef unsigned int candidate_block
    cdef long best = -1

    target = wallet_addr.lower()

    for entry_value in bitmap_values:
        for raw_line in str(entry_value).splitlines():
            line = raw_line.strip()

            if line == "":
                continue

            colon_pos = line.find(":")
            if colon_pos < 0:
                continue

            kv_key = line[:colon_pos]
            values_str = line[colon_pos + 1:]

            pipe_pos = kv_key.rfind("|")
            if pipe_pos < 0:
                continue

            bitmap_wallet = kv_key[:pipe_pos].lower()
            rb_key_str = kv_key[pipe_pos + 1:]

            if bitmap_wallet != target:
                continue

            rb_key = <unsigned int>int(rb_key_str)

            for token in values_str.split(","):
                token = token.strip()

                if token == "":
                    continue

                rb_value = <unsigned int>int(token)

                candidate_block = (rb_key << 16) | rb_value

                if candidate_block > current_block:
                    if best < 0 or candidate_block < best:
                        best = candidate_block

    return best


cdef public void run_internal(const DBAccess* access):
    cdef list new_tags
    cdef str row_text
    cdef str new_row_text
    cdef list fields

    cdef unsigned int current_block

    cdef str from_address
    cdef str to_address

    cdef unsigned int to_low32
    cdef unsigned int to_b_key
    cdef unsigned int to_c_key

    cdef unsigned int from_lower_16
    cdef unsigned int to_lower_16

    cdef object dag_key
    cdef list bitmap_values
    cdef long next_block

    cdef object filter_done_tag
    cdef object filter_fail_tag

    try:
        row_text = cstr_to_str(access.value)
        fields = next(csv.reader(StringIO(row_text)))

        current_block = <unsigned int>int(fields[3])
        from_address = fields[5].lower()
        to_address = fields[6].lower()

        from_lower_16 = addr_lower_16(from_address)

        fields[5] = from_address + "_" + str(current_block)

        new_tags = ["ETH", "dag-edge"]

        if to_address == "" or to_address == "0x":
            fields[6] = to_address + "_utxo"
            dag_key = dbkey_t(ETH_DAG_KEY, from_lower_16, 0)

            new_row_text = csv_fields_to_row(fields)
            Pando.make_new_entry_lt_10_tags(access, new_tags, new_row_text, dag_key)

            filter_done_tag = Pando.make_tag(filter_done)
            Pando.add_tag(access, filter_done_tag)
            return

        to_low32 = addr_low32(to_address)
        to_b_key = addr_b_key_from_low32(to_low32)
        to_c_key = addr_c_key_from_low32(to_low32)

        bitmap_values = bitmap_values_for_wallet_key(access, to_b_key, to_c_key)
        next_block = find_next_block_in_bitmap(bitmap_values, to_address, current_block)

        if next_block >= 0:
            to_lower_16 = addr_lower_16(to_address)

            fields[6] = to_address + "_" + str(next_block)
            dag_key = dbkey_t(ETH_DAG_KEY, from_lower_16, to_lower_16)
        else:
            fields[6] = to_address + "_utxo"
            dag_key = dbkey_t(ETH_DAG_KEY, from_lower_16, 0)

        new_row_text = csv_fields_to_row(fields)
        Pando.make_new_entry_lt_10_tags(access, new_tags, new_row_text, dag_key)

        filter_done_tag = Pando.make_tag(filter_done)
        Pando.add_tag(access, filter_done_tag)

    except Exception:
        filter_fail_tag = Pando.make_tag(filter_fail)
        Pando.add_tag(access, filter_fail_tag)


cdef public bint should_run_internal(const DBAccess* access):
    cdef object tags = Pando.get_tags(access)

    if filter_done in tags or filter_fail in tags:
        return False

    if "ETH" in tags and "tx_csv" in tags:
        return True

    return False

