# OpenSIPs Module Rust Bindings

This project was tested using OpenSIPs 2.4.10. Use branch
merging in order to use the bindings/script in other versions.

## Setup

1. Put opensips executable on `$PWD/bin/opensips`
2. Run `./bindgen.sh` in order to generate C bindings to Rust
3. Use the project `opensips-mod-sys` in your projects.

## Sample

See the sample project on the root directory (Cargo.toml and src/lib.rs).