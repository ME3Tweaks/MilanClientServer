//! Parses and stores patch index list.

use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use regex::Regex;


pub fn make_source_path(root: &str, record: &DiffIndexRecord) -> PathBuf {
    let stripped_source_file = crate::utils::strip_leading_slashes(&record.source_file);
    Path::new(root).join("ThirdParty").join(&record.submodule).join(stripped_source_file)
}

pub fn make_diff_path(root: &str, record: &DiffIndexRecord) -> PathBuf {
    let stripped_diff_file = crate::utils::strip_leading_slashes(&record.diff_file);
    Path::new(root).join("Patching").join(stripped_diff_file)
}


/// Parsed index list.
#[derive(Clone, Debug)]
pub struct DiffIndex(pub Vec<DiffIndexRecord>);

impl DiffIndex {
    pub fn read(path: &impl AsRef<Path>) -> Self {
        let file = File::open(path).unwrap();
        let mut reader = BufReader::new(file);
        Self::parse(&mut reader)
    }

    pub fn read_or_default(path: &impl AsRef<Path>) -> Self {
        match path.as_ref().exists() {
            true => Self::read(path),
            false => Self(Vec::new()),
        }
    }

    pub fn parse(reader: &mut impl BufRead) -> Self {
        let mut index = Self(Vec::new());
        let mut string = String::new();

        let re = Regex::new(r"^([^:]+):([^:]+):([^:]+):([^:]+?)\r?\n?$").unwrap();
        while reader.read_line(&mut string).unwrap() != 0_usize {
            {
                let captures = re.captures(&string).unwrap();
                assert_eq!(captures.len(), 5);

                index.0.push(DiffIndexRecord {
                    submodule: captures.get(1).unwrap().as_str().to_string(),
                    target: captures.get(2).unwrap().as_str().to_string(),
                    source_file: captures.get(3).unwrap().as_str().to_string(),
                    diff_file: captures.get(4).unwrap().as_str().to_string(),
                });
            }

            string.clear();
        }

        index
    }

    pub fn has_pair(&self, submodule: &str, source_file: &str) -> bool {
        self.0.iter().any(|r| r.submodule == submodule && r.source_file == source_file)
    }
}


/// Parsed index list record.
#[derive(Clone, Debug)]
pub struct DiffIndexRecord {
    /// Name of the folder in `ThirdParty` containing the original source file to be patched.
    pub submodule: String,
    /// CMake target which contains the original source file.
    pub target: String,
    /// Name of the source file within `submodule` folder to be patched.
    pub source_file: String,
    /// Name of the diff file which should be used to patch `source_file`.
    pub diff_file: String,
}
