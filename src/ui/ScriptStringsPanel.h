#pragma once

#include "core/ScriptStrings.h"

#include <QHash>
#include <QWidget>
#include <atomic>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// Texts written in the code of the scripts ("You see Sammy.", display_msg(...)): they are not in any .msg file.
// The panel lists them, takes a translation for each, and writes the translated scripts into the output.
class ScriptStringsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptStringsPanel(QWidget *parent = nullptr);
    ~ScriptStringsPanel() override;

    void setProject(const std::shared_ptr<Project> &project);

    // For the tests
    void scanNow();
    bool scanning() const { return m_scanning; }
    int rowCountShown() const;
    QVector<ScriptStrings::Text> texts() const { return m_texts; }
    void setTranslation(int textIndex, const QString &translation);
    QVector<ScriptTranslation> translations() const;
    bool allowEnlarge() const;

signals:
    void translationsChanged(const QVector<ScriptTranslation> &translations);
    void applyRequested(const QVector<ScriptTranslation> &translations, bool allowEnlarge);

private:
    void fill();
    void updateRowState(int row);
    void applyFilter();
    static QString keyOf(const QString &script, quint32 operand);

    std::shared_ptr<Project> m_project;
    QVector<ScriptStrings::Text> m_texts;
    QHash<QString, QString> m_targets;           // "script#operand" -> translation
    QPushButton *m_scan = nullptr;
    QPushButton *m_apply = nullptr;
    QCheckBox *m_enlarge = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_status = nullptr;
    QTableWidget *m_table = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    int m_generation = 0;
    bool m_scanning = false;
    bool m_filling = false;
};
