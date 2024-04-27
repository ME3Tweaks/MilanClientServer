use std::env;

fn main() {
    let root = env::var("CARGO_MANIFEST_DIR").unwrap();

    // This is a primitive configuration for setup validation only.
    // It drastically needs to be reworked for the real client.

    // Embrace the cbindgen here: https://github.com/mozilla/cbindgen/blob/master/docs.md.

    cbindgen::Builder::new()
        .with_crate(root)
        .generate()
        .expect("failed to generate ffi bindings")
        .write_to_file("../milan_ecsm_ffi.h");
}
