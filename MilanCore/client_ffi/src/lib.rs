#![allow(dead_code)]
#![allow(clippy::unused_unit)]
#![allow(clippy::missing_safety_doc)]

//! Static library exposing Milan ECSM via foreign function interface.
//! The state machine itself is implemented in `client_core`.

use std::ffi::{c_char, c_float, c_int, c_uint, c_ulonglong, c_void, CStr};


/// Greets a user by their `name`.
#[no_mangle]
pub unsafe extern "C" fn say_hello(name: *const c_char) {
    let name = unsafe { CStr::from_ptr(name) };
    if let Ok(name) = name.to_str() {
        println!("Hello there, {}!", name);
    }
}


/// Initializes the global allocator.
#[no_mangle]
pub unsafe extern "C" fn init_allocator(
    _alloc: extern "C" fn(size: c_ulonglong, align: c_ulonglong) -> *mut c_void,
    _realloc: extern "C" fn(ptr: *mut c_void, size: c_ulonglong, align: c_ulonglong) -> *mut c_void,
    _free: extern "C" fn(ptr: *mut c_void) -> (),
) -> () {
    // ...
}


/// Initializes the global logger.
#[no_mangle]
pub unsafe extern "C" fn init_logger(
    _cb: extern "C" fn(file: *const c_char, line: c_uint, sev: c_int, msg: *const c_char) -> (),
) -> () {
    // ...
}


/// Initializes the global state machine instance.
#[no_mangle]
pub unsafe extern "C" fn init_state() {
    // ...
}


/// Uninitializes the global state machine instance.
#[no_mangle]
pub unsafe extern "C" fn uninit_state() {
    // ...
}


/// Ticks the the global state machine instance.
/// Expected to be called exactly once per `UWorld::Tick`.
#[no_mangle]
pub unsafe extern "C" fn world_tick(_delta: c_float) {
    // ...
}
