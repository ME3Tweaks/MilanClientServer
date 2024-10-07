fn main() {
    // Try to fix debug CRT linkage. (spoiler: it doesn't do a thing)
    if cfg!(debug_assertions) {
        println!("cargo::rustc-link-arg=/nodefaultlib:msvcrt");
        println!("cargo::rustc-link-arg=/defaultlib:msvcrtd");
    }
}
