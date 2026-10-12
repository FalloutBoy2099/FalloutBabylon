#pragma once

#include <QString>

// Settings of the translation and correction services (stored with QSettings, per user).
// The DeepL key is kept in the user's settings store as is: do not share the settings file.
namespace AppSettings {

QString deeplKey();
void setDeeplKey(const QString &key);

bool deeplUseGlossary();
void setDeeplUseGlossary(bool on);
bool deeplUseContext();
void setDeeplUseContext(bool on);
bool deeplMarkReview();          // machine translations are marked "to review"
void setDeeplMarkReview(bool on);
QString deeplFormality();        // "", "prefer_more", "prefer_less"
void setDeeplFormality(const QString &value);

QString grammalecteUrl();
void setGrammalecteUrl(const QString &url);
QString languageToolUrl();
void setLanguageToolUrl(const QString &url);
bool checkEnabled();
void setCheckEnabled(bool on);


// Pip-Boy holodisk layout
int pipboyMaxWidth();               // pixels of the text area (374 in the game)
void setPipboyMaxWidth(int px);
int pipboyFallbackChars();          // characters per line when the game font is not available
void setPipboyFallbackChars(int n);

}
