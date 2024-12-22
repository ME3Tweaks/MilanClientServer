use std::fmt::{self, Write};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;

use regex::Regex;
use xxhash_rust::xxh3::xxh3_64;

use crate::index::{DiffIndex, make_source_path, make_diff_path};


/// Parsed state list.
#[derive(Clone, Debug)]
pub struct DiffState(pub Vec<DiffStateRecord>);

impl DiffState {
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

        let re = Regex::new(r"^([^:]+):([^:]+):([0-9A-Za-z]+):([0-9A-Za-z]+):([0-9A-Za-z]+)\r?\n?$").unwrap();
        while reader.read_line(&mut string).unwrap() != 0_usize {
            {
                let captures = re.captures(&string).unwrap();
                assert_eq!(captures.len(), 6);

                index.0.push(DiffStateRecord {
                    submodule: captures.get(1).unwrap().as_str().to_string(),
                    source_file: captures.get(2).unwrap().as_str().to_string(),
                    source_hash: FileHash(u64::from_str_radix(captures.get(3).unwrap().as_str(), 16).unwrap()),
                    diff_hash: FileHash(u64::from_str_radix(captures.get(4).unwrap().as_str(), 16).unwrap()),
                    clean_apply_hash: FileHash(u64::from_str_radix(captures.get(5).unwrap().as_str(), 16).unwrap()),
                    missing_from_index: false,
                });
            }

            string.clear();
        }

        index
    }

    pub fn write(&self, path: impl AsRef<Path>) -> std::io::Result<()> {
        let mut string = String::new();
        for record in &self.0 {
            writeln!(
                string,
                "{}:{}:{:016X}:{:016X}:{:016X}",
                record.submodule,
                record.source_file,
                record.source_hash.0,
                record.diff_hash.0,
                record.clean_apply_hash.0,
            ).unwrap();
        }
        crate::utils::write_all(path, string)
    }

    pub fn has_pair(&self, submodule: &str, source_file: &str) -> bool {
        self.0.iter().any(|r| r.submodule == submodule && r.source_file == source_file)
    }

    pub fn update_from_index(&mut self, index: &DiffIndex, root: &str) {
        // Mark state records without a matching index record.
        for state_record in self.0.iter_mut() {
            if !index.has_pair(&state_record.submodule, &state_record.source_file) {
                state_record.missing_from_index = true;
            }
        }

        // TODO: Populate missing state records for which there is an index record.
        for index_record in index.0.iter() {
            if !self.has_pair(&index_record.submodule, &index_record.source_file) {
                self.0.push(DiffStateRecord {
                    submodule: index_record.submodule.clone(),
                    source_file: index_record.source_file.to_string(),
                    source_hash: FileHash::calculate(make_source_path(root, index_record)).unwrap(),
                    diff_hash: FileHash::calculate(make_diff_path(root, index_record)).unwrap(),
                    clean_apply_hash: FileHash(u64::MAX),  // TODO: Calculate using a temporary directory.
                    missing_from_index: false,
                })
            }
        }
    }

    pub fn assert_no_removed_diffs(&self) {
        let mut has_removed_diffs = false;
        for state_record in self.0.iter().filter(|r| r.missing_from_index) {
            eprintln!("state missing index record: submodule = {}, source = {}", state_record.submodule, state_record.source_file);
            has_removed_diffs = true;
        }
        assert!(!has_removed_diffs, "one or more states are missing index records");
    }
}


/// Parsed state list record.
#[derive(Clone, Debug)]
pub struct DiffStateRecord {
    /// Name of the folder in `ThirdParty` containing the original source file to be patched.
    pub submodule: String,
    /// Name of the source file within `submodule` folder to be patched.
    pub source_file: String,
    /// Hash of the original source file sitting in vendored tree.
    pub source_hash: FileHash,
    /// Hash of the original diff patch sitting in patching tree.
    pub diff_hash: FileHash,
    /// Hash of the file you get when you apply original diff patch to the original source file.
    pub clean_apply_hash: FileHash,
    /// (TRANSIENT) Whether there is no matching record in diff index list.
    pub missing_from_index: bool,
}


#[derive(Clone)]
pub struct FileHash(pub u64);

impl FileHash {
    pub fn calculate(path: impl AsRef<Path>) -> Result<FileHash, std::io::Error> {
        let contents = std::fs::read(path.as_ref())?;
        Ok(Self(xxh3_64(&contents)))
    }
}

impl fmt::Debug for FileHash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "0x{:016X}", self.0)
    }
}

impl fmt::Display for FileHash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "0x{:016X}", self.0)
    }
}
