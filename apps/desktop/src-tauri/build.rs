use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    // 自动复制 hostd 二进制文件到 binaries 目录
    copy_hostd_binary();

    tauri_build::build();
}

fn copy_hostd_binary() {
    // 获取目标平台三元组
    let target_triple = env::var("TARGET").expect("TARGET not set");

    // 使用 CARGO_MANIFEST_DIR 获取 Cargo.toml 所在目录
    let manifest_dir = env::var("CARGO_MANIFEST_DIR")
        .map(PathBuf::from)
        .expect("CARGO_MANIFEST_DIR not set");

    let binaries_dir = manifest_dir.join("binaries");
    let target_binary_name = if target_triple.contains("windows") {
        format!("hostd-{target_triple}.exe")
    } else {
        format!("hostd-{target_triple}")
    };
    let target_binary_path = binaries_dir.join(&target_binary_name);

    // 如果目标二进制文件已存在，跳过复制
    if target_binary_path.exists() {
        println!(
            "cargo:warning=hostd binary already exists: {}",
            target_binary_path.display()
        );
        return;
    }

    // 优先使用 NSPEAKER_HOSTD_PATH 环境变量（CMake 集成构建时自动设置）
    println!("cargo:rerun-if-env-changed=NSPEAKER_HOSTD_PATH");
    let env_source = env::var("NSPEAKER_HOSTD_PATH").ok().map(PathBuf::from);

    // manifest_dir 是 apps/desktop/src-tauri
    // 项目根目录是 manifest_dir/../../../ 即 network_speaker/
    let project_root = manifest_dir
        .join("../../../")
        .canonicalize()
        .unwrap_or_else(|_| manifest_dir.join("../../../"));

    // 根据目标平台确定来源路径
    let mut source_paths: Vec<PathBuf> = Vec::new();
    if let Some(p) = env_source {
        source_paths.push(p);
    }
    if target_triple.contains("linux") {
        source_paths.push(project_root.join("out/build/linux-ninja-system/hostd"));
        source_paths.push(project_root.join("out/build/linux-desktop/hostd"));
        source_paths.push(project_root.join("build/hostd"));
    } else if target_triple.contains("windows") {
        source_paths.push(project_root.join("out/build/windows-ninja-vcpkg/hostd.exe"));
        source_paths.push(project_root.join("out/build/windows-desktop/hostd.exe"));
        source_paths.push(project_root.join("build/hostd.exe"));
    }

    for source in source_paths {
        println!("cargo:warning=Checking hostd source: {}", source.display());
        if source.exists() {
            println!(
                "cargo:warning=Copying hostd from {} to {}",
                source.display(),
                target_binary_path.display()
            );

            // 确保 binaries 目录存在
            if let Err(e) = fs::create_dir_all(&binaries_dir) {
                println!("cargo:warning=Failed to create binaries directory: {e}");
                continue;
            }

            // 复制文件
            match fs::copy(&source, &target_binary_path) {
                Ok(_) => {
                    // 在 Unix 系统上设置可执行权限
                    #[cfg(unix)]
                    {
                        use std::os::unix::fs::PermissionsExt;
                        if let Ok(metadata) = fs::metadata(&target_binary_path) {
                            let mut perms = metadata.permissions();
                            perms.set_mode(0o755);
                            let _ = fs::set_permissions(&target_binary_path, perms);
                        }
                    }
                    println!("cargo:warning=Successfully copied hostd binary");
                    return;
                }
                Err(e) => {
                    println!(
                        "cargo:warning=Failed to copy from {}: {}",
                        source.display(),
                        e
                    );
                }
            }
        }
    }

    // No real hostd found – create a placeholder so that tauri_build::build()
    // does not fail on the externalBin resource check.  This is essential for
    // CI runs (e.g. cargo clippy) that do not build the C++ hostd binary.
    // The placeholder is overwritten on the next build when a real hostd is
    // available.  Because binaries/ is listed in .gitignore it will never be
    // committed accidentally.
    if let Err(e) = fs::create_dir_all(&binaries_dir) {
        println!("cargo:warning=Failed to create binaries directory: {e}");
    } else {
        match fs::write(&target_binary_path, "placeholder\n") {
            Ok(_) => {
                println!(
                    "cargo:warning=Created placeholder hostd: {}",
                    target_binary_path.display()
                );
            }
            Err(e) => {
                println!("cargo:warning=Failed to create placeholder hostd: {e}");
            }
        }
    }
}
