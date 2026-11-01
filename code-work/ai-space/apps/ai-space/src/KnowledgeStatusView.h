#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

struct KnowledgeStatusContext {
    bool busy = false;
    bool retryOcr = false;
    bool ocrActionBlocked = false;
    QString stage;
    QString outcome;
    QString summary;
    QJsonObject job;
    QDateTime started;
    QString message;
    QString currentDatabase;
    QString newDirectory;
    int reserveGiB = 0;
    int quotaGiB = 0;
    int cpuPercent = 0;
    int memoryMiB = 0;
};

// Presentation only: the owner schedules statistics, work and destructive actions.
class KnowledgeStatusView : public QWidget
{
  public:
    explicit KnowledgeStatusView(bool light, QWidget *parent = nullptr);
    void updateStatus(const QJsonObject &snapshot, const KnowledgeStatusContext &context);
    QPushButton *failuresButton() const { return m_failures; }
    QPushButton *cleanupButton() const { return m_cleanup; }
    QPushButton *ocrButton() const { return m_ocr; }

  private:
    QLabel *label(const QString &key, const QString &role = QStringLiteral("secondary"));
    QWidget *card(const QString &title, QVBoxLayout **layout);
    QWidget *disclosure(const QString &title, QWidget *content);
    void setText(const QString &key, const QString &text);
    QHash<QString, QLabel *> m_labels;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_failures = nullptr;
    QPushButton *m_cleanup = nullptr;
    QPushButton *m_ocr = nullptr;
    QWidget *m_liveDetails = nullptr;
};
