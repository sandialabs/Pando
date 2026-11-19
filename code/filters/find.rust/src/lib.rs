mod pando_filter_interface;
use crate::pando_filter_interface::filter_type_SINGLE_ENTRY;
use crate::pando_filter_interface::DBAccess;
use crate::pando_filter_interface::FilterInterface;

use crate::pando_filter_interface::dbkey_t;

use rand::random;

use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;
use std::os::raw::c_void;
use std::ptr;

use libc;

#[no_mangle]
pub static filter_name_str: &[u8; 5] = b"find\0";
const FILTER_DONE_TAG: &str = "find:done";
const FILTER_FAIL_TAG: &str = "find:fail";

const SEARCH_KEY_DOMAIN: u64 = 99;
const SEARCH_KEY: dbkey_t = dbkey_t {
    a: SEARCH_KEY_DOMAIN,
    b: 0,
    c: 1,
};

#[no_mangle]
pub static filter: FilterInterface = {
    FilterInterface {
        filter_name: filter_name_str.as_ptr() as *const c_char,
        filter_type: filter_type_SINGLE_ENTRY,
        should_run: Some(should_run),
        init: None,
        destroy: None,
        run: Some(run),
    }
};

#[no_mangle]
pub extern "C" fn should_run(acc: *const DBAccess) -> i32 {
    if acc.is_null() {
        return 0;
    }
    let data: &DBAccess = unsafe { &*(acc) };

    // Do not run on anything in the search key domain
    if data.key.a == SEARCH_KEY_DOMAIN {
        return 0;
    }

    // Check if we have already run
    let mut tags = data.tags;

    while unsafe { *(*(tags)) } != 0 {
        // Check the C string for this tag
        let tag_cstr = unsafe { CStr::from_ptr(*tags) };
        let tag = String::from_utf8_lossy(tag_cstr.to_bytes()).to_string();

        if tag == FILTER_DONE_TAG || tag == FILTER_FAIL_TAG {
            // Do not run again
            return 0;
        }

        // Increment the tags pointer
        tags = unsafe { tags.offset(1) };
    }

    // Otherwise, we should run
    return 1;
}

/// Look up the values for many keys with one blocking request. The keys are
/// sent to all of the agents that own them at once (see
/// ParDB::get_entries_by_keys), instead of one request per key as with
/// get_entry_by_key.
///
/// Returns one result per key, in the same order (keys may repeat):
/// `Some(bytes)` with the value, cut at its first NUL byte (an empty value
/// gives `Some` of an empty vector), or `None` if the key does not exist.
/// An empty `keys` returns an empty vector without calling into the DB.
#[allow(dead_code)]
pub fn get_entries_by_keys(data: &DBAccess, keys: &[dbkey_t]) -> Vec<Option<Vec<u8>>> {
    if keys.is_empty() {
        return Vec::new();
    }

    // One slot per key; the DB sets each to a malloc'd copy or leaves NULL
    let mut values: Vec<*mut c_char> = vec![ptr::null_mut(); keys.len()];
    unsafe {
        data.get_entries_by_keys.run.expect("get_entries_by_keys is not set")(
            ptr::addr_of!(data.get_entries_by_keys),
            keys.len(),
            keys.as_ptr(),
            values.as_mut_ptr(),
        );
    };

    values
        .into_iter()
        .map(|v| {
            if v.is_null() {
                None
            } else {
                let bytes = unsafe { CStr::from_ptr(v) }.to_bytes().to_vec();
                unsafe {
                    libc::free(v as *mut c_void);
                };
                Some(bytes)
            }
        })
        .collect()
}

fn get_entry_by_key(data: &mut DBAccess, key: dbkey_t) -> Option<String> {
    // Grab the search query string
    let mut query_cstr_ptr: *const c_char = ptr::null();
    unsafe {
        data.get_entry_by_key.run.expect("Err")(
            ptr::addr_of!(data.get_entry_by_key),
            key,
            ptr::addr_of_mut!(query_cstr_ptr),
        );
    };
    if query_cstr_ptr.is_null() {
        println!("find ERROR unable to find query database entry");
        // Add the failure tag
        let fail_tag = CString::new(FILTER_FAIL_TAG).expect("Unable to make string");
        unsafe {
            data.add_tag.run.expect("Error")(ptr::addr_of!(data.add_tag), fail_tag.as_ptr());
        };
        return None;
    }
    let query_cstr = unsafe { CStr::from_ptr(query_cstr_ptr) };
    let query = String::from_utf8_lossy(query_cstr.to_bytes()).to_string();

    unsafe {
        libc::free(query_cstr_ptr as *mut _);
    }

    Some(query)
}

#[no_mangle]
pub extern "C" fn run(acc: *mut c_void) {
    if acc.is_null() {
        return;
    }
    let data: &mut DBAccess = unsafe { &mut *(acc as *mut DBAccess) };

    let query_res = get_entry_by_key(data, SEARCH_KEY);
    if query_res.is_none() {
        return;
    }
    let query = query_res.unwrap();

    // Now, search for the query string in the value
    let val_cstr = unsafe { CStr::from_ptr(data.value) };
    let val = String::from_utf8_lossy(val_cstr.to_bytes()).to_string();

    if val.contains(&query) {
        // This is a match
        // We want to output a new entry with the suitable tags, values, and key
        let tag_end = CString::new("").expect("Unable to build empty string");
        let tag_end_ptr: *const c_char = tag_end.as_ptr();
        let res_val_tags: *const *const c_char = &tag_end_ptr;
        let res_val_str = CString::new(format!("{}:{}:{}", data.key.a, data.key.b, data.key.c))
            .expect("Unable to build res tag string").into_raw();
        let res_val_key: dbkey_t = {
            dbkey_t {
                a: SEARCH_KEY_DOMAIN,
                b: random::<i64>(),
                c: random::<i64>(),
            }
        };

        // Now, save the new entry
        unsafe {
            data.make_new_entry.run.expect("Error")(
                ptr::addr_of!(data.make_new_entry),
                res_val_tags,
                res_val_str,
                res_val_key,
            )
        }

        // Re-claim and free the CString
        unsafe {
            let _ = CString::from_raw(res_val_str);
        }
    }

    // Add the done
    let done_tag = CString::new(FILTER_DONE_TAG).expect("Unable to make string");
    unsafe {
        data.add_tag.run.expect("Error")(ptr::addr_of!(data.add_tag), done_tag.as_ptr());
    };
}
