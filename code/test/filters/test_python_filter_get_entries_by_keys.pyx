#!python
#cython: language_level=3

from filter_lib cimport *
cimport Pando

cdef done = "DONE"
cdef fail = "FAILURE"

# Looks up, in one batched call: {2,2,2} = "test2" (as a dbkey_t), {3,3,3} =
# "test3" (as a tuple), {9,9,9} which does not exist, {2,2,2} again, and
# {4,4,4} whose value is empty. Tags its entry SUCCESS if every result is
# right and an empty batch gives [], else FAILURE.
cdef public void run_internal(const DBAccess* access):
    keys = [dbkey_t(2,2,2), (3,3,3), dbkey_t(9,9,9), dbkey_t(2,2,2), (4,4,4)]
    values = Pando.get_entries_by_keys(access, keys)
    expected = [b"test2", b"test3", None, b"test2", b""]
    if values == expected and Pando.get_entries_by_keys(access, []) == []:
        Pando.add_tag(access, "SUCCESS")
    else:
        print("get_entries_by_keys returned", values, "expected", expected, flush=True)
        Pando.add_tag(access, fail)
    Pando.add_tag(access, done)


cdef public bint should_run_internal(const DBAccess* access):
    tags = Pando.get_tags(access)
    if done in tags or fail in tags:
        return False
    if "A" in tags:
        return True
    return False
