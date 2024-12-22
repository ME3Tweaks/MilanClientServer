//! Applies diffs, optionally having rewritten them.

use std::mem::replace;
use std::path::PathBuf;
use patch::{Line, Patch};


pub fn enrich_patch<'a>(patch: &mut Patch<'a>, begin_string: &'a str, end_string: &'a str) {
    for hunk in &mut patch.hunks {
        let mut lines = vec![];
        let mut added = 0_u64;

        // False value here accounts for hunks which start with "add" lines.
        let mut is_prev_add = false;

        for line in &hunk.lines {
            if let Line::Add(_) = line {
                if !replace(&mut is_prev_add, true) {
                    lines.push(Line::Add(""));
                    lines.push(Line::Add(begin_string));
                    lines.push(Line::Add(""));
                    added = added.saturating_add(1);
                }
                lines.push(line.clone());
            } else {
                if replace(&mut is_prev_add, false) {
                    lines.push(Line::Add(""));
                    lines.push(Line::Add(end_string));
                    lines.push(Line::Add(""));
                    added = added.saturating_add(1);
                }
                lines.push(line.clone());
            }
        }

        if is_prev_add {
            lines.push(Line::Add(""));
            lines.push(Line::Add(end_string));
            lines.push(Line::Add(""));
            added = added.saturating_add(1);
        }

        // Update post-image line count by (times we added anything * number of added lines).
        hunk.new_range.count += added.saturating_add(3);
        hunk.lines = lines;
    }
}

pub fn save_temporary_patch(patch: &Patch<'_>, root_dir: &str, in_name: &str, out_path: &mut PathBuf) {
    *out_path = crate::utils::save_temporary(root_dir, in_name, patch)
}
