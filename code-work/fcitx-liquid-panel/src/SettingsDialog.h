#pragma once
#include "CandidateWindow.h"
#include <QDialog>
#include <QMap>
#include <QLabel>
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(CandidateWindow &live,QWidget *parent=nullptr);
    const CandidateConfig &draft() const { return m_draft; }
    void resetDefaults();
    bool applySettings();
    void reject() override;
signals:
    void settingsApplied(bool success);
private:
    bool eventFilter(QObject *,QEvent *) override;
    void populate();
    void changed(bool coalescePreview=false);
    void flushPreview();
    CandidateWindow &m_live;
    CandidateConfig m_draft;
    CandidateWindow *m_preview=nullptr;
    QMap<QString,QWidget *> m_controls;
    QLabel *m_status;
    QTimer m_previewTimer;
    bool m_populating=false;
    bool m_saving=false;
    bool m_closeAfterSave=false;
};
