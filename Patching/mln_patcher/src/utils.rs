use std::collections::HashSet;
use std::fmt::Display;
use std::io::Write;
use std::path::{Path, PathBuf};

pub fn is_unique<I, M, R>(iter: I, mapping: M) -> bool
where
    I: IntoIterator,
    M: Fn(I::Item) -> R,
    R: Eq + std::hash::Hash,
{
    let mut unique_results = HashSet::<R>::new();
    iter.into_iter().map(mapping).all(move |result| unique_results.insert(result))
}

pub fn read_utf16_string(path: &impl AsRef<Path>) -> String {
    let bytes = std::fs::read(path).unwrap();
    let words = bytes.chunks(2).map(|ch| u16::from_le_bytes(ch.try_into().unwrap())).collect::<Vec<_>>();
    String::from_utf16(&words).unwrap()
}

pub fn save_temporary(root: &str, path: &str, contents: &impl Display) -> PathBuf {
    let path = Path::new(root).join("Build").join("Temporary").join(path);
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut file = std::fs::File::create(&path).unwrap();
    write!(file, "{}", contents).unwrap();
    path
}

pub fn write_all(path: impl AsRef<Path>, contents: impl AsRef<[u8]>) -> std::io::Result<()> {
    let path = path.as_ref();
    std::fs::create_dir_all(path.parent().unwrap())?;
    std::fs::write(path, contents)
}

pub fn strip_leading_slashes(source: &str) -> &str {
    source.strip_prefix(|ch| ch == '/' || ch == '\\').unwrap_or(source)
}
