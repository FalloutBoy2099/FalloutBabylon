//! Actions on a whole project: export to a DAT archive, the output folder.

use super::App;
use crate::MainWindow;
use babylon_core::dat_archive::Format;
use babylon_core::dat_writer::{self, DatWriteItem};
use babylon_core::i18n::{tr, trf};
use babylon_core::output_archive;
use std::path::Path;

fn collect(dir: &Path, root: &Path, out: &mut Vec<(String, Vec<u8>)>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for entry in rd.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect(&path, root, out);
        } else if let (Ok(data), Ok(rel)) = (std::fs::read(&path), path.strip_prefix(root)) {
            out.push((rel.to_string_lossy().replace('\\', "/"), data));
        }
    }
}

impl App {
    pub fn export_to_dat(&mut self, window: &MainWindow, id: &str) {
        let Some(si) = self.session_index(id) else { return };
        self.save_all(window);
        let config = self.sessions[si].project.config().clone();
        let root = Path::new(&config.output_dir);
        let text_dir = root.join("text").join(&config.target_language);
        if !text_dir.is_dir() {
            self.notify(
                &tr("Export"),
                &trf("Nothing to export yet: save some translated files first.\n(%1 does not exist)", &[&text_dir.to_string_lossy()]),
            );
            return;
        }
        let Some(path) = self.picker(window)
            .set_title(tr("Export to a DAT archive"))
            .set_directory(root)
            .set_file_name(format!("{}.dat", config.target_language))
            .add_filter(tr("Fallout 2 archive (zlib) (*.dat)"), &["dat"])
            .add_filter(tr("Fallout 1 archive (*.dat)"), &["dat"])
            .save_file()
        else {
            return;
        };
        // the archive format follows the project's output format, Fallout 2 by default
        let format = if config.output_archive_format == "dat1" { Format::Fallout1 } else { Format::Fallout2 };
        let mut files = Vec::new();
        collect(&text_dir, root, &mut files);
        let items: Vec<DatWriteItem> = files.into_iter().map(|(p, d)| DatWriteItem::from_data(p, d)).collect();
        match dat_writer::write(&path, format, &items) {
            Ok(()) => self.notify(&tr("Export"), &trf("%1 files written to\n%2", &[&items.len().to_string(), &path.to_string_lossy()])),
            Err(e) => self.notify(&tr("Export"), &trf("Cannot write the archive:\n%1", &[&e.to_string()])),
        }
    }

    pub fn open_output_folder(&mut self, id: &str) {
        let Some(si) = self.session_index(id) else { return };
        let config = self.sessions[si].project.config();
        let dir = if output_archive::enabled(config) {
            Path::new(&config.output_archive).parent().map(|p| p.to_path_buf()).unwrap_or_default()
        } else {
            Path::new(&config.output_dir).to_path_buf()
        };
        let _ = std::fs::create_dir_all(&dir);
        let _ = open::that(dir);
    }

    pub fn build_archive_now(&mut self, window: &MainWindow, id: &str) {
        self.save_all(window);
        self.build_archive(window, id);
    }
}
