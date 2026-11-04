//! A panel can be shown in the main window (docked right or bottom) or in a window of its own. The data of a
//! panel lives in Slint globals, and each window has its own copy of them: `Host` gives the globals of the
//! window that shows a given panel, so the panel code does not care where it is.

use super::App;
use crate::{
    DialogData, FloatPanelWindow, GlossaryData, MainWindow, PipboyData, QualityData, ScriptsData, SearchData, StatsData,
    SuggestionData, TodoData,
};
use slint::ComponentHandle;

pub enum Host {
    Main(MainWindow),
    Float(FloatPanelWindow),
}

macro_rules! accessor {
    ($name:ident, $ty:ident) => {
        pub fn $name(&self) -> $ty<'_> {
            match self {
                Host::Main(w) => w.global::<$ty>(),
                Host::Float(w) => w.global::<$ty>(),
            }
        }
    };
}

impl Host {
    accessor!(glossary, GlossaryData);
    accessor!(stats, StatsData);
    accessor!(todo, TodoData);
    accessor!(suggestions, SuggestionData);
    accessor!(search, SearchData);
    accessor!(quality, QualityData);
    accessor!(scripts, ScriptsData);
    accessor!(dialog, DialogData);
    accessor!(pipboy, PipboyData);
}

impl App {
    /// The window that shows the panel `panel` (its own window when it is detached, else the main window).
    pub fn host_of(&self, panel: i32, main: &MainWindow) -> Host {
        match self.floats.get(&panel) {
            Some(f) => Host::Float(f.clone_strong()),
            None => Host::Main(main.clone_strong()),
        }
    }
}
