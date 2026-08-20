// Links the C++ side of WireLab into the Rust frontend.
//
// CMake is the master build: it compiles wirelab_ffi and its dependencies and
// hands their directories over in WIRELAB_LIB_DIRS. Building the crate on its
// own is supported for frontend iteration -- set WIRELAB_LIB_DIRS yourself --
// but there is deliberately no fallback that guesses a build directory, because
// a guess that finds a stale archive is worse than a clear failure.

use std::env;
use std::path::Path;

fn main() {
    println!("cargo:rerun-if-env-changed=WIRELAB_LIB_DIRS");
    println!("cargo:rerun-if-env-changed=WIRELAB_LINK_METAL");
    println!("cargo:rerun-if-env-changed=WIRELAB_LINK_CUDA");
    println!("cargo:rerun-if-changed=../include/wirelab/wirelab_ffi.h");

    let Ok(lib_dirs) = env::var("WIRELAB_LIB_DIRS") else {
        panic!(
            "WIRELAB_LIB_DIRS is not set.\n\
             Build through CMake:\n\
             \x20 cmake -S . -B build -DPROJECT_BUILD_DESKTOP=ON\n\
             \x20 cmake --build build --target wirelab-desktop\n\
             or set WIRELAB_LIB_DIRS to a ';'-separated list of directories holding\n\
             libwirelab_ffi.a, libwirelab_session.a, libwirelab_backends.a and libWireLab.a."
        );
    };

    for dir in lib_dirs.split(';').filter(|dir| !dir.is_empty()) {
        if !Path::new(dir).is_dir() {
            panic!("WIRELAB_LIB_DIRS names a directory that does not exist: {dir}");
        }
        println!("cargo:rustc-link-search=native={dir}");
    }

    // Link order is dependency order: the shim, then the orchestration layer,
    // then backend selection, then the core. A static archive only resolves
    // symbols for archives listed after it.
    println!("cargo:rustc-link-lib=static=wirelab_ffi");
    println!("cargo:rustc-link-lib=static=wirelab_session");
    println!("cargo:rustc-link-lib=static=wirelab_backends");
    println!("cargo:rustc-link-lib=static=WireLab");

    if env::var("WIRELAB_LINK_METAL").as_deref() == Ok("1") {
        println!("cargo:rustc-link-lib=static=wirelab_metal");
        println!("cargo:rustc-link-lib=framework=Metal");
        println!("cargo:rustc-link-lib=framework=Foundation");
    }
    if env::var("WIRELAB_LINK_CUDA").as_deref() == Ok("1") {
        println!("cargo:rustc-link-lib=static=wirelab_cuda");
        println!("cargo:rustc-link-lib=cudart");
    }

    // The archives are C++; rustc does not add a standard library for them.
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=c++");
    } else if cfg!(target_os = "linux") {
        println!("cargo:rustc-link-lib=stdc++");
    }
}
