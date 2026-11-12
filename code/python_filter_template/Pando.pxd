#cython: language_level=3

from filter_lib cimport *

from ctypes import c_char_p as chr_pntr
from libc.stdlib cimport malloc, free
from cpython.bytes cimport PyBytes_FromString, PyBytes_AsString
import os
import sys


cdef inline list get_tags(const DBAccess* access):
    """
      Function:
          get_tags

      Description:
          A function that returns a python list of access.tags as strings

      Parameters:
          access (const DBAccess*) : A pointer to the DBAccess object whose entry's tags are to be returned
    
      Calls:
          access.tags

      Modifies:
          None

      Returns:
          list[str]
    """
    tags = access.tags
    decoded_tags = []
    i = 0
    while tags[i] != b"":
        decoded_tag = tags[i].decode('utf-8')
        decoded_tags.append(decoded_tag)
        i += 1
    return decoded_tags


cdef inline list _fill_tags(char** c_tags, list py_tags, int limit):
    """
      Function:
          _fill_tags

      Description:
          Fills a char** object for the purposes of make_new_entry using a python list.
          The input list is copied, not modified. All slots of c_tags are first set to NULL,
          and the tags are terminated with an empty string. If limit or more tags are given,
          only the first limit - 1 are used so the terminator still fits.

          c_tags only holds pointers into the bytes objects in the returned list; nothing is
          copied. The caller must keep the returned list alive for as long as c_tags is in use,
          otherwise those bytes objects are freed and c_tags is left with dangling pointers.

      Parameters:
          c_tags  (char**)    : The array of char* that is limited to the size passed in as limit
          py_tags (list[str]) : A python list of python strings that contain the tags intended to be added to access
          limit   (int)       : An integer limit that is the maximum length of c_tags

      Calls:
          make_tag()

      Modifies:
          c_tags

      Returns:
          list[bytes] : The encoded tags that c_tags points into. Must be kept alive while c_tags is in use.
    """
    cdef int i
    cdef list local_tags
    cdef list new_tags_store

    for i in range(limit):
        c_tags[i] = NULL

    local_tags = list(py_tags)

    if len(local_tags) >= limit:
        print("ERROR: BUFFER OVERFLOW IN MAKE NEW ENTRY. TAG COUNT EXPECTED <", limit, "RECEIVED", len(local_tags), flush=True)
        print("Only applying first", limit - 1, "tags. Use a higher-bounds function if a larger number of tags is needed.")
        local_tags = local_tags[:limit - 1]

    local_tags.append("")

    new_tags_store = [make_tag(tag) for tag in local_tags]

    for i, tag in enumerate(new_tags_store):
        if i >= limit:
            break
        c_tags[i] = <char*>tag

    return new_tags_store


cdef inline void make_new_entry_lt_3_tags(const DBAccess* access, list tags, object value, dbkey_t new_key):
    """
      Function:
          make_new_entry_lt_3_tags

      Description:
          Abstraction of make_new_entry with less than 3 tags.

      Parameters:
          access (const DBAccess*)         : A pointer to the DBAccess object
          tags (list[str | char* | bytes]) : A python list of tags to add in the new entry
          value (str | char* | bytes)      : A string-like object to be added as the value
          new_key (dbkey_t)                : The key to add to the entry

      Calls:
          _fill_tags()
          make_value()
          access.make_new_entry.run()

      Modifies:
          access

      Returns:
          void
    """
    limit = 3
    cdef char* new_tags[3]  # Has to be manually declared, can't use 'limit'
    # Keep the returned list alive: new_tags points into its bytes objects until run() completes.
    new_tags_store = _fill_tags(new_tags, tags, limit)
    new_value = make_value(value)
    access.make_new_entry.run(&access.make_new_entry, new_tags, new_value, <dbkey_t>new_key)

