#include "Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace Theme {

namespace {

bool g_dark = false;
Colors g_colors;

void buildColors()
{
    if (g_dark) {
        g_colors.editorBack = QColor(0x1e, 0x1f, 0x22);
        g_colors.editorFore = QColor(0xdf, 0xe1, 0xe5);
        g_colors.editorSelection = QColor(0x2f, 0x4f, 0x7f);
        g_colors.caretLine = QColor(0x26, 0x28, 0x2c);
        g_colors.term = QColor(0x5c, 0xb0, 0xff);
        g_colors.fuzzyTerm = QColor(0x8c, 0xc6, 0xff);
        g_colors.issue = QColor(0xff, 0xa8, 0x3d);
        g_colors.spelling = QColor(0xff, 0x6b, 0x6b);
        g_colors.untranslated = QColor(0xc0, 0x50, 0x50, 55);
        g_colors.translated = QColor(0x50, 0xa0, 0x60, 40);
        g_colors.review = QColor(0xd0, 0xa0, 0x30, 70);
    } else {
        g_colors.editorBack = QColor(0xff, 0xff, 0xff);
        g_colors.editorFore = QColor(0x20, 0x22, 0x26);
        g_colors.editorSelection = QColor(0xb7, 0xd5, 0xff);
        g_colors.caretLine = QColor(0xf3, 0xf5, 0xf8);
        g_colors.term = QColor(0x0b, 0x57, 0xd0);
        g_colors.fuzzyTerm = QColor(0x3d, 0x8b, 0xe8);
        g_colors.issue = QColor(0xd9, 0x77, 0x06);
        g_colors.spelling = QColor(0xd9, 0x30, 0x25);
        g_colors.untranslated = QColor(0xe0, 0x60, 0x60, 45);
        g_colors.translated = QColor(0x40, 0xa0, 0x50, 35);
        g_colors.review = QColor(0xe0, 0xa0, 0x20, 70);
    }
}

QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x2b, 0x2d, 0x31), base(0x1e, 0x1f, 0x22), text(0xdf, 0xe1, 0xe5);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x25, 0x27, 0x2b));
    p.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3c, 0x42));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x36, 0x38, 0x3d));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(0x5c, 0xb0, 0xff));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xd0));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8d, 0x93));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x82, 0x88));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x82, 0x88));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x82, 0x88));
    return p;
}

QPalette lightPalette()
{
    QPalette p;
    const QColor window(0xf0, 0xf0, 0xf0), base(0xff, 0xff, 0xff), text(0x20, 0x22, 0x26);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf6, 0xf8));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0xe8, 0xe8, 0xe8));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(0x0b, 0x57, 0xd0));
    p.setColor(QPalette::Highlight, QColor(0x30, 0x7c, 0xd8));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x82, 0x88));
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe3, 0xe3, 0xe3));
    p.setColor(QPalette::Mid, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Dark, QColor(0x78, 0x78, 0x78));
    p.setColor(QPalette::Shadow, QColor(0x69, 0x69, 0x69));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x90, 0x92, 0x98));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x90, 0x92, 0x98));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x90, 0x92, 0x98));
    return p;
}

}

Mode savedMode()
{
    const QString v = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    if (v == QLatin1String("dark"))
        return Mode::Dark;
    if (v == QLatin1String("light"))
        return Mode::Light;
    return Mode::System;
}

void saveMode(Mode mode)
{
    const char *name = mode == Mode::Dark ? "dark" : mode == Mode::Light ? "light" : "system";
    QSettings().setValue(QStringLiteral("ui/theme"), QString::fromLatin1(name));
}

void apply(Mode mode)
{
    if (mode == Mode::System)
        g_dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    else
        g_dark = mode == Mode::Dark;

    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    qApp->setPalette(g_dark ? darkPalette() : lightPalette());
    buildColors();
}

bool isDark()
{
    return g_dark;
}

const Colors &colors()
{
    if (!g_colors.editorBack.isValid())
        buildColors();
    return g_colors;
}

int toScintilla(const QColor &c)
{
    return c.red() | (c.green() << 8) | (c.blue() << 16);
}

QColor statusTint(SegmentStatus status)
{
    switch (status) {
    case SegmentStatus::Untranslated: return colors().untranslated;
    case SegmentStatus::ToReview: return colors().review;
    case SegmentStatus::Translated: return colors().translated;
    }
    return {};
}

QString statusText(SegmentStatus status)
{
    switch (status) {
    case SegmentStatus::Untranslated: return QCoreApplication::translate("Theme", "Untranslated");
    case SegmentStatus::ToReview: return QCoreApplication::translate("Theme", "To review");
    case SegmentStatus::Translated: return QCoreApplication::translate("Theme", "Translated");
    }
    return {};
}

}
