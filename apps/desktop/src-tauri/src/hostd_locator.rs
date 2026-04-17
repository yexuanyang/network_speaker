use std::path::{Path, PathBuf};

use crate::platform::hostd_binary_name;

pub fn locate(base_dir: &Path) -> Option<PathBuf> {
    // 1. Environment variable override
    if let Ok(env_path) = std::env::var("NSPEAKER_HOSTD_PATH") {
        let p = PathBuf::from(&env_path);
        if p.is_file() {
            return Some(p);
        }
    }

    let binary = hostd_binary_name();

    // 2. Same directory as the running binary (bundled / sidecar)
    let same_dir = base_dir.join(binary);
    if same_dir.is_file() {
        return Some(same_dir);
    }

    // 3. Walk up looking for CMakeLists.txt, then check known build output directories
    let build_dirs = [
        "out/build/windows-ninja-vcpkg",
        "out/build/linux-ninja-system",
        "build-windows-release",
        "build-windows-verify",
        "build-windows",
        "build",
    ];

    let mut current = base_dir.to_path_buf();
    for _ in 0..10 {
        if current.join("CMakeLists.txt").is_file() {
            for dir in &build_dirs {
                let candidate = current.join(dir).join(binary);
                if candidate.is_file() {
                    return Some(candidate);
                }
            }
            break;
        }
        if !current.pop() {
            break;
        }
    }

    None
}
