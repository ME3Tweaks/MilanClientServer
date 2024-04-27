#![allow(dead_code)]

//! Static library exposing Milan ECSM via foreign function interface.
//! The state machine itself is implemented in `client_core`.

use std::ffi::{c_char, CStr};


/// Greets a user by their `name`.
#[no_mangle]
pub extern "C" fn say_hello(name: *const c_char) {
    let name = unsafe { CStr::from_ptr(name) };
    if let Ok(name) = name.to_str() {
        println!("Hello there, {}!", name);
    }
}
