#pragma once
class PointerEffect;

#include <QColor>
#include <QByteArray>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QDateTime>
#include <QElapsedTimer>

class QContextMenuEvent;
class QHideEvent;
class QJsonObject;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QProcess;
class QShowEvent;
class QTemporaryFile;
class QWheelEvent;
class QMoveEvent;
class QResizeEvent;
class QDialog;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QNetworkAccessManager;
class QUrl;

struct ProcessInfo {
    QString name;
    QString executablePath;
    double cpuPercent = 0.0;
    double rssMb = 0.0;
    quint64 startTicks = 0;
    int pid = 0;
    bool cpuValid=false;
    int uid=-1;
    QString state;
};

class SystemMonitor : public QWidget
{
    Q_OBJECT
    friend struct PointerEffectTestAccess;
public:
    enum class Skin {
        Dark,
        Light,
        Cyber,
        Glass,
        Wallpaper,
        Liquid
    };

    explicit SystemMonitor(QWidget *parent = nullptr);
    static constexpr int COVERED_STAT_INTERVAL_SEC = 30;
    int statIntervalMs() const;
    ~SystemMonitor() override;

    QWidget *createSettingsPage(QWidget *parent);
    void setEditMode(bool edit);
    void refreshWallpaperTheme();
    void setSkin(Skin skin);
    void resetVisualDefaults(QWidget *settingsPage = nullptr);
    Skin skin() const { return m_skin; }

    static bool autoStartEnabled();
    void setAutoStart(bool enabled);
    bool autoStart() const { return m_autoStart; }

signals:
    void closeRequested();
    void appearanceChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void publishVideoCutout();
    friend struct SystemMonitorTestAccess;
    struct CpuTotals {
        quint64 total = 0;
        quint64 idle = 0;
        quint64 iowait = 0;
        QVector<quint64> coreTotal;
        QVector<quint64> coreIdle;
    };

    struct MemStats {
        quint64 totalKiB = 0;
        quint64 availableKiB = 0;
        quint64 cacheKiB = 0;
        quint64 swapTotalKiB = 0;
        quint64 swapFreeKiB = 0;
    };

    struct DiskIoTotals {
        quint64 readSectors = 0;
        quint64 writeSectors = 0;
    };

    struct ProcessDetails {
        QString name;
        QString executableName;
        QString state;
        double rssMb = 0.0;
        double virtualMemoryMb = 0.0;
        quint64 startTicks = 0;
        quint64 readBytes = 0;
        quint64 writeBytes = 0;
        quint64 voluntaryContextSwitches = 0;
        quint64 involuntaryContextSwitches = 0;
        int parentPid = 0;
        int threadCount = 0;
        int uid = -1;
        int fileDescriptorCount = -1;
        bool hasIoCounters = false;
        bool valid = false;
    };

    struct Palette {
        QColor panel;
        QColor card;
        QColor border;
        QColor text;
        QColor muted;
        QColor cpu;
        QColor memory;
        QColor disk;
        QColor track;
    };

    void refreshStats();
    static CpuTotals readCpuTotals();
    static MemStats readMemStats();
    static QVector<ProcessInfo> readProcessSnapshot(
        int count, quint64 totalDelta, QHash<QString, quint64> &previousTicks, int cores);
    static ProcessDetails readProcessDetails(int pid);
    static DiskIoTotals readDiskIoTotals();
    static QVector<double> readLoadAverages();
    static QString readCpuModel();
    static QString platformDescription();
    static QString defaultWidgetTitle();
    struct Sample {
        CpuTotals cpu;
        MemStats memory;
        DiskIoTotals io;
        ProcessDetails details;
        QVector<ProcessInfo> processes;
        QHash<QString, quint64> ticks;
        QVector<double> frequencies, loads;
        double diskTotal = 0, diskUsed = 0, diskPercent = 0;
        struct FrameworkInfo {
            QString id, name, status;
            QVector<int> pids;
            double cpu=0, rss=0, pss=0, swap=0;
            bool cpuValid=false, memoryValid=false, warning=false;
        };
        QVector<FrameworkInfo> framework;
    };
    static QVector<Sample::FrameworkInfo> readFrameworkSnapshot(const QVector<ProcessInfo> &processes);
    static Sample collectSample(bool processes, int count, quint64 previousTotal,
        QHash<QString, quint64> previousTicks, int targetPid, bool disk, bool framework=false);
    void applyStats(const Sample &sample, bool heavy);
    void setCompact(bool compact);
    void restoreSettings();
    void saveSettings() const;
    QPoint boundedPosition(const QPoint &position) const;

