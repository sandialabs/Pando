#!python
#cython: language_level=3

import csv
from io import StringIO
import sys

from cpython.bytes cimport PyBytes_FromString

from filter_lib cimport *
cimport Pando


cdef filter_done = "python_ETH_roar_bitmap:done"
cdef filter_fail = "python_ETH_roar_bitmap:fail"
cdef unsigned int ETH_ROARING_BITMAP_KEY = 18
cdef unsigned int ETH_DAG_KEY = 41



cdef inline str cstr_to_str(const char* s):
    if s == NULL:
        return ""
    return PyBytes_FromString(s).decode("utf-8")


cdef inline unsigned int roaring_key(unsigned int x):
    return x >> 16


cdef inline unsigned int roaring_value(unsigned int x):
    return x & 0xFFFF


cdef inline unsigned int addr_low32(str addr):
    if addr.startswith("0x"):
        addr = addr[2:]
    return <unsigned int>(int(addr, 16) & 0xFFFFFFFF)


cdef inline unsigned int addr_b_key_from_low32(unsigned int low32):
    return low32 >> 16


cdef inline unsigned int addr_c_key_from_low32(unsigned int low32):
    return low32 & 0xFFFF


cdef public void run_internal(const DBAccess* access):
    cdef list new_tags
    cdef str row_text
    cdef list fields

    cdef unsigned int block_num
    cdef unsigned int rb_key
    cdef unsigned int rb_value

    cdef str from_address
    cdef unsigned int from_low32
    cdef unsigned int from_b_key
    cdef unsigned int from_c_key

    cdef object new_key
    cdef str new_value

    cdef object filter_done_tag
    cdef object filter_fail_tag

    try:
        row_text = cstr_to_str(access.value)
        fields = next(csv.reader(StringIO(row_text)))

        block_num = <unsigned int>int(fields[3])
        from_address = fields[5].lower()

        from_low32 = addr_low32(from_address)
        from_b_key = addr_b_key_from_low32(from_low32)
        from_c_key = addr_c_key_from_low32(from_low32)

        rb_key = roaring_key(block_num)
        rb_value = roaring_value(block_num)

        new_tags = ["ETH", "BITMAP", "MERGE_STRATEGY=KV"]

        new_key = dbkey_t(ETH_ROARING_BITMAP_KEY, from_b_key, from_c_key)
        new_value = from_address + "|" + str(rb_key) + ":" + str(rb_value)

        Pando.make_new_entry_lt_10_tags(access, new_tags, new_value, new_key)

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
