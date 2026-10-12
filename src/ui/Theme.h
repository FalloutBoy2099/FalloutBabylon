#pragma once

#include "core/Project.h"

#include <QColor>

// Light / dark appearance for the Qt widgets and for the Scintilla editors.
namespace Theme {

enum class Mode { System, Light, Dark };

Mode savedMode();
void saveMode(Mode mode);

// Applies the Fusion style and the matching palette to the whole application.
void apply(Mode mode);
bool isDark();

struct Colors {
    QColor editorBack, editorFore, editorSelection, caretLine;
    QColor term, fuzzyTerm, issue, spelling, grammar;
    QColor untranslated, translated, review;   // row tints
};
const Colors &colors();

// Scintilla colours are 0xBBGGRR integers.
int toScintilla(const QColor &color);

QColor statusTint(SegmentStatus status);
QString statusText(SegmentStatus status);

}