cdef inline void make_new_entry_lt_10_tags(const DBAccess* access, list tags, object value, dbkey_t new_key):
    """
      Function:
          make_new_entry_lt_10_tags

      Description:
          Abstraction of make_new_entry with less than 10 tags.

      Parameters:
          access (const DBAccess*)         : A pointer to the DBAccess object
          tags (list[str | char* | bytes]) : A python list of tags to add in the new entry
          value (str | char* | bytes)      : A string-like object to be added as the value
          new_key (dbkey_t)                : The key to add to the entry

      Calls:
          _fill_tags()
          make_value()
          access.make_new_entry.run()

      Modifies:
          access

      Returns:
          void
    """
    limit = 10
    cdef char* new_tags[10]  # Has to be manually declared, can't use 'limit'
    # Keep the returned list alive: new_tags points into its bytes objects until run() completes.
    new_tags_store = _fill_tags(new_tags, tags, limit)
    new_value = make_value(value)
    access.make_new_entry.run(&access.make_new_entry, new_tags, new_value, <dbkey_t>new_key)


cdef inline void make_new_entry_lt_25_tags(const DBAccess* access, list tags, object value, dbkey_t new_key):
    """
      Function:
          make_new_entry_lt_25_tags

      Description:
          Abstraction of make_new_entry with less than 25 tags.

      Parameters:
          access (const DBAccess*)         : A pointer to the DBAccess object
          tags (list[str | char* | bytes]) : A python list of tags to add in the new entry
          value (str | char* | bytes)      : A string-like object to be added as the value
          new_key (dbkey_t)                : The key to add to the entry

      Calls:
          _fill_tags()
          make_value()
          access.make_new_entry.run()

      Modifies:
          access          

      Returns:
          void
    """
    limit = 25
    cdef char* new_tags[25]  # Has to be manually declared, can't use 'limit'
    # Keep the returned list alive: new_tags points into its bytes objects until run() completes.
    new_tags_store = _fill_tags(new_tags, tags, limit)
    new_value = make_value(value)
    access.make_new_entry.run(&access.make_new_entry, new_tags, new_value, <dbkey_t>new_key)


cdef inline void make_new_entry_lt_50_tags(const DBAccess* access, list tags, object value, dbkey_t new_key):
    """
      Function:
          make_new_entry_lt_50_tags

      Description:
          Abstraction of make_new_entry with less than 50 tags.

      Parameters:
          access (const DBAccess*)         : A pointer to the DBAccess object
          tags (list[str | char* | bytes]) : A python list of tags to add in the new entry
          value (str | char* | bytes)      : A string-like object to be added as the value
          new_key (dbkey_t)                : The key to add to the entry

      Calls:
          _fill_tags()
          make_value()
          access.make_new_entry.run()

      Modifies:
          access

      Returns:
          void
    """
    limit = 50
    cdef char* new_tags[50]  # Has to be manually declared, can't use 'limit'
    # Keep the returned list alive: new_tags points into its bytes objects until run() completes.
    new_tags_store = _fill_tags(new_tags, tags, limit)
    new_value = make_value(value)
    access.make_new_entry.run(&access.make_new_entry, new_tags, new_value, <dbkey_t>new_key)


cdef inline void remove_tag(const DBAccess* access, tag):
    """
      Function:
          remove_tag

      Description:
          Removes a tag from DBAccess.

      Parameters:
          access (const DBAccess*)  : A pointer to the DBAccess object from which the tag will be removed
          tag (str | bytes | char*) : A string-like object that is to be removed from access.tags
      
      Calls:
          access.remove_tag.run()
          make_tag()

      Modifies:
          access

      Returns:
          void
    """    
    access.remove_tag.run(&access.remove_tag, <dbkey_t>access.key, make_tag(tag));


cdef inline void add_tag(const DBAccess* access, tag):
    """
      Function:
          add_tag

      Description:
          Takes a single string-like object and a DBAccess pointer and adds the string-like object as a tag

      Parameters:
          access (const DBAccess*)  : A pointer to the DBAccess object whose entry will receive the new tag 
          tag (str | bytes | char*) : A string-like object to be added as a tag
 
      Calls:
          access.add_tag.run()
          make_tag()

      Modifies:
          access
 
      Returns:
          void
    """
    new_tag = make_tag(tag)
    access.add_tag.run(&access.add_tag, <const char*>new_tag);


