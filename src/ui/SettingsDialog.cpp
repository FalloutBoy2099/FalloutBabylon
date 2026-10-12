#include "SettingsDialog.h"

#include "TranslationService.h"
#include "core/AppSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(TranslationService *service, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
{
    setWindowTitle(tr("Settings"));
    resize(560, 440);

    // --- DeepL
    m_key = new QLineEdit(AppSettings::deeplKey());
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(tr("DeepL API key (free keys end with :fx)"));
    auto *testKey = new QPushButton(tr("Test"));
    m_keyStatus = new QLabel;
    m_keyStatus->setWordWrap(true);
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(testKey);

    m_formality = new QComboBox;
    m_formality->addItem(tr("Default"), QString());
    m_formality->addItem(tr("More formal"), QStringLiteral("prefer_more"));
    m_formality->addItem(tr("Less formal"), QStringLiteral("prefer_less"));
    m_formality->setCurrentIndex(qMax(0, m_formality->findData(AppSettings::deeplFormality())));
    m_glossary = new QCheckBox(tr("Use the project glossary (terms of the /game files; the free plan allows one glossary)"));
    m_glossary->setChecked(AppSettings::deeplUseGlossary());
    m_context = new QCheckBox(tr("Send the neighbouring lines as context (not translated, not billed)"));
    m_context->setChecked(AppSettings::deeplUseContext());
    m_markReview = new QCheckBox(tr("Mark machine translations as \"to review\""));
    m_markReview->setChecked(AppSettings::deeplMarkReview());

    auto *deeplBox = new QGroupBox(tr("DeepL"));
    auto *deeplForm = new QFormLayout(deeplBox);
    deeplForm->addRow(tr("API key"), keyRow);
    deeplForm->addRow(QString(), m_keyStatus);
    deeplForm->addRow(tr("Formality"), m_formality);
    deeplForm->addRow(m_glossary);
    deeplForm->addRow(m_context);
    deeplForm->addRow(m_markReview);

    // --- correction
    m_check = new QCheckBox(tr("Underline spelling and grammar errors in the translation"));
    m_check->setChecked(AppSettings::checkEnabled());
    m_grammalecte = new QLineEdit(AppSettings::grammalecteUrl());
    m_grammalecte->setPlaceholderText(QStringLiteral("http://host:7050/"));
    m_languageTool = new QLineEdit(AppSettings::languageToolUrl());
    m_languageTool->setPlaceholderText(QStringLiteral("http://host:7060/"));
    auto *testG = new QPushButton(tr("Test"));
    auto *testL = new QPushButton(tr("Test"));
    m_grammalecteStatus = new QLabel;
    m_languageToolStatus = new QLabel;
    auto *gRow = new QHBoxLayout;
    gRow->addWidget(m_grammalecte, 1);
    gRow->addWidget(testG);
    auto *lRow = new QHBoxLayout;
    lRow->addWidget(m_languageTool, 1);
    lRow->addWidget(testL);

    auto *checkBox = new QGroupBox(tr("Correction"));
    auto *checkForm = new QFormLayout(checkBox);
    checkForm->addRow(m_check);
    checkForm->addRow(tr("Grammalecte server (French)"), gRow);
    checkForm->addRow(QString(), m_grammalecteStatus);
    checkForm->addRow(tr("LanguageTool server (other languages)"), lRow);
    checkForm->addRow(QString(), m_languageToolStatus);
    auto *note = new QLabel(tr("The text of the line being edited is sent to these servers: use servers you trust."));
    note->setWordWrap(true);
    checkForm->addRow(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(deeplBox);
    layout->addWidget(checkBox);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(testKey, &QPushButton::clicked, this, &SettingsDialog::testDeepL);
    connect(testG, &QPushButton::clicked, this, [this] { testServer(true); });
    connect(testL, &QPushButton::clicked, this, [this] { testServer(false); });
}

void SettingsDialog::save()
{
    AppSettings::setDeeplKey(m_key->text());
    AppSettings::setDeeplFormality(m_formality->currentData().toString());
    AppSettings::setDeeplUseGlossary(m_glossary->isChecked());
    AppSettings::setDeeplUseContext(m_context->isChecked());
    AppSettings::setDeeplMarkReview(m_markReview->isChecked());
    AppSettings::setCheckEnabled(m_check->isChecked());
    AppSettings::setGrammalecteUrl(m_grammalecte->text());
    AppSettings::setLanguageToolUrl(m_languageTool->text());
}

void SettingsDialog::testDeepL()
{
    m_keyStatus->setText(tr("Contacting DeepL..."));
    QPointer<QLabel> status(m_keyStatus);   // the dialog may be closed before the answer arrives
    m_service->deepl()->usage(m_key->text(), [status](qint64 used, qint64 limit, const QString &error) {
        if (!status)
            return;
        if (!error.isEmpty())
            status->setText(QStringLiteral("✖ ") + error);
        else
            status->setText(QObject::tr("✔ Key accepted. Usage: %L1 / %L2 characters this period.").arg(used).arg(limit));
    });
}

void SettingsDialog::testServer(bool grammalecte)
{
    QPointer<QLabel> status(grammalecte ? m_grammalecteStatus : m_languageToolStatus);
    const QString url = (grammalecte ? m_grammalecte : m_languageTool)->text().trimmed();
    if (url.isEmpty()) {
        status->setText(tr("Enter the server address first."));
        return;
    }
    status->setText(tr("Contacting the server..."));
    m_service->grammar()->check(grammalecte ? GrammarClient::Grammalecte : GrammarClient::LanguageTool, url,
                                QStringLiteral("french"), QStringLiteral("Je suis aller au marché."),
                                [status](const QVector<GrammarIssue> &issues, const QString &error) {
                                    if (!status)
                                        return;
                                    if (!error.isEmpty())
                                        status->setText(QStringLiteral("✖ ") + error);
                                    else
                                        status->setText(QObject::tr("✔ The server answers (%1 issue(s) found in the test sentence).")
                                                            .arg(issues.size()));
                                });
}
