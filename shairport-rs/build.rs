fn main() {
    let alac_dir = std::path::Path::new("..");
    println!(
        "cargo:rerun-if-changed={}",
        alac_dir.join("alac.c").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        alac_dir.join("alac.h").display()
    );

    cc::Build::new()
        .file(alac_dir.join("alac.c"))
        .include(alac_dir)
        .compile("alac");
}