cdef inline object get_entry_by_key(const DBAccess* access, dbkey_t search_key):
    """
      Function:
          get_entry_by_key

      Description:
          Finds and returns the value associated with the search_key

      Parameters:
          access (const DBAccess*)  : A pointer to the DBAccess object  
          search_key (dbkey_t)      : The key to the entry intended to be returned
          
      Calls:
          access.get_entry_by_key.run()

      Modifies:
          access

      Returns:
          String
    """
    cdef char* ret = NULL
    access.get_entry_by_key.run(&access.get_entry_by_key, <dbkey_t>search_key, <unsigned long>&ret);
    if not ret:
        return None
    else:
        py_ret = PyBytes_FromString(ret)
        free(ret)
        return py_ret
        

cdef inline bytes make_tag(tag_name):
    """
      Function:
          make_tag

      Description:
          Wrapper function for making a tag compatible for Pando. Converts to bytes and assumes a UTF-8 encoding

      Parameters:
          tag_name (str | bytes | char*) : The string-like object to be converted into a tag-compatible byte-string

      Calls:
          to_bytes()

      Modifies:
          None

      Returns:
          Bytes (UTF-8)
    """
    return to_bytes(tag_name, 'utf-8')


cdef inline bytes make_value(value):
    """
      Function:
          make_value

      Description:
          Wrapper function for making a value for Pando. Converts to bytes and assumes a UTF-8 encoding

      Parameters:
          value (str | bytes | char*) : The string-like object to be converted into a value-compatible byte-string

      Calls:
          to_bytes()

      Modifies:
          None

      Returns:
          Bytes (UTF-8)
    """
    return to_bytes(value, 'utf-8')


cdef inline bint has_tag(const DBAccess* access, tag):
    """
      Function:
          has_tag

      Description:
          Checks if an entry has a certain tag

      Parameters:
          access (const DBAccess*)     :  A pointer to the DBAccess object whose entry will be checked
          tag    (str | bytes | char*) :  A string-like object that is the tag to check

      Calls:
          get_tags()

      Modifies:
          None

      Returns:
          Boolean Integer
    """
    tags = get_tags(access)
    if not isinstance(tag, str):
        tag = tag.decode()
    if tag in tags:
        return True
    return False


cdef inline bytes to_bytes(value, encoding):
    """
      Function:
          to_bytes

      Description:
          Converts a string-like object to bytes with the passed in encoding

      Parameters:
          value (str | bytes | char*) : The value to encode
          encoding (str)              : A string detailing the encoding (e.g. "utf-8")

      Calls:
          None

      Modifies:
          None

      Returns:
          Bytes
    """
    if isinstance(value, bytes):
        return value 
    else:
        return bytes(value, encoding)


cdef inline void subscribe_to_entry(const DBAccess* access, dbkey_t subscriber_key, dbkey_t search_key, tag):
    """
      Function:
          subscribe_to_entry

      Description:
          Abstract of subscribe_to_entry. An entry with "subscriber_key" will have "tag" removed when the entry with "search_key" is created

      Parameters:
          access         (const DBAccess*)     : A pointer to the DBAccess object
          subscriber_key (dbkey_t)             : The key of the entry that is waiting for the entry with "search key" to be created 
          search_key     (dbkey_t)             : The key of the entry that will be created in the future
          tag            (str | bytes | char*) : The tag to be removed from subscriber_key's entry when search_key's entry is created
       
      Calls:
          make_tag()
          access.subscribe_to_entry.run()
 
      Modifies:
          access

      Returns:
          void
    """
    new_tag = make_tag(tag)
    access.subscribe_to_entry.run(&access.subscribe_to_entry, <dbkey_t>subscriber_key, <dbkey_t>search_key, <const char*>new_tag)


cdef inline void update_entry_val(const DBAccess* access, dbkey_t search_key, new_val):
    """
      Function:
          update_entry_val

      Description:
          Abstract of update_entry_val. Updates the value of the entry found by search_key

      Parameters:
          access     (const DBAccess*)     : A pointer to the DBAccess object
          search_key (dbkey_t)             : The key to the entry to update
          new_val    (str | bytes | char*) : The updated value of the entry

       Calls:
          to_bytes()
          access.update_entry_val.run()

      Modifies:
          access

      Returns:
          void
    """
    val = to_bytes(new_val, 'utf-8')
    access.update_entry_val.run(&access.update_entry_val, <dbkey_t>search_key, val)