    Palette paletteForSkin() const;
    void drawHeader(QPainter &p, const Palette &colors);
    void drawMetricCard(QPainter &p, const QRect &rect,
                        const QString &title, const QString &detail,
                        double percent, const QColor &accent,
                        const QVector<double> &history);
    void drawProcessPanel(QPainter &p, const QRect &rect,
                          const Palette &colors);
    void drawOverviewPanel(QPainter &p, const QRect &rect,
                     const Palette &colors);
    void drawButton(QPainter &p, const QRect &rect, const QString &text,
                    const QColor &foreground, const QColor &background,
                    const QColor &border);
    void drawRing(QPainter &p, const QRectF &rect, double percent,
                  const QColor &accent, const QColor &track);
    void drawSparkline(QPainter &p, const QRectF &rect,
                       const QVector<double> &history,
                       const QColor &accent);
    void appendHistory(QVector<double> &history, double value);
    void showSettingsDialog();
    QFont uiFont(int sizeOffset = 0, bool bold = false) const;
    void updateHitRects();
    QSize baseSize() const;
    int logicalWidth() const;
    int logicalHeight() const;
    QPoint logicalPosition(const QPoint &position) const;
    QString tooltipAt(const QPoint &logicalPosition) const;
    static double readCoreFrequencyGhz(int core);
    QColor wallpaperSampleColor() const;

    bool m_statsPending=false;
    QString resourceSnapshotText(const ProcessInfo *process=nullptr) const;
    void copyResourceSnapshot(const ProcessInfo *process=nullptr);
    void openResourceSnapshot();
    void exportResourceSnapshot(const QString &snapshot={});
    QTimer m_timer;
    CpuTotals m_prevCpu;
    quint64 m_lastProcTotal = 0;
    QHash<QString, quint64> m_prevProcTicks;

    QString m_cpuModel;
    int m_coreCount = 1;
    double m_cpuPercent = 0.0;
    double m_memPercent = 0.0;
    double m_memUsedGb = 0.0;
    double m_memTotalGb = 0.0;
    double m_memCacheGb = 0.0;
    double m_diskPercent = 0.0;
    double m_diskUsedGb = 0.0;
    double m_diskTotalGb = 0.0;
    QVector<double> m_corePercents;
    QVector<double> m_coreFrequenciesGhz;
    QVector<double> m_cpuHistory;
    QVector<double> m_memHistory;
    QVector<double> m_diskHistory;
    QVector<ProcessInfo> m_topProcs;
    QVector<double> m_loads;
    DiskIoTotals m_previousDiskIo;
    QElapsedTimer m_sampleClock;
    QDateTime m_lastUpdated;
    double m_diskReadMb=0,m_diskWriteMb=0,m_swapUsedGb=0;
    bool m_sortByMemory=false;
    bool m_showFramework=true;
    QVector<Sample::FrameworkInfo> m_framework;
    QVector<QRect> m_frameworkRowRects;
    QString m_widgetTitle = QStringLiteral("桌面资源监控");
    QString m_widgetFontFamily;
    int m_widgetFontSize = 9;
    int m_windowOpacity = 100;
    QColor m_wallpaperThemeColor;
    QImage m_liquidGlassImage;
    QRect m_liquidGlassGeometry;
    PointerEffect *m_liquidPointerEffect = nullptr;
    QPointF m_liquidGlassPointer {-1000, -1000};
    bool m_compact = false;
    bool m_hovered = false;
    bool m_editMode = false;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_mouseGrabbed = false;
    bool m_autoStart = false;
    bool m_premiumAesthetics = false;
    QPoint m_dragOffset;
    QPoint m_resizeStartGlobal;
    QSize m_resizeStartSize;
    QPoint m_resizeStartPosition;
    double m_scale = 1.0;
    int m_statIntervalSec = 5;
    Skin m_skin = Skin::Dark;
    int m_tick = 0;
    QRect m_optimizeRect;
    QRect m_toggleRect;
    QRect m_skinRect;
    QRect m_settingsRect;
    QRect m_cpuRingRect;
    QRect m_memoryRingRect;
    QRect m_diskRingRect;
    QRect m_overviewRect,m_copyRect,m_detailsRect;
    QPointer<QDialog> m_resourceDialog;
    QRect m_resizeHandleRect;
    QVector<QRect> m_coreRects;
    QVector<QRect> m_processRowRects;

    static constexpr int COMPACT_W = 720;
    static constexpr int COMPACT_H = 248;
    static constexpr int EXPANDED_W = 900;
    static constexpr int EXPANDED_H = 486;
    static constexpr int HEADER_H = 54;
    static constexpr int MARGIN = 16;
    static constexpr int CORE_LIMIT = 32;
    static constexpr int DEFAULT_STAT_INTERVAL_SEC = 5;
    static constexpr int HEAVY_EVERY_TICKS = 6;
    static constexpr int HISTORY_POINTS = 60;
};
