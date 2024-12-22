#![allow(dead_code)]

//! # Milan Patcher
//!
//! Despite its name, this tool does nothing to patch Project Milan's
//! contents or binaries. It is designed purely as a build-system step
//! to intelligently apply diff patches to LExSDKv2 submodule.
//!
//! For example:
//! ```powershell
//! > mln_patcher.exe apply --root D:\Milan\Code\MilanClientServer
//! ```

use std::path::{Path, PathBuf};
use clap::{arg, command, Command};
use patch::Patch;

mod diff;
mod index;
mod state;
mod utils;

use crate::index::DiffIndex;
use crate::state::DiffState;

fn main() {
    let matches = command!()
        .about("Build-system tool for patching submodules, part of Project Milan.")
        .subcommand_required(true)
        .subcommand(
            Command::new("apply")
                .about("Selectively applies all relevant diffs.")
                .arg(arg!(-r --root <DIRECTORY> "Specifies workspace root").required(true))
        )
        .get_matches();

    if let Some(apply_matches) = matches.subcommand_matches("apply") {
        let root = apply_matches.get_one::<String>("root").unwrap();
        handle_apply(root);
    }
}

fn handle_apply(root: &str) {
    // Read and parse diff index list.
    let index_path = Path::new(root).join("Patching").join("_index.list");
    let index_contents = DiffIndex::read_or_default(&index_path);

    // Check that there are no duplicate module-file pairs.
    let all_pairs_unique = utils::is_unique(&index_contents.0, |r| (&r.submodule, &r.source_file));
    assert!(all_pairs_unique, "expected all module-file pairs to be unique in the index");

    // Pass 1: Check and rebuild state index.
    let state_path = Path::new(root).join("Build").join("Patched").join("_state.list");
    let mut state_contents = DiffState::read_or_default(&state_path);
    state_contents.update_from_index(&index_contents, root);
    state_contents.assert_no_removed_diffs();
    state_contents.write(state_path).unwrap();

    // Pass 2: Apply every diff record.
    for record in &index_contents.0 {
        let begin_string = format!("// MILAN PATCH BEGIN, DIFF = {}", record.diff_file);
        let end_string = format!("// MILAN PATCH END, DIFF = {}", record.diff_file);

        // Read and parse the diff file.
        let diff_path = Path::new(root).join("Patching").join(&record.diff_file);
        let diff_contents = utils::read_utf16_string(&diff_path);
        let mut patch = Patch::from_single(&diff_contents).unwrap();

        // Enrich it with insertion prefixes and postfixes.
        let mut temp_diff_path = PathBuf::new();
        diff::enrich_patch(&mut patch, &begin_string, &end_string);
        diff::save_temporary_patch(&patch, root, &record.diff_file, &mut temp_diff_path);
    }
}
