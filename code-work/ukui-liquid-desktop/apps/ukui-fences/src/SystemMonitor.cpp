#include "PointerEffect.h"
#include <QPaintEvent>
#include "../../../shared/async-work/BackgroundTask.h"
#include "LiquidPopup.h"
#include "LiquidDialog.h"
#include <QPlainTextEdit>
#include <QSaveFile>
#include "SystemMonitor.h"
#include "GlassFinish.h"
#include "../../../shared/liquid-glass/src/LiquidMaterial.h"
#include "Palette.h"
#include "../../../shared/liquid-glass/src/CohesiveGlass.h"
#include <QUrl>
#include "DesktopCanvas.h"
#include "MenuStyle.h"
#include "VideoWallpaperRegion.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QScreen>
#include <QSpinBox>
#include <QSlider>
#include <QSignalBlocker>
#include <QSharedPointer>
#include <QShowEvent>
#include <QResizeEvent>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

QStringList splitBySpace(const QString &text)
{
    QStringList result;
    QString current;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }
    if (!current.isEmpty())
        result.append(current);
    return result;
}

double clampPercent(double value)
{
    return std::max(0.0, std::min(100.0, value));
}

QString compactModelName(QString name)
{
    name.replace(QStringLiteral("(R)"), QString());
    name.replace(QStringLiteral("(TM)"), QString());
    name = name.simplified();
    if (name.size() > 24)
        name = name.left(23) + QStringLiteral("…");
    return name;
}

double averageOf(const QVector<double> &values)
{
    if (values.isEmpty())
        return 0.0;
    double total = 0.0;
    for (double value : values)
        total += value;
    return total / values.size();
}

double peakOf(const QVector<double> &values)
{
    return values.isEmpty()
        ? 0.0
        : *std::max_element(values.constBegin(), values.constEnd());
}

} // namespace

SystemMonitor::SystemMonitor(QWidget *parent)
    : QWidget(parent)
{
    // 作为 DesktopCanvas 的子控件存在：不创建独立顶层窗口，因此绝不会
    // 覆盖普通应用窗口，也无需依赖不同窗口管理器对 desktop hint 的实现。
    // 避免 WA_TranslucentBackground：在当前 UKUI/X11 组合里它会让 native
    // child window 变成不可见的黑/透明窗口。
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("ukui-fences-monitor"));

    m_cpuModel = readCpuModel();
    m_prevCpu = readCpuTotals();
    m_lastProcTotal = m_prevCpu.total;
    m_coreCount = std::max(1, m_prevCpu.coreTotal.size());
    m_corePercents = QVector<double>(std::min(m_coreCount, CORE_LIMIT), 0.0);
    m_cpuHistory.reserve(HISTORY_POINTS);m_memHistory.reserve(HISTORY_POINTS);m_diskHistory.reserve(HISTORY_POINTS);

    restoreSettings();
    m_liquidPointerEffect = new PointerEffect(this, [this](const QPoint &pos, bool inside) {
        if (m_skin != Skin::Liquid) return false;
        const QPointF pointer = inside ? QPointF(logicalPosition(pos)) : QPointF(-1000,-1000);
        if (pointer == m_liquidGlassPointer) return false;
        QPainterPath rim;
        const qreal inset=CohesiveGlass::DesktopInset/m_scale,radius=CohesiveGlass::CardRadius/m_scale;
        rim.addRoundedRect(QRectF(inset,inset,logicalWidth()-2*inset,logicalHeight()-2*inset),radius,radius);
        QRegion damage = pointerRimDamage(rim,m_liquidGlassPointer,112)
            | pointerRimDamage(rim,pointer,112);
        m_liquidGlassPointer = pointer;
        const QTransform scale = QTransform::fromScale(m_scale,m_scale);
        damage = scale.map(damage);
        if (!damage.isEmpty()) update(damage);
        return false;
    }, 16);
    m_liquidPointerEffect->setEnabled(m_skin == Skin::Liquid);
    refreshStats();

    m_timer.setTimerType(Qt::VeryCoarseTimer);
    m_timer.setInterval(statIntervalMs());
    connect(&m_timer, &QTimer::timeout,
            this, &SystemMonitor::refreshStats);
    if (auto *canvas = qobject_cast<DesktopCanvas *>(parent)) {
        connect(canvas, &DesktopCanvas::desktopCoveredChanged, this, [this](bool covered) {
            m_timer.setInterval(statIntervalMs());
            // Catch up at once when the desktop is uncovered, so stale numbers never show.
            if (!covered && isVisible()) refreshStats();
        });
    }
    QTimer::singleShot(500, this, &SystemMonitor::refreshStats);
    QTimer::singleShot(5500, this, [this] {
        if (isVisible() && !m_compact) {
            m_tick = 0;
            refreshStats();
        }
    });
}

SystemMonitor::~SystemMonitor()
{
    if (m_liquidPointerEffect) m_liquidPointerEffect->stop();
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this); process->kill();
    }
}

void SystemMonitor::setEditMode(bool edit)
{
    m_editMode = edit;
    m_dragging = false;
    setCursor(edit ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

SystemMonitor::CpuTotals SystemMonitor::readCpuTotals()
{
    CpuTotals totals;
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return totals;

    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isEmpty())
            break;
        const QByteArray line = rawLine.simplified();
        if (!line.startsWith("cpu"))
            break;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 5)
            continue;

        const quint64 user = parts.value(1).toULongLong();
        const quint64 nice = parts.value(2).toULongLong();
        const quint64 system = parts.value(3).toULongLong();
        const quint64 idle = parts.value(4).toULongLong();
        const quint64 iowait = parts.value(5).toULongLong();
        const quint64 irq = parts.value(6).toULongLong();
        const quint64 softirq = parts.value(7).toULongLong();
        const quint64 steal = parts.value(8).toULongLong();
        const quint64 total =
            user + nice + system + idle + iowait + irq + softirq + steal;
        const quint64 idleAll = idle + iowait;

        if (parts.first() == "cpu") {
            totals.total = total;
            totals.idle = idleAll;
            totals.iowait = iowait;
        } else {
            totals.coreTotal.append(total);
            totals.coreIdle.append(idleAll);
        }
    }
    return totals;
}

SystemMonitor::MemStats SystemMonitor::readMemStats()
{
    MemStats stats;
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return stats;

    while (true) {
        const QByteArray line = file.readLine();
        if (line.isEmpty())
            break;
        const int separator = line.indexOf(':');
        if (separator <= 0)
            continue;
        const QByteArray key = line.left(separator).trimmed();
        const QByteArray valuePart =
            line.mid(separator + 1).trimmed().split(' ').value(0);
        const quint64 value = valuePart.toULongLong();

        if (key == "MemTotal")
            stats.totalKiB = value;
        else if (key == "MemAvailable")
            stats.availableKiB = value;
        else if (key == "Cached" || key == "SReclaimable" ||
                 key == "Buffers")
            stats.cacheKiB += value;
        else if (key == "SwapTotal")
            stats.swapTotalKiB = value;
        else if (key == "SwapFree")
            stats.swapFreeKiB = value;
    }
    return stats;
}

QString SystemMonitor::readCpuModel()
{
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("处理器");

    QString hardware;
    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isEmpty())
            break;
        const int separator = rawLine.indexOf(':');
        if (separator <= 0)
            continue;
        const QByteArray key = rawLine.left(separator).trimmed();
        const QString value =
            QString::fromUtf8(rawLine.mid(separator + 1).trimmed());
        if ((key == "model name" || key == "Processor") && !value.isEmpty())
            return compactModelName(value);
        if (key == "Hardware" && !value.isEmpty())
            hardware = value;
    }
    return hardware.isEmpty() ? QStringLiteral("处理器")
                              : compactModelName(hardware);
}

QString SystemMonitor::platformDescription()
{
    // Describe the real machine instead of a hardcoded vendor. This keeps the
    // title accurate on every supported platform
    // (for example Hygon x86_64, Phytium ARM64, Zhaoxin, Kunpeng).
    const QString model = readCpuModel();
    const QString arch = QSysInfo::currentCpuArchitecture();
    if (model == QStringLiteral("处理器"))
        return QStringLiteral("%1 处理器").arg(arch.isEmpty() ? QStringLiteral("本机") : arch);
    return QStringLiteral("%1 · %2").arg(model, arch);
}

QString SystemMonitor::defaultWidgetTitle()
{
    return QStringLiteral("桌面资源监控");
}

SystemMonitor::Sample SystemMonitor::collectSample(bool processes, int count,
    quint64 previousTotal, QHash<QString, quint64> previousTicks, int targetPid, bool disk, bool framework)
{
    Sample sample;
    sample.cpu = readCpuTotals(); sample.memory = readMemStats();
    const int cores = qMax(1, sample.cpu.coreTotal.size());
    for (int i = 0; i < qMin(CORE_LIMIT, cores); ++i) sample.frequencies << readCoreFrequencyGhz(i);
    if (processes) {
        const quint64 delta = previousTotal && sample.cpu.total > previousTotal ? sample.cpu.total - previousTotal : 0;
        sample.processes = readProcessSnapshot(count, delta, previousTicks, cores);
        sample.ticks = previousTicks;
        if (framework) sample.framework=readFrameworkSnapshot(sample.processes);
    }
    if (targetPid > 0) sample.details = readProcessDetails(targetPid);
    sample.io = readDiskIoTotals(); sample.loads = readLoadAverages();
    if (disk) {
        struct statvfs stats {};
        if (statvfs(QDir::homePath().toLocal8Bit().constData(), &stats) == 0) {
            const double total = static_cast<double>(stats.f_blocks) * stats.f_frsize;
            const double available = static_cast<double>(stats.f_bavail) * stats.f_frsize;
            if (total > 0) {
                sample.diskTotal = total / 1073741824.0;
                sample.diskUsed = (total - available) / 1073741824.0;
                sample.diskPercent = clampPercent((total - available) * 100.0 / total);
            }
        }
    }
    return sample;
}

QVector<ProcessInfo> SystemMonitor::readProcessSnapshot(
    int count, quint64 totalDelta, QHash<QString, quint64> &previousTicks, int cores)
{
    QVector<ProcessInfo> processes;
    QHash<QString, quint64> currentTicks;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        bool pidOk = false;
        const int pid = entry.toInt(&pidOk);
        if (!pidOk || pid <= 0)
            continue;

        QFile file(QStringLiteral("/proc/") + entry + QStringLiteral("/stat"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        const QString stat = QString::fromUtf8(file.readAll());
        const int nameStart = stat.indexOf(QLatin1Char('('));
        const int nameEnd = stat.lastIndexOf(QLatin1Char(')'));
        if (nameStart < 0 || nameEnd <= nameStart)
            continue;

        const QStringList fields = splitBySpace(stat.mid(nameEnd + 2));
        if (fields.size() < 22 || fields[0] == "Z")
            continue;

        bool ok = false;
        const quint64 userTicks = fields[11].toULongLong(&ok);
        if (!ok)
            continue;
        const quint64 systemTicks = fields[12].toULongLong(&ok);
        if (!ok)
            continue;
        const quint64 startTicks = fields[19].toULongLong(&ok);
        if (!ok)
            continue;
        const qint64 rssPages = fields[21].toLongLong(&ok);
        if (!ok)
            continue;

        const quint64 ticks = userTicks + systemTicks;
        const QString identity=QString::number(pid)+":"+QString::number(startTicks);
        currentTicks.insert(identity, ticks);

        ProcessInfo info;
        info.pid = pid;
        info.uid=QFileInfo(QStringLiteral("/proc/")+entry).ownerId();
        info.state=fields[0];
        info.startTicks = startTicks;
        info.name = stat.mid(nameStart + 1, nameEnd - nameStart - 1);
        info.executablePath =
            QFileInfo(QStringLiteral("/proc/") + entry +
                      QStringLiteral("/exe")).symLinkTarget();
        info.rssMb = std::max<qint64>(0, rssPages) *
                     static_cast<double>(::getpagesize()) / 1048576.0;

        const quint64 previous = previousTicks.value(identity, ticks);
        if (totalDelta > 0 && previousTicks.contains(identity) && ticks >= previous) {
            // /proc/stat 的总量包含全部核心，因此这里乘核心数得到常见的
            // “单进程可到 100%/核”的显示语义。
            info.cpuPercent = qBound(0., (ticks - previous) * 100.0 * cores / totalDelta, cores*100.);
            info.cpuValid=true;
        }
        processes.append(info);
    }

    previousTicks = currentTicks;
    std::sort(processes.begin(), processes.end(),
              [](const ProcessInfo &a, const ProcessInfo &b) {
        if (a.cpuPercent != b.cpuPercent)
            return a.cpuPercent > b.cpuPercent;
        return a.rssMb > b.rssMb;
    });
    if (count > 0 && processes.size() > count)
        processes.resize(count);
    return processes;
}

QVector<SystemMonitor::Sample::FrameworkInfo> SystemMonitor::readFrameworkSnapshot(const QVector<ProcessInfo> &processes)
{
    // The embedded inventory is shared with integration/desktop_status.py.
    // Only matched main processes incur smaps reads; no second /proc scan.
    static const QJsonArray catalog=[] {
        QFile file(":/framework/components.json");
        if(!file.open(QIODevice::ReadOnly))return QJsonArray{};
        return QJsonDocument::fromJson(file.readAll()).object().value("components").toArray();
    }();
    QVector<Sample::FrameworkInfo> result;
    for(const auto &value:catalog) {
        const auto component=value.toObject(), config=component.value("monitor").toObject();
        if(config.isEmpty())continue;
        Sample::FrameworkInfo row;row.id=component.value("id").toString();row.name=config.value("label").toString();row.status=config.value("idle").toString();
        bool cpuValid=true,memoryValid=true;QStringList states;
        for(const auto &process:processes) {
            if(process.uid!=int(getuid()))continue;
            const auto executable=QFileInfo(process.executablePath).fileName();
            bool matches=component.value("executables").toArray().contains(executable);
            const auto suffix=component.value("scriptSuffix").toString();
            if(!matches&&!suffix.isEmpty()&&executable.startsWith("python")) {
                QFile args(QString("/proc/%1/cmdline").arg(process.pid));
                if(args.open(QIODevice::ReadOnly)) {
                    const auto fields=args.read(8192).split('\0');
                    matches=fields.size()>1&&QString::fromLocal8Bit(fields[1]).endsWith(suffix);
                }
            }
            if(!matches)continue;
            // Reject exits/reused PIDs between the common snapshot and memory read.
            const auto details=readProcessDetails(process.pid);
            if(!details.valid||details.startTicks!=process.startTicks)continue;
            row.pids<<process.pid;row.cpu+=process.cpuPercent;row.rss+=process.rssMb;
            cpuValid=cpuValid&&process.cpuValid;
            if(process.state=="T"||process.state=="t"){row.warning=true;states<<"已暂停";}
            else if(process.state=="D"){row.warning=true;states<<"I/O 等待";}
            if(component.value("id").toString()=="panel") {
                QFile maps(QString("/proc/%1/maps").arg(process.pid));
                if(!maps.open(QIODevice::ReadOnly)||!maps.readAll().contains("libukuiliquidpanel.so")) {
                    row.warning=true;states<<"原生样式";
                }
            }
            QFile memory(QString("/proc/%1/smaps_rollup").arg(process.pid));
            bool hasPss=false,hasSwap=false;
            if(memory.open(QIODevice::ReadOnly)) {
                for(const auto &line:memory.readAll().split('\n')) {
                    const auto fields=line.simplified().split(' ');
                    if(fields.size()<2)continue;
                    if(fields[0]=="Pss:"){row.pss+=fields[1].toDouble()/1024.;hasPss=true;}
                    if(fields[0]=="SwapPss:"){row.swap+=fields[1].toDouble()/1024.;hasSwap=true;}
                }
            }
            memoryValid=memoryValid&&hasPss&&hasSwap;
        }
        if(!row.pids.isEmpty()) {
            states.removeDuplicates();row.status=states.isEmpty()?QStringLiteral("运行"):states.join(" / ");
            row.cpuValid=cpuValid;row.memoryValid=memoryValid;
        }
        result<<row;
    }
    return result;
}

SystemMonitor::ProcessDetails SystemMonitor::readProcessDetails(int pid)
{
    ProcessDetails details;
    if (pid <= 0)
        return details;

    const QString procBase = QStringLiteral("/proc/%1/").arg(pid);
    QFile statFile(procBase + QStringLiteral("stat"));
    if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return details;
    const QString stat = QString::fromUtf8(statFile.readAll());
    const int nameStart = stat.indexOf(QLatin1Char('('));
    const int nameEnd = stat.lastIndexOf(QLatin1Char(')'));
    if (nameStart < 0 || nameEnd <= nameStart)
        return details;
    const QStringList statFields = splitBySpace(stat.mid(nameEnd + 2));
    if (statFields.size() < 22)
        return details;
    bool ok = false;
    details.startTicks = statFields[19].toULongLong(&ok);
    if (!ok)
        return details;
    details.name = stat.mid(nameStart + 1, nameEnd - nameStart - 1);

    QFile statusFile(procBase + QStringLiteral("status"));
    if (!statusFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return details;
    while (!statusFile.atEnd()) {
        const QString line = QString::fromUtf8(statusFile.readLine()).trimmed();
        const int separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0)
            continue;
        const QString key = line.left(separator);
        const QString value = line.mid(separator + 1).trimmed();
        const QString firstValue = value.section(QLatin1Char(' '), 0, 0);
        if (key == QLatin1String("Name"))
            details.name = value;
        else if (key == QLatin1String("State"))
            details.state = value;
        else if (key == QLatin1String("PPid"))
            details.parentPid = firstValue.toInt();
        else if (key == QLatin1String("Threads"))
            details.threadCount = firstValue.toInt();
        else if (key == QLatin1String("Uid"))
            details.uid = firstValue.toInt();
        else if (key == QLatin1String("VmRSS"))
            details.rssMb = firstValue.toDouble() / 1024.0;
        else if (key == QLatin1String("VmSize"))
            details.virtualMemoryMb = firstValue.toDouble() / 1024.0;
        else if (key == QLatin1String("voluntary_ctxt_switches"))
            details.voluntaryContextSwitches = firstValue.toULongLong();
        else if (key == QLatin1String("nonvoluntary_ctxt_switches"))
            details.involuntaryContextSwitches = firstValue.toULongLong();
    }

    const QString executable = QFileInfo(
        procBase + QStringLiteral("exe")).symLinkTarget();
    details.executableName = QFileInfo(executable).fileName();
    if (details.executableName.isEmpty())
        details.executableName = details.name;

    const QDir fdDirectory(procBase + QStringLiteral("fd"));
    if (fdDirectory.isReadable()) {
        details.fileDescriptorCount = fdDirectory.entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot).size();
    }

    QFile ioFile(procBase + QStringLiteral("io"));
    if (ioFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!ioFile.atEnd()) {
            const QString line = QString::fromUtf8(ioFile.readLine()).trimmed();
            const int separator = line.indexOf(QLatin1Char(':'));
            if (separator <= 0)
                continue;
            const QString key = line.left(separator);
            const quint64 value = line.mid(separator + 1).trimmed().toULongLong();
            if (key == QLatin1String("read_bytes")) {
                details.readBytes = value;
                details.hasIoCounters = true;
            } else if (key == QLatin1String("write_bytes")) {
                details.writeBytes = value;
                details.hasIoCounters = true;
            }
        }
    }
    details.valid = true;
    return details;
}

SystemMonitor::DiskIoTotals SystemMonitor::readDiskIoTotals()
{
    DiskIoTotals totals;
    QFile file(QStringLiteral("/proc/diskstats"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return totals;

    static const QRegularExpression devicePattern(
        QStringLiteral("^(sd[a-z]+|vd[a-z]+|xvd[a-z]+|nvme\\d+n\\d+|"
                       "mmcblk\\d+)$"));
    while (!file.atEnd()) {
        const QList<QByteArray> fields = file.readLine().simplified().split(' ');
        if (fields.size() < 14)
            continue;
        const QString device = QString::fromLatin1(fields.value(2));
        if (!devicePattern.match(device).hasMatch())
            continue;
        totals.readSectors += fields.value(5).toULongLong();
        totals.writeSectors += fields.value(9).toULongLong();
    }
    return totals;
}

QVector<double> SystemMonitor::readLoadAverages()
{
    QFile file(QStringLiteral("/proc/loadavg"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QList<QByteArray> fields = file.readLine().simplified().split(' ');
    if (fields.size() < 3)
        return {};
    return { fields[0].toDouble(), fields[1].toDouble(),
             fields[2].toDouble() };
}

void SystemMonitor::appendHistory(QVector<double> &history, double value)
{
    if (history.size() >= HISTORY_POINTS)
        history.removeFirst();
    history.append(clampPercent(value));
}

int SystemMonitor::statIntervalMs() const
{
    // Nobody reads the gauges behind a maximized window; keep a slow sample so
    // history stays continuous without five-second wake-ups.
    const auto *canvas = qobject_cast<const DesktopCanvas *>(parentWidget());
    const int seconds = canvas && canvas->desktopCovered()
        ? qMax(COVERED_STAT_INTERVAL_SEC, m_statIntervalSec) : m_statIntervalSec;
    return seconds * 1000;
}

void SystemMonitor::refreshStats()
{
    if (m_statsPending || (m_tick > 0 && !isVisible())) return;
    m_statsPending = true;
    const bool heavy = m_tick == 0 || m_tick % HEAVY_EVERY_TICKS == 0;
    const bool processes = !m_compact;
    const auto total = m_lastProcTotal;
    const auto ticks = m_prevProcTicks;
    const bool framework=m_showFramework;
    BackgroundTask::run(this, [=] { return collectSample(processes, 0, total, ticks, 0, heavy, framework); },
        [this, heavy](const Sample &sample) {
            m_statsPending = false;
            if ((m_tick > 0 && !isVisible())) return;
            applyStats(sample, heavy);
        });
}

void SystemMonitor::applyStats(const Sample &sample, bool heavy)
{
    const CpuTotals current = sample.cpu;
    const quint64 totalDelta =
        current.total > m_prevCpu.total ? current.total - m_prevCpu.total : 0;
    const quint64 idleDelta =
        current.idle > m_prevCpu.idle ? current.idle - m_prevCpu.idle : 0;

    const quint64 minimumUsefulDelta =
        static_cast<quint64>(std::max(1, m_coreCount)) * 5;
    if (totalDelta >= minimumUsefulDelta)
        m_cpuPercent =
            clampPercent((totalDelta - idleDelta) * 100.0 / totalDelta);

    const int coreCount = std::min(current.coreTotal.size(), CORE_LIMIT);
    if (m_corePercents.size() != coreCount)
        m_corePercents = QVector<double>(coreCount, 0.0);
    for (int i = 0; i < coreCount; ++i) {
        const quint64 coreDelta =
            current.coreTotal[i] > m_prevCpu.coreTotal.value(i)
            ? current.coreTotal[i] - m_prevCpu.coreTotal.value(i) : 0;
        const quint64 coreIdleDelta =
            current.coreIdle.value(i) > m_prevCpu.coreIdle.value(i)
            ? current.coreIdle.value(i) - m_prevCpu.coreIdle.value(i) : 0;
        if (coreDelta >= 2) {
            m_corePercents[i] = clampPercent(
                (coreDelta - coreIdleDelta) * 100.0 / coreDelta);
        }
    }
    m_prevCpu = current;
    m_coreCount = current.coreTotal.isEmpty()
        ? std::max(1, QThread::idealThreadCount())
        : current.coreTotal.size();
    m_coreFrequenciesGhz.resize(std::min(CORE_LIMIT, m_coreCount));
    for (int i = 0; i < m_coreFrequenciesGhz.size(); ++i)
        m_coreFrequenciesGhz[i] = sample.frequencies.value(i);

    const MemStats memory = sample.memory;
    if (memory.totalKiB > 0) {
        const quint64 usedKiB = memory.totalKiB - memory.availableKiB;
        m_memPercent =
            clampPercent(usedKiB * 100.0 / memory.totalKiB);
        m_memUsedGb = usedKiB / 1048576.0;
        m_memTotalGb = memory.totalKiB / 1048576.0;
        m_memCacheGb = memory.cacheKiB / 1048576.0;
    }

    if (!m_compact) {
        m_topProcs=sample.processes;m_prevProcTicks=sample.ticks;m_framework=sample.framework;
        if(m_sortByMemory)std::sort(m_topProcs.begin(),m_topProcs.end(),[](const ProcessInfo &a,const ProcessInfo &b){return a.rssMb>b.rssMb;});
        if(m_topProcs.size()>5)m_topProcs.resize(5);
        m_lastProcTotal=current.total;
    }
    const qint64 elapsed=m_sampleClock.isValid()?m_sampleClock.restart():0;
    if(!m_sampleClock.isValid())m_sampleClock.start();
    if(elapsed>0&&elapsed<=qMax(120000,statIntervalMs()*3)) {
        m_diskReadMb=sample.io.readSectors>=m_previousDiskIo.readSectors?(sample.io.readSectors-m_previousDiskIo.readSectors)*512./1048576.*1000./elapsed:0;
        m_diskWriteMb=sample.io.writeSectors>=m_previousDiskIo.writeSectors?(sample.io.writeSectors-m_previousDiskIo.writeSectors)*512./1048576.*1000./elapsed:0;
    } else m_diskReadMb=m_diskWriteMb=0;
    m_previousDiskIo=sample.io;m_loads=sample.loads;m_lastUpdated=QDateTime::currentDateTime();
    m_swapUsedGb=(sample.memory.swapTotalKiB-qMin(sample.memory.swapTotalKiB,sample.memory.swapFreeKiB))/1048576.;
    if (heavy) {
        if (sample.diskTotal > 0) {
            m_diskTotalGb = sample.diskTotal; m_diskUsedGb = sample.diskUsed; m_diskPercent = sample.diskPercent;
        }
    }
    ++m_tick;

    appendHistory(m_cpuHistory, m_cpuPercent);
    appendHistory(m_memHistory, m_memPercent);
    appendHistory(m_diskHistory, m_diskPercent);
    update();
}

double SystemMonitor::readCoreFrequencyGhz(int core)
{
    const QString base = QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/")
                             .arg(core);
    const QStringList files = {
        base + QStringLiteral("scaling_cur_freq"),
        base + QStringLiteral("cpuinfo_cur_freq"),
        base + QStringLiteral("scaling_max_freq")
    };
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        bool ok = false;
        const double khz = file.readAll().trimmed().toDouble(&ok);
        if (ok && khz > 0.0)
            return khz / 1000000.0;
    }
    return 0.0;
}

SystemMonitor::Palette SystemMonitor::paletteForSkin() const
{
    Palette palette;
    switch (m_skin) {
    case Skin::Light:
        palette = { QColor(248, 250, 252, 242), QColor(255, 255, 255, 220),
                    QColor(100, 116, 139, 55), QColor(30, 41, 59),
                    QColor(100, 116, 139), QColor(79, 70, 229),
                    QColor(5, 150, 105), QColor(217, 119, 6),
                    QColor(100, 116, 139, 35) };
        break;
    case Skin::Cyber:
        palette = { QColor(10, 10, 22, 235), QColor(18, 18, 44, 225),
                    QColor(0, 240, 255, 95), QColor(235, 250, 255),
                    QColor(138, 167, 181), QColor(0, 240, 255),
                    QColor(255, 0, 127), QColor(250, 204, 21),
                    QColor(0, 240, 255, 28) };
        break;
    case Skin::Glass:
        palette = { QColor(22, 29, 43, 175), QColor(30, 41, 59, 150),
                    QColor(255, 255, 255, 62), QColor(241, 245, 249),
                    QColor(186, 200, 220), QColor(129, 140, 248),
                    QColor(52, 211, 153), QColor(251, 191, 36),
                    QColor(255, 255, 255, 28) };
        break;
    case Skin::Wallpaper: {
        QColor accent = m_wallpaperThemeColor.isValid()
            ? m_wallpaperThemeColor : QColor(79, 70, 229);
        const int luminance =
            (accent.red() * 299 + accent.green() * 587 +
             accent.blue() * 114) / 1000;
        QColor panel = accent.darker(luminance > 145 ? 210 : 155);
        QColor card = accent.darker(luminance > 145 ? 165 : 125);
        palette = { QColor(panel.red(), panel.green(), panel.blue(), 232),
                    QColor(card.red(), card.green(), card.blue(), 205),
                    QColor(accent.red(), accent.green(), accent.blue(), 105),
                    QColor(248, 250, 252), QColor(203, 213, 225),
                    accent.lighter(145), accent.lighter(120),
                    QColor(251, 191, 36),
                    QColor(accent.red(), accent.green(), accent.blue(), 38) };
        break;
    }
    case Skin::Liquid:
        // The Snell material is sampled once from the desktop wallpaper;
        // these light cards preserve contrast for live metrics and labels.
        palette = { QColor(14, 22, 34, 82), QColor(23, 31, 43, 68),
                    QColor(232, 244, 255, 46), QColor(250, 252, 255),
                    QColor(206, 219, 234), QColor(139, 190, 255),
                    QColor(101, 226, 176), QColor(255, 207, 120),
                    QColor(235, 246, 255, 24) };
        break;
    case Skin::Dark:
    default:
        palette = { QColor(15, 23, 42, 235), QColor(2, 6, 23, 118),
                    QColor(148, 163, 184, 45), QColor(241, 245, 249),
                    QColor(148, 163, 184), QColor(99, 102, 241),
                    QColor(16, 185, 129), QColor(245, 158, 11),
                    QColor(148, 163, 184, 28) };
        break;
    }

    const auto applyOpacity = [this](QColor color) {
        color.setAlpha(qRound(color.alpha() * m_windowOpacity / 100.0));
        return color;
    };
    palette.panel = applyOpacity(palette.panel);
    palette.card = applyOpacity(palette.card);
    palette.border = applyOpacity(palette.border);
    palette.track = applyOpacity(palette.track);
    return palette;
}

QColor SystemMonitor::wallpaperSampleColor() const
{
    const auto *canvas = qobject_cast<const DesktopCanvas *>(parentWidget());
    if (!canvas)
        return QColor();
    return canvas->wallpaperColorAt(geometry().adjusted(-40, -40, 40, 40));
}

void SystemMonitor::refreshWallpaperTheme()
{
    m_liquidGlassImage = {};
    m_liquidGlassGeometry = {};
    const QColor sampled = wallpaperSampleColor();
    if (sampled.isValid())
        m_wallpaperThemeColor = sampled;
    if (m_skin == Skin::Wallpaper)
        update();
    emit appearanceChanged();
}

void SystemMonitor::drawRing(QPainter &p, const QRectF &rect,
                             double percent, const QColor &accent,
                             const QColor &track)
{
    QPen pen(track, 5, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(rect, 0, 360 * 16);

    pen.setColor(accent);
    p.setPen(pen);
    p.drawArc(rect, 90 * 16,
              -qRound(360.0 * clampPercent(percent) / 100.0 * 16.0));
}

void SystemMonitor::drawSparkline(QPainter &p, const QRectF &rect,
                                  const QVector<double> &history,
                                  const QColor &accent)
{
    if (history.size() < 2)
        return;

    QPainterPath path;
    const qreal step = rect.width() / (history.size() - 1);
    for (int i = 0; i < history.size(); ++i) {
        const QPointF point(rect.left() + i * step,
            rect.bottom() - rect.height() * clampPercent(history[i]) / 100.0);
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    p.setPen(QPen(accent, 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void SystemMonitor::drawMetricCard(QPainter &p, const QRect &rect,
                                   const QString &title,
                                   const QString &detail,
                                   double percent,
                                   const QColor &accent,
                                   const QVector<double> &history)
{
    const Palette colors = paletteForSkin();
    if (m_premiumAesthetics) {
        QLinearGradient grad(rect.topLeft(), rect.bottomRight());
        QColor cStart = colors.card;
        cStart.setAlpha(qMin(255, cStart.alpha() + 25));
        QColor cEnd = colors.card;
        cEnd.setAlpha(qMax(0, cEnd.alpha() - 20));
        grad.setColorAt(0, cStart);
        grad.setColorAt(1, cEnd);
        p.setBrush(grad);
        p.setPen(QPen(colors.border, 1));
        p.drawRoundedRect(rect, 12, 12);
        
        // Glare highlight line
        p.setPen(QPen(QColor(255, 255, 255, 45), 1));
        p.drawLine(rect.left() + 10, rect.top() + 1, rect.right() - 10, rect.top() + 1);
    } else {
        p.setPen(QPen(colors.border, 1));
        p.setBrush(colors.card);
        p.drawRoundedRect(rect, 10, 10);
    }

    QFont titleFont = uiFont(2, true);
    p.setFont(titleFont);
    p.setPen(colors.muted);
    p.drawText(rect.adjusted(10, 8, -8, 0),
               Qt::AlignLeft | Qt::AlignTop, title);

    const QRect ringRect(rect.left() + 12, rect.top() + 38, 66, 66);
    drawRing(p, ringRect, percent, accent, colors.track);

    QFont valueFont = uiFont(7, true);
    p.setFont(valueFont);
    p.setPen(colors.text);
    p.drawText(ringRect, Qt::AlignCenter,
               QStringLiteral("%1%").arg(percent, 0, 'f', 0));

    QFont detailFont = uiFont(1);
    p.setFont(detailFont);
    p.setPen(colors.muted);
    const QRect detailRect(ringRect.right() + 12, ringRect.top(),
                           rect.right() - ringRect.right() - 22, 42);
    p.drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter |
               Qt::TextWordWrap, detail);

    drawSparkline(p, QRectF(ringRect.right() + 12, ringRect.bottom() - 9,
                            rect.right() - ringRect.right() - 22, 13),
                  history, accent);
}

void SystemMonitor::drawButton(QPainter &p, const QRect &rect,
                               const QString &text,
                               const QColor &foreground,
                               const QColor &background,
                               const QColor &border)
{
    p.setPen(QPen(border, 1));
    p.setBrush(background);
    p.drawRoundedRect(rect, 7, 7);
    QFont buttonFont = uiFont(0, true);
    p.setFont(buttonFont);
    p.setPen(foreground);
    p.drawText(rect, Qt::AlignCenter, text);
}

QSize SystemMonitor::baseSize() const
{
    return m_compact ? QSize(COMPACT_W, COMPACT_H)
                     : QSize(EXPANDED_W, EXPANDED_H);
}

QFont SystemMonitor::uiFont(int sizeOffset, bool bold) const
{
    QFont result = font();
    if (!m_widgetFontFamily.isEmpty())
        result.setFamily(m_widgetFontFamily);
    // 整个部件按 m_scale 线性缩放，字号必须先放大再缩小，
    // 否则缩到 0.65 时正文不足 6 像素。渲染后至少 9 像素（中文可读下限）。
    const int floor = int(std::ceil(9.0 / qMax(m_scale, 0.35)));
    result.setPixelSize(qBound(7, std::max(m_widgetFontSize + sizeOffset, floor), 28));
    result.setBold(bold);
    return result;
}

int SystemMonitor::logicalWidth() const
{
    return baseSize().width();
}

int SystemMonitor::logicalHeight() const
{
    return baseSize().height();
}

QPoint SystemMonitor::logicalPosition(const QPoint &position) const
{
    return QPoint(qRound(position.x() / m_scale),
                  qRound(position.y() / m_scale));
}

void SystemMonitor::updateHitRects()
{
    const int y = 12;
    const int h = 30;
    m_settingsRect = QRect(logicalWidth() - 46, y, 30, h);
    m_skinRect = QRect(m_settingsRect.left() - 38, y, 30, h);
    m_toggleRect = QRect(m_skinRect.left() - 92, y, 84, h);
    m_optimizeRect = QRect(m_toggleRect.left() - 98, y, 90, h);

    if(!m_compact) {
        const int leftWidth=qRound((logicalWidth()-MARGIN*2-12)*.45);
        m_overviewRect=QRect(MARGIN+leftWidth+12,236,logicalWidth()-MARGIN*2-leftWidth-12,logicalHeight()-252);
        const int half=(m_overviewRect.width()-36)/2;
        m_copyRect=QRect(m_overviewRect.left()+12,m_overviewRect.bottom()-31,half,24);
        m_detailsRect=QRect(m_copyRect.right()+13,m_copyRect.top(),half,24);
    } else {m_overviewRect={};m_copyRect={};m_detailsRect={};}
    m_resizeHandleRect =
        QRect(logicalWidth() - 22, logicalHeight() - 22, 18, 18);
}

void SystemMonitor::drawHeader(QPainter &p, const Palette &colors)
{
    updateHitRects();
    if(m_skin!=Skin::Liquid) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,qRound((m_skin==Skin::Light?10:35)*m_windowOpacity/100.0)));
        p.drawRoundedRect(QRect(1,1,logicalWidth()-2,HEADER_H),15,15);
    }

    p.setBrush(QColor(16, 185, 129));
    p.drawEllipse(QPoint(MARGIN + 6, HEADER_H / 2), 5, 5);

    QFont titleFont = uiFont(3, true);
    p.setFont(titleFont);
    p.setPen(colors.text);
    p.drawText(QRect(MARGIN + 22, 0, m_optimizeRect.left() - MARGIN - 30,
                     HEADER_H),
               Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(titleFont).elidedText(
                   m_widgetTitle, Qt::ElideRight,
                   m_optimizeRect.left() - MARGIN - 34));

    drawButton(p, m_optimizeRect, QStringLiteral("⚡ 立即刷新"),
               colors.cpu, QColor(colors.cpu.red(), colors.cpu.green(),
                                  colors.cpu.blue(), 25),
               QColor(colors.cpu.red(), colors.cpu.green(),
                      colors.cpu.blue(), 70));
    drawButton(p, m_toggleRect,
               m_compact ? QStringLiteral("展开视图")
                         : QStringLiteral("紧凑模式"),
               colors.text, colors.card, colors.border);
    drawButton(p, m_skinRect, QStringLiteral("◈"),
               colors.text, colors.card, colors.border);
    drawButton(p, m_settingsRect, QStringLiteral("⚙"),
               colors.cpu, QColor(colors.cpu.red(), colors.cpu.green(),
                                  colors.cpu.blue(), 25),
               QColor(colors.cpu.red(), colors.cpu.green(),
                      colors.cpu.blue(), 70));

    if (m_editMode) {
        p.setPen(QPen(colors.cpu, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(
            QRect(2, 2, logicalWidth() - 4, logicalHeight() - 4),
            12, 12);
    }
}

void SystemMonitor::drawProcessPanel(QPainter &p, const QRect &rect,
                                     const Palette &colors)
{
    if (m_premiumAesthetics) {
        QLinearGradient grad(rect.topLeft(), rect.bottomRight());
        QColor cStart = colors.card;
        cStart.setAlpha(qMin(255, cStart.alpha() + 25));
        QColor cEnd = colors.card;
        cEnd.setAlpha(qMax(0, cEnd.alpha() - 20));
        grad.setColorAt(0, cStart);
        grad.setColorAt(1, cEnd);
        p.setBrush(grad);
        p.setPen(QPen(colors.border, 1));
        p.drawRoundedRect(rect, 12, 12);
        
        // Glare highlight line
        p.setPen(QPen(QColor(255, 255, 255, 45), 1));
        p.drawLine(rect.left() + 10, rect.top() + 1, rect.right() - 10, rect.top() + 1);
    } else {
        p.setPen(QPen(colors.border, 1));
        p.setBrush(colors.card);
        p.drawRoundedRect(rect, 10, 10);
    }

    QFont heading = uiFont(2, true);
    p.setFont(heading);
    p.setPen(colors.text);
    p.drawText(rect.adjusted(14, 12, -12, 0),
               Qt::AlignLeft | Qt::AlignTop,
               m_sortByMemory?QStringLiteral("内存占用 · TOP 5"):QStringLiteral("CPU 占用 · TOP 5"));

    QFont rowFont = uiFont();
    p.setFont(rowFont);

    p.setPen(colors.muted);
    p.drawText(QRect(rect.right() - 160, rect.top() + 9, 52, 18),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("PID"));
    p.drawText(QRect(rect.right() - 101, rect.top() + 9, 42, 18),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("CPU"));
    p.drawText(QRect(rect.right() - 50, rect.top() + 9, 38, 18),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("RSS"));

    m_processRowRects.clear();
    int y = rect.top() + 38;
    for (int i = 0; i < m_topProcs.size() && i < 5; ++i) {
        const ProcessInfo &process = m_topProcs[i];
        if (i > 0) {
            p.setPen(QPen(colors.border, 1));
            p.drawLine(rect.left() + 12, y - 3, rect.right() - 12, y - 3);
        }

        p.setPen(colors.text);
        m_processRowRects.append(
            QRect(rect.left() + 8, y - 2, rect.width() - 16, 22));
        const QString name = QFontMetrics(rowFont).elidedText(
            process.name, Qt::ElideRight, rect.width() - 190);
        p.drawText(QRect(rect.left() + 12, y, rect.width() - 190, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, name);

        p.setPen(colors.muted);
        p.drawText(QRect(rect.right() - 172, y, 48, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(process.pid));
        p.setPen(colors.cpu);
        p.drawText(QRect(rect.right() - 116, y, 48, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   process.cpuValid?QStringLiteral("%1%").arg(process.cpuPercent, 0, 'f', 1):QStringLiteral("—"));
        p.setPen(colors.memory);
        p.drawText(QRect(rect.right() - 61, y, 49, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   process.rssMb >= 1024.0
                       ? QStringLiteral("%1G").arg(process.rssMb / 1024.0, 0, 'f', 1)
                       : QStringLiteral("%1M").arg(process.rssMb, 0, 'f', 0));
        y += 28;
    }
    const int footerY=rect.bottom()-43;
    p.setFont(uiFont());p.setPen(colors.muted);
    p.drawText(QRect(rect.left()+12,footerY,rect.width()-24,18),Qt::AlignLeft|Qt::AlignVCenter,
        QString("负载 %1 / %2 核   ·   交换 %3 GB").arg(m_loads.value(0),0,'f',2).arg(m_coreCount).arg(m_swapUsedGb,0,'f',2));
    p.drawText(QRect(rect.left()+12,footerY+19,rect.width()-24,18),Qt::AlignLeft|Qt::AlignVCenter,
        QString("读 %1 · 写 %2 MB/s   ·   %3").arg(m_diskReadMb,0,'f',1).arg(m_diskWriteMb,0,'f',1).arg(m_lastUpdated.toString("HH:mm:ss")));
}

void SystemMonitor::drawOverviewPanel(QPainter &p,const QRect &rect,const Palette &colors)
{
    p.setPen(QPen(colors.border,1));p.setBrush(colors.card);p.drawRoundedRect(rect,10,10);
    p.setFont(uiFont(2,true));p.setPen(colors.text);
    p.drawText(rect.adjusted(14,11,-14,0),Qt::AlignLeft|Qt::AlignTop,
        m_showFramework?QStringLiteral("Kylin-LQUI"):QStringLiteral("资源概况"));
    m_frameworkRowRects.clear();
    if(m_showFramework) {
        p.setFont(uiFont());p.setPen(colors.muted);
        p.drawText(QRect(rect.right()-142,rect.top()+12,60,18),Qt::AlignRight|Qt::AlignVCenter,"CPU");
        p.drawText(QRect(rect.right()-77,rect.top()+12,64,18),Qt::AlignRight|Qt::AlignVCenter,"PSS MiB");
        const int available=qMax(0,m_copyRect.top()-rect.top()-40);
        const int visibleRows=qMin(m_framework.size(),available/16);
        const int rowHeight=visibleRows?qMin(20,available/visibleRows):16;
        for(int i=0;i<visibleRows;++i) {
            const auto &item=m_framework[i];const int y=rect.top()+36+i*rowHeight;
            const QRect row(rect.left()+10,y,rect.width()-20,rowHeight);m_frameworkRowRects<<row;
            p.setPen(Qt::NoPen);p.setBrush(item.pids.isEmpty()?colors.muted:item.warning?QColor(239,177,95):colors.cpu);
            p.drawEllipse(QRectF(row.left()+1,y+(rowHeight-5)/2,5,5));
            p.setPen(colors.text);
            p.drawText(QRect(row.left()+13,y,rect.width()-265,rowHeight),Qt::AlignLeft|Qt::AlignVCenter,
                QFontMetrics(p.font()).elidedText(item.name,Qt::ElideRight,rect.width()-265));
            p.setPen(item.warning?QColor(239,177,95):colors.muted);
            p.drawText(QRect(rect.right()-237,y,88,rowHeight),Qt::AlignRight|Qt::AlignVCenter,
                QFontMetrics(p.font()).elidedText(item.status,Qt::ElideRight,88));
            p.setPen(colors.cpu);
            p.drawText(QRect(rect.right()-142,y,60,rowHeight),Qt::AlignRight|Qt::AlignVCenter,item.cpuValid?QString::number(item.cpu,'f',1)+"%":"—");
            p.setPen(colors.memory);
            p.drawText(QRect(rect.right()-77,y,64,rowHeight),Qt::AlignRight|Qt::AlignVCenter,item.memoryValid?QString::number(item.pss,'f',1):"—");
        }
        if(m_framework.isEmpty()) {p.setPen(colors.muted);p.drawText(rect.adjusted(14,40,-14,-40),"采样中…");}
        drawButton(p,m_copyRect,"复制快照",colors.text,colors.card,colors.border);
        drawButton(p,m_detailsRect,"查看详情",colors.cpu,colors.card,colors.border);
        return;
    }
    const QList<QPair<QString,QString>> rows{
        {"CPU 采样峰值",QString::number(peakOf(m_cpuHistory),'f',1)+"%"},
        {"内存采样峰值",QString::number(peakOf(m_memHistory),'f',1)+"%"},
        {"负载 · 1 分钟",QString::number(m_loads.value(0),'f',2)+QString(" / %1 核").arg(m_coreCount)},
        {"交换内存",QString::number(m_swapUsedGb,'f',2)+" GB"},
        {"磁盘读 / 写",QString("%1 / %2 MB/s").arg(m_diskReadMb,0,'f',1).arg(m_diskWriteMb,0,'f',1)},
        {"最近采样",m_lastUpdated.isValid()?m_lastUpdated.toString("HH:mm:ss"):QStringLiteral("采样中…")}
    };
    p.setFont(uiFont());
    for(int i=0;i<rows.size();++i){const QRect row=rect.adjusted(14,34+i*20,-14,0);p.setPen(colors.muted);p.drawText(QRect(row.left(),row.top(),row.width(),18),Qt::AlignLeft|Qt::AlignVCenter,rows[i].first);p.setPen(colors.text);p.drawText(QRect(row.left(),row.top(),row.width(),18),Qt::AlignRight|Qt::AlignVCenter,rows[i].second);}
    drawButton(p,m_copyRect,"复制快照",colors.text,colors.card,colors.border);
    drawButton(p,m_detailsRect,"查看详情",colors.cpu,colors.card,colors.border);
}

void SystemMonitor::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(m_scale, m_scale);
    const Palette colors = paletteForSkin();

    if (m_skin == Skin::Liquid) {
        if (m_liquidGlassImage.isNull() || m_liquidGlassGeometry != geometry()) {
            if (auto *canvas = qobject_cast<DesktopCanvas *>(parentWidget())) {
                const qreal inset=CohesiveGlass::DesktopInset;
                const auto outline=GlassFinish::squirclePath(QRectF(rect()).adjusted(inset,inset,-inset,-inset),CohesiveGlass::CardRadius);
                m_liquidGlassImage=GlassFinish::withEdge(GlassFinish::withScrim(canvas->renderLiquidGlass(geometry(),CohesiveGlass::CardRadius,outline),GlassFinish::scrim()),outline);
                QVector<QColor> cells;const auto sample=m_liquidGlassImage.scaled(12,8,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
                for(int y=1;y<sample.height()-1;++y)for(int x=1;x<sample.width()-1;++x)if(sample.pixelColor(x,y).alpha()>200)cells.append(sample.pixelColor(x,y));
                const qreal lift=::Palette::toneLift(cells,::Palette::SecondaryTextContrast+.05,Qt::white);
                if(lift>0)m_liquidGlassImage=LiquidMaterial::bodyTone(m_liquidGlassImage,qMin(1.,lift+.004),outline);
                m_liquidGlassGeometry = geometry();
                if (!m_liquidGlassImage.isNull())
                    VideoWallpaperRegion::publish(
                        this, VideoWallpaperRegion::coverage(m_liquidGlassImage, devicePixelRatioF()));
            }
        }
        if (!m_liquidGlassImage.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(0, 0, logicalWidth(), logicalHeight()),
                              m_liquidGlassImage);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.panel);
            painter.drawRoundedRect(QRect(0, 0, logicalWidth(), logicalHeight()), 14, 14);
        }

        const QPointF pointer = m_liquidGlassPointer;
        if (m_hovered && pointer.x() >= 0 && pointer.y() >= 0 &&
            pointer.x() < logicalWidth() && pointer.y() < logicalHeight()) {
            QPainterPath rim;
            const qreal inset=CohesiveGlass::DesktopInset/m_scale,radius=CohesiveGlass::CardRadius/m_scale;
            rim.addRoundedRect(QRectF(inset,inset,logicalWidth()-2*inset,logicalHeight()-2*inset),radius,radius);
            QRadialGradient reflection(pointer, 112);
            reflection.setColorAt(0, QColor(233, 247, 255, 66));
            reflection.setColorAt(0.38, QColor(193, 225, 255, 24));
            reflection.setColorAt(1, Qt::transparent);
            painter.save();
            painter.setClipPath(rim, Qt::IntersectClip);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QBrush(reflection), 1.6));
            painter.drawPath(rim);
            reflection.setColorAt(0, QColor(208, 234, 255, 22));
            reflection.setColorAt(0.45, QColor(193, 225, 255, 8));
            painter.setPen(QPen(QBrush(reflection), 8));
            painter.drawPath(rim);
            painter.restore();
        }
    } else if (m_premiumAesthetics) {
        // Draw main panel with double border & soft glow
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.panel);
        painter.drawRoundedRect(QRect(0, 0, logicalWidth(), logicalHeight()), 14, 14);

        // Soft outer glow using accent color (CPU color)
        QColor glowColor = colors.cpu;
        glowColor.setAlpha(25);
        painter.setPen(QPen(glowColor, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRect(1, 1, logicalWidth() - 2, logicalHeight() - 2), 13, 13);

        // Crisp inner border
        painter.setPen(QPen(colors.border, 1));
        painter.drawRoundedRect(QRect(0, 0, logicalWidth() - 1, logicalHeight() - 1), 14, 14);
    } else {
        painter.setPen(QPen(colors.border, 1));
        painter.setBrush(colors.panel);
        painter.drawRoundedRect(
            QRect(0, 0, logicalWidth() - 1, logicalHeight() - 1), 14, 14);
    }
    drawHeader(painter, colors);

    const int cardY = HEADER_H + 14;
    const int cardGap = 12;
    const int cardWidth =
        (logicalWidth() - MARGIN * 2 - cardGap * 2) / 3;
    const int cardHeight = 156;

    const QString cpuDetail =
        QStringLiteral("%1\n%2 核心").arg(m_cpuModel).arg(m_coreCount);
    const QString memDetail =
        QStringLiteral("%1 / %2 GB\n缓存 %3 GB")
            .arg(m_memUsedGb, 0, 'f', 1)
            .arg(m_memTotalGb, 0, 'f', 1)
            .arg(m_memCacheGb, 0, 'f', 1);
    const QString diskDetail =
        QStringLiteral("%1 / %2 GB\n主目录分区")
            .arg(m_diskUsedGb, 0, 'f', 0)
            .arg(m_diskTotalGb, 0, 'f', 0);

    const QRect cpuCard(MARGIN, cardY, cardWidth, cardHeight);
    const QRect memoryCard(MARGIN + cardWidth + cardGap, cardY,
                           cardWidth, cardHeight);
    const QRect diskCard(MARGIN + (cardWidth + cardGap) * 2, cardY,
                         cardWidth, cardHeight);
    m_cpuRingRect = QRect(cpuCard.left() + 12, cpuCard.top() + 38, 66, 66);
    m_memoryRingRect =
        QRect(memoryCard.left() + 12, memoryCard.top() + 38, 66, 66);
    m_diskRingRect =
        QRect(diskCard.left() + 12, diskCard.top() + 38, 66, 66);

    if (painter.clipRegion().intersects(cpuCard)) drawMetricCard(painter, cpuCard,
        QStringLiteral("CPU"), cpuDetail, m_cpuPercent,
        colors.cpu, m_cpuHistory);
    const int coreAreaX = cpuCard.left() + 92;
    const int coreAreaY = cpuCard.top() + 105;
    const int coreAreaW = cpuCard.right() - coreAreaX - 12;
    const int shownCores = std::min(CORE_LIMIT, std::max(1, m_coreCount));
    // Prefer two rows above eight cores, adding rows when the compact card
    // cannot fit every four-pixel track without spilling into the next card.
    const int coreGap = 3;
    const int rowCapacity = qMax(1, (coreAreaW + coreGap) / (4 + coreGap));
    const int coreRows = qMax(shownCores > 8 ? 2 : 1,
                             (shownCores + rowCapacity - 1) / rowCapacity);
    const int coresPerRow = (shownCores + coreRows - 1) / coreRows;
    const int trackHeight = qMax(1, (36 - coreGap * (coreRows - 1)) / coreRows);
    const int coreW =
        std::max(4, (coreAreaW - coreGap * (coresPerRow - 1)) / coresPerRow);
    m_coreRects.clear();
    for (int i = 0; i < shownCores; ++i) {
        const QRect track(coreAreaX + (i % coresPerRow) * (coreW + coreGap),
                          coreAreaY + (i / coresPerRow) * (trackHeight + coreGap),
                          coreW, trackHeight);
        m_coreRects.append(track);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.track);
        painter.drawRoundedRect(track, 2, 2);
        const double corePercent = m_corePercents.value(i, m_cpuPercent);
        const int fillH = qRound(track.height() * corePercent / 100.0);
        if (fillH > 0) {
            painter.setBrush(corePercent > 80.0
                                 ? QColor(244, 63, 94)
                                 : corePercent > 65.0
                                       ? QColor(245, 158, 11)
                                       : colors.cpu);
            painter.drawRoundedRect(
                QRect(track.left(), track.bottom() - fillH + 1,
                      track.width(), fillH), 2, 2);
        }
    }
    if (painter.clipRegion().intersects(memoryCard)) drawMetricCard(painter, memoryCard,
        QStringLiteral("内存"), memDetail, m_memPercent,
        colors.memory, m_memHistory);
    if (painter.clipRegion().intersects(diskCard)) drawMetricCard(painter, diskCard,
        QStringLiteral("磁盘"), diskDetail, m_diskPercent,
        colors.disk, m_diskHistory);

    if (!m_compact) {
        const int panelTop = cardY + cardHeight + 12;
        const int panelGap = 12;
        const int panelHeight = logicalHeight() - panelTop - 16;
        // A wider native process table and a compact local summary.
        const int leftWidth =
            qRound((logicalWidth() - MARGIN * 2 - panelGap) * 0.45);
        drawProcessPanel(painter,
            QRect(MARGIN, panelTop, leftWidth,
                  panelHeight), colors);
        drawOverviewPanel(painter,
            QRect(MARGIN + leftWidth + panelGap, panelTop,
                  logicalWidth() - MARGIN * 2 - leftWidth - panelGap,
                  panelHeight),
            colors);
    } else if (m_hovered && m_editMode) {
        QFont foot = uiFont(-1);
        painter.setFont(foot);
        painter.setPen(colors.muted);
        painter.drawText(QRect(MARGIN, logicalHeight() - 26,
                               logicalWidth() - MARGIN * 2, 14),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("编辑模式：拖动任意空白区域移动"));
    }

    if (m_editMode) {
        painter.setPen(QPen(colors.cpu, 2));
        painter.setBrush(QColor(colors.cpu.red(), colors.cpu.green(),
                                colors.cpu.blue(), 45));
        painter.drawRoundedRect(m_resizeHandleRect, 3, 3);
        painter.drawLine(m_resizeHandleRect.left() + 5,
                         m_resizeHandleRect.bottom() - 4,
                         m_resizeHandleRect.right() - 4,
                         m_resizeHandleRect.top() + 5);
    }
}

void SystemMonitor::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    resize(baseSize() * m_scale);
    if (!compact) {
        m_tick = 0;
        refreshStats();
        QTimer::singleShot(5500, this, [this] {
            if (isVisible() && !m_compact) {
                m_tick = 0;
                refreshStats();
            }
        });
    }
    move(boundedPosition(pos()));
    saveSettings();
    update();
}

void SystemMonitor::setSkin(Skin skin)
{
    if (m_skin == skin)
        return;
    m_skin = skin;
    publishVideoCutout();
    m_liquidPointerEffect->setEnabled(skin == Skin::Liquid);
    if (skin == Skin::Wallpaper)
        refreshWallpaperTheme();
    saveSettings();
    update();
    emit appearanceChanged();
}

void SystemMonitor::resetVisualDefaults(QWidget *settingsPage)
{
    bool functionalDraft=false;
    if(settingsPage) {
        if(auto *interval=settingsPage->findChild<QSpinBox *>("monitorSampleInterval"))
            functionalDraft|=interval->value()!=m_statIntervalSec;
        if(auto *sort=settingsPage->findChild<QComboBox *>("monitorSortBy"))
            functionalDraft|=(sort->currentData().toString()=="memory")!=m_sortByMemory;
        if(auto *framework=settingsPage->findChild<QCheckBox *>("monitorFramework"))
            functionalDraft|=framework->isChecked()!=m_showFramework;
    }
    const QPoint savedPosition=pos();
    m_compact=false;
    m_skin=Skin::Liquid;
    m_scale=1.0;
    m_premiumAesthetics=false;
    m_widgetTitle=defaultWidgetTitle();
    m_widgetFontFamily.clear();
    m_widgetFontSize=9;
    m_windowOpacity=100;
    m_wallpaperThemeColor=wallpaperSampleColor();
    resize(baseSize());
    move(boundedPosition(savedPosition));
    publishVideoCutout();
    if(m_liquidPointerEffect)m_liquidPointerEffect->setEnabled(true);
    if(settingsPage) {
        if(auto *skin=settingsPage->findChild<QComboBox *>("monitorSkin"))skin->setCurrentIndex(int(Skin::Liquid));
        if(auto *title=settingsPage->findChild<QLineEdit *>("monitorTitle"))title->setText(m_widgetTitle);
        if(auto *font=settingsPage->findChild<QComboBox *>("monitorFont"))font->setCurrentIndex(qMax(0,font->findData(QString())));
        if(auto *size=settingsPage->findChild<QSpinBox *>("monitorFontSize"))size->setValue(m_widgetFontSize);
        if(auto *opacity=settingsPage->findChild<QSlider *>("monitorOpacity"))opacity->setValue(m_windowOpacity);
        if(auto *glow=settingsPage->findChild<QCheckBox *>("monitorCardGlow"))glow->setChecked(m_premiumAesthetics);
        if(auto *compact=settingsPage->findChild<QCheckBox *>("monitorCompact"))compact->setChecked(m_compact);
        settingsPage->setProperty("settingsDirty",functionalDraft);
    }
    // Keep sampling, sorting, framework visibility, startup, process choice,
    // and histories as they are. The normal settings writer preserves them.
    saveSettings();
    update();
    emit appearanceChanged();
}

QPoint SystemMonitor::boundedPosition(const QPoint &position) const
{
    if (!parentWidget())
        return position;
    const QRect bounds = parentWidget()->rect();
    constexpr int snapDist = 32;  // 磁吸触发距离（像素）
    const int maxX = std::max(bounds.left(), bounds.right() - width() + 1);
    const int maxY = std::max(bounds.top(), bounds.bottom() - height() + 1);

    int x = qBound(bounds.left(), position.x(), maxX);
    int y = qBound(bounds.top(), position.y(), maxY);

    // 边缘磁吸：靠近屏幕边缘时自动吸附
    if (qAbs(x - bounds.left()) < snapDist)
        x = bounds.left();
    if (qAbs(x - maxX) < snapDist)
        x = maxX;
    if (qAbs(y - bounds.top()) < snapDist)
        y = bounds.top();
    if (qAbs(y - maxY) < snapDist)
        y = maxY;

    return QPoint(x, y);
}

void SystemMonitor::restoreSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("systemMonitor"));
    m_compact = settings.value(QStringLiteral("compact"), false).toBool();
    m_skin = static_cast<Skin>(
        qBound(0, settings.value(QStringLiteral("skin"),
                                 static_cast<int>(Skin::Liquid)).toInt(), 5));
    m_scale = qBound(0.65,
                     settings.value(QStringLiteral("scale"), 1.0).toDouble(),
                     1.60);
    m_statIntervalSec = qBound(
        1, settings.value(QStringLiteral("statInterval"),
                          DEFAULT_STAT_INTERVAL_SEC).toInt(), 60);
    m_autoStart = settings.value(QStringLiteral("autoStart"), true).toBool();
    m_premiumAesthetics = settings.value(QStringLiteral("premiumAesthetics"), false).toBool();
    m_sortByMemory=settings.value(QStringLiteral("sortByMemory"),false).toBool();
    m_showFramework=settings.value(QStringLiteral("showFramework"),true).toBool();
    const QString legacyTitles[] = {
        QStringLiteral("飞腾桌面资源监控"),
        // Historic config on some deployments stored a mojibake first glyph
        // (U+98FE instead of U+98DE). Migrate it with the intact spelling.
        QString::fromUtf8("\u98fe\u817e\u684c\u9762\u8d44\u6e90\u76d1\u63a7"),
    };
    m_widgetTitle = settings.value(QStringLiteral("title")).toString().trimmed();
    if (m_widgetTitle.isEmpty() || m_widgetTitle == legacyTitles[0]
        || m_widgetTitle == legacyTitles[1])
        m_widgetTitle = defaultWidgetTitle();
    m_widgetFontFamily =
        settings.value(QStringLiteral("fontFamily")).toString();
    m_widgetFontSize = qBound(
        8, settings.value(QStringLiteral("fontSize"), 9).toInt(), 16);
    m_windowOpacity = qBound(
        25, settings.value(QStringLiteral("opacity"), 100).toInt(), 100);
    m_wallpaperThemeColor = QColor(
        settings.value(QStringLiteral("wallpaperThemeColor")).toString());
    if (!m_wallpaperThemeColor.isValid())
        m_wallpaperThemeColor = wallpaperSampleColor();
    resize(baseSize() * m_scale);

    QPoint position = settings.value(QStringLiteral("position"),
                                     QPoint(-1, -1)).toPoint();
    if (position.x() < 0 || position.y() < 0) {
        QSize area = parentWidget() ? parentWidget()->size() : QSize();
        if (area.isEmpty() && QApplication::primaryScreen())
            area = QApplication::primaryScreen()->size();
        position = QPoint(std::max(MARGIN, area.width() - width() - 24), 48);
    }
    move(boundedPosition(position));
    settings.endGroup();

}

bool SystemMonitor::autoStartEnabled()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("systemMonitor"));
    const bool enabled = settings.value(QStringLiteral("autoStart"), true).toBool();
    settings.endGroup();
    return enabled;
}

void SystemMonitor::setAutoStart(bool enabled)
{
    if (m_autoStart == enabled)
        return;
    m_autoStart = enabled;
    saveSettings();
}

void SystemMonitor::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("systemMonitor"));
    settings.setValue(QStringLiteral("position"), pos());
    settings.setValue(QStringLiteral("compact"), m_compact);
    settings.setValue(QStringLiteral("skin"), static_cast<int>(m_skin));
    settings.setValue(QStringLiteral("scale"), m_scale);
    settings.setValue(QStringLiteral("statInterval"), m_statIntervalSec);
    settings.setValue(QStringLiteral("autoStart"), m_autoStart);
    settings.setValue(QStringLiteral("premiumAesthetics"), m_premiumAesthetics);
    settings.setValue(QStringLiteral("sortByMemory"),m_sortByMemory);
    settings.setValue(QStringLiteral("showFramework"),m_showFramework);
    settings.setValue(QStringLiteral("title"), m_widgetTitle);
    settings.setValue(QStringLiteral("fontFamily"), m_widgetFontFamily);
    settings.setValue(QStringLiteral("fontSize"), m_widgetFontSize);
    settings.setValue(QStringLiteral("opacity"), m_windowOpacity);
    settings.setValue(QStringLiteral("wallpaperThemeColor"),
                      m_wallpaperThemeColor.name(QColor::HexArgb));
    settings.sync();
    QFile::setPermissions(
        settings.fileName(),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    settings.endGroup();
}

QString SystemMonitor::resourceSnapshotText(const ProcessInfo *process) const
{
    QStringList lines{
        QStringLiteral("桌面资源快照 · %1").arg(m_lastUpdated.isValid()?m_lastUpdated.toString("yyyy-MM-dd HH:mm:ss"):"尚未采样"),
        QStringLiteral("CPU：%1% · %2 核 · %3").arg(m_cpuPercent,0,'f',1).arg(m_coreCount).arg(m_cpuModel),
        QStringLiteral("内存：%1 / %2 GB（%3%），缓存 %4 GB，交换已用 %5 GB").arg(m_memUsedGb,0,'f',2).arg(m_memTotalGb,0,'f',2).arg(m_memPercent,0,'f',1).arg(m_memCacheGb,0,'f',2).arg(m_swapUsedGb,0,'f',2),
        QStringLiteral("主目录分区：%1 / %2 GB（%3%）").arg(m_diskUsedGb,0,'f',1).arg(m_diskTotalGb,0,'f',1).arg(m_diskPercent,0,'f',1),
        QStringLiteral("整盘读写：读 %1 MB/s，写 %2 MB/s").arg(m_diskReadMb,0,'f',2).arg(m_diskWriteMb,0,'f',2),
        QStringLiteral("系统负载（1/5/15 分钟）：%1 / %2 / %3").arg(m_loads.value(0),0,'f',2).arg(m_loads.value(1),0,'f',2).arg(m_loads.value(2),0,'f',2),
        QStringLiteral("最近 %1 次采样峰值：CPU %2%，内存 %3%").arg(m_cpuHistory.size()).arg(peakOf(m_cpuHistory),0,'f',1).arg(peakOf(m_memHistory),0,'f',1),
        QStringLiteral("进程 CPU 以单核 100% 计，多核进程可超过 100%；内存为 RSS，进程间共享页可能重复。"),
        QStringLiteral("PID\t进程\tCPU\tRSS MB")
    };
    const QVector<ProcessInfo> processes=process?QVector<ProcessInfo>{*process}:m_topProcs;
    for(const auto &item:processes)lines<<QString("%1\t%2\t%3\t%4").arg(item.pid).arg(item.name).arg(item.cpuValid?QString::number(item.cpuPercent,'f',1)+"%":"采样基线中").arg(item.rssMb,0,'f',1);
    if(!process&&m_showFramework) {
        lines<<"\nKylin-LQUI 框架主进程（PSS / SwapPSS MiB，不含 CLI 与 WebEngine 子进程）";
        for(const auto &item:m_framework) {
            QStringList pids;for(int pid:item.pids)pids<<QString::number(pid);
            lines<<QString("%1 · %2 · PID %3 · CPU %4 · PSS %5 · SwapPSS %6")
                .arg(item.name,item.status,pids.join(","),item.cpuValid?QString::number(item.cpu,'f',1)+"%":"—",item.memoryValid?QString::number(item.pss,'f',1):"—",item.memoryValid?QString::number(item.swap,'f',1):"—");
        }
    }
    lines<<QStringLiteral("仅本地采样；未记录命令参数、文档内容或输入。可将本快照粘贴到 AI 小站进一步分析。");
    return lines.join('\n');
}
void SystemMonitor::copyResourceSnapshot(const ProcessInfo *process) {QApplication::clipboard()->setText(resourceSnapshotText(process));}
void SystemMonitor::exportResourceSnapshot(const QString &snapshot)
{
    const QPointer<SystemMonitor> guard(this);const auto text=(snapshot.isEmpty()?resourceSnapshotText():snapshot).toUtf8();
    const QString path=LiquidDialog::getSaveFileName(this,"导出资源快照",QDir::homePath()+"/资源快照-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+".txt","文本文件 (*.txt)");
    if(!guard||path.isEmpty())return;
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(text)!=text.size()||!file.commit())LiquidDialog::warning(this,"保存失败",file.errorString());
}
void SystemMonitor::openResourceSnapshot()
{
    if(m_resourceDialog){m_resourceDialog->showNormal();m_resourceDialog->raise();m_resourceDialog->activateWindow();return;}
    auto *dialog=new LiquidDialog::Dialog(this);m_resourceDialog=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("monitorResourceSnapshot");dialog->setWindowTitle("本地资源快照");
    dialog->setWindowIcon(QIcon::fromTheme("utilities-system-monitor"));
    const QSize area=QGuiApplication::primaryScreen()->availableGeometry().size()-QSize(32,32);dialog->resize(QSize(660,470).boundedTo(area));
    auto *layout=new QVBoxLayout(dialog);auto *text=new QPlainTextEdit(resourceSnapshotText(),dialog);text->setObjectName("monitorSnapshotText");text->setReadOnly(true);layout->addWidget(text);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);buttons->button(QDialogButtonBox::Close)->setText("关闭");auto *copy=buttons->addButton("复制到剪贴板",QDialogButtonBox::ActionRole);auto *save=buttons->addButton("导出…",QDialogButtonBox::ActionRole);layout->addWidget(buttons);
    connect(copy,&QPushButton::clicked,dialog,[text]{QApplication::clipboard()->setText(text->toPlainText());});
    connect(save,&QPushButton::clicked,dialog,[this,text]{exportResourceSnapshot(text->toPlainText());});connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);dialog->show();
}

QString SystemMonitor::tooltipAt(const QPoint &position) const
{
    if(!m_compact&&m_showFramework)for(int i=0;i<m_frameworkRowRects.size()&&i<m_framework.size();++i) {
        if(!m_frameworkRowRects[i].contains(position))continue;
        const auto &item=m_framework[i];QStringList pids;for(int pid:item.pids)pids<<QString::number(pid);
        if(item.pids.isEmpty())return item.name+" · "+item.status+"\n只观察进程，不会自动启动应用。";
        return QString("%1 · %2\nPID %3\nPSS %4 MiB · 换出 %5 MiB · RSS %6 MiB\n只计当前用户组件主进程；CPU 单核 100%。\nPSS 分摊共享页；不含 CLI 子任务或 WebEngine 子进程。")
            .arg(item.name,item.status,pids.join(", "),item.memoryValid?QString::number(item.pss,'f',1):"未知",item.memoryValid?QString::number(item.swap,'f',1):"未知",QString::number(item.rss,'f',1));
    }
    if (m_cpuRingRect.contains(position)) {
        return QStringLiteral("CPU 总占用率：%1%\n处理器：%2\n逻辑核心：%3")
            .arg(m_cpuPercent, 0, 'f', 1)
            .arg(m_cpuModel)
            .arg(m_coreCount);
    }
    if (m_memoryRingRect.contains(position)) {
        return QStringLiteral("内存占用率：%1%\n已用：%2 GB\n总计：%3 GB\n"
                              "缓存：%4 GB")
            .arg(m_memPercent, 0, 'f', 1)
            .arg(m_memUsedGb, 0, 'f', 1)
            .arg(m_memTotalGb, 0, 'f', 1)
            .arg(m_memCacheGb, 0, 'f', 1);
    }
    if (m_diskRingRect.contains(position)) {
        return QStringLiteral("主目录磁盘占用率：%1%\n已用：%2 GB\n总计：%3 GB")
            .arg(m_diskPercent, 0, 'f', 1)
            .arg(m_diskUsedGb, 0, 'f', 0)
            .arg(m_diskTotalGb, 0, 'f', 0);
    }
    for (int i = 0; i < m_coreRects.size(); ++i) {
        if (!m_coreRects[i].contains(position))
            continue;
        const double frequency = m_coreFrequenciesGhz.value(i);
        return QStringLiteral("CPU 核心 %1\n当前频率：%2\n占用率：%3%")
            .arg(i + 1)
            .arg(frequency > 0.0
                     ? QStringLiteral("%1 GHz").arg(frequency, 0, 'f', 2)
                     : QStringLiteral("无法读取"))
            .arg(m_corePercents.value(i), 0, 'f', 1);
    }
    for (int i = 0; i < m_processRowRects.size() &&
                    i < m_topProcs.size(); ++i) {
        if (!m_processRowRects[i].contains(position))
            continue;
        const ProcessInfo &process = m_topProcs[i];
        return QStringLiteral("应用：%1\nPID：%2\nCPU：%3%\n内存：%4 MB\n"
                              "路径：%5\nCPU 以单核 100% 计，多核进程可超过 100%。")
            .arg(process.name)
            .arg(process.pid)
            .arg(process.cpuPercent, 0, 'f', 1)
            .arg(process.rssMb, 0, 'f', 0)
            .arg(process.executablePath.isEmpty()
                     ? QStringLiteral("无权限读取或内核线程")
                     : process.executablePath);
    }
    if(m_copyRect.contains(position))return QStringLiteral("复制本地采样，可粘贴到 AI 小站分析。");
    if(m_detailsRect.contains(position))return QStringLiteral("查看、复制或导出本地资源快照。");
    if (m_optimizeRect.contains(position))
        return QStringLiteral("立即刷新全部资源数据");
    if (m_toggleRect.contains(position))
        return m_compact ? QStringLiteral("展开进程与资源概况")
                         : QStringLiteral("收起为紧凑视图");
    if (m_skinRect.contains(position))
        return QStringLiteral("切换整个小组件窗口皮肤");
    if (m_settingsRect.contains(position))
        return QStringLiteral("配置标题、字体、字号与自动启动");
    if (m_resizeHandleRect.contains(position) && m_editMode)
        return QStringLiteral("按住拖动，等比例放大或缩小组件");
    return QString();
}

void SystemMonitor::showSettingsDialog()
{
    if (auto *canvas = qobject_cast<DesktopCanvas *>(parentWidget()))
        canvas->showSettingsPage("monitor");
}

QWidget *SystemMonitor::createSettingsPage(QWidget *parent)
{
    auto *form = new QWidget(parent);
    QWidget &dialog = *form;
    connect(this, &QObject::destroyed, form, &QObject::deleteLater);
    dialog.setWindowTitle(QStringLiteral("小组件配置"));
    dialog.setMinimumWidth(500);
    auto *layout = new QFormLayout(&dialog);

    auto *skinChoice=new QComboBox(&dialog);
    skinChoice->setObjectName("monitorSkin");
    skinChoice->addItems({"深色","浅色","赛博","毛玻璃","壁纸取色","液态玻璃"});
    skinChoice->setCurrentIndex(static_cast<int>(m_skin));
    layout->addRow("外观皮肤：",skinChoice);

    auto *titleEdit = new QLineEdit(m_widgetTitle, &dialog);
    titleEdit->setObjectName("monitorTitle");
    titleEdit->setMaxLength(40);
    layout->addRow(QStringLiteral("小组件标题："), titleEdit);

    auto *fontCombo = new QComboBox(&dialog);
    fontCombo->setObjectName("monitorFont");
    fontCombo->addItem(QStringLiteral("系统默认"), QString());
    static const QStringList families = QFontDatabase().families();
    for (const QString &family : families)
        fontCombo->addItem(family, family);
    const int fontIndex = fontCombo->findData(m_widgetFontFamily);
    if (fontIndex >= 0)
        fontCombo->setCurrentIndex(fontIndex);
    layout->addRow(QStringLiteral("小组件字体："), fontCombo);

    auto *fontSize = new QSpinBox(&dialog);
    fontSize->setObjectName("monitorFontSize");
    fontSize->setRange(8, 16);
    fontSize->setValue(m_widgetFontSize);
    fontSize->setSuffix(QStringLiteral(" px"));
    layout->addRow(QStringLiteral("字体大小："), fontSize);

    auto *opacitySlider = new QSlider(Qt::Horizontal, &dialog);
    opacitySlider->setObjectName("monitorOpacity");
    opacitySlider->setRange(25, 100);
    opacitySlider->setValue(m_windowOpacity);
    auto *opacityLabel = new QLabel(
        QStringLiteral("%1%").arg(m_windowOpacity), &dialog);
    auto *opacityRow = new QWidget(&dialog);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(opacitySlider);
    opacityLayout->addWidget(opacityLabel);
    connect(opacitySlider, &QSlider::valueChanged,
            opacityLabel, [opacityLabel](int value) {
        opacityLabel->setText(QStringLiteral("%1%").arg(value));
    });
    layout->addRow(QStringLiteral("窗口透明度："), opacityRow);

    auto *preview = new QLabel(m_widgetTitle, &dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(42);
    auto updatePreview = [=] {
        QFont previewFont;
        const QString family = fontCombo->currentData().toString();
        if (!family.isEmpty())
            previewFont.setFamily(family);
        previewFont.setPixelSize(fontSize->value());
        previewFont.setBold(true);
        preview->setFont(previewFont);
        preview->setText(titleEdit->text().trimmed().isEmpty()
                             ? QStringLiteral("小组件标题预览")
                             : titleEdit->text().trimmed());
    };
    updatePreview();
    connect(titleEdit, &QLineEdit::textChanged,
            &dialog, [=](const QString &) { updatePreview(); });
    connect(fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [=](int) { updatePreview(); });
    connect(fontSize, QOverload<int>::of(&QSpinBox::valueChanged),
            &dialog, [=](int) { updatePreview(); });
    layout->addRow(QStringLiteral("预览："), preview);

    auto *intervalSpin = new QSpinBox(&dialog);
    intervalSpin->setObjectName("monitorSampleInterval");
    intervalSpin->setRange(1, 60);
    intervalSpin->setValue(m_statIntervalSec);
    intervalSpin->setSuffix(QStringLiteral(" 秒"));
    layout->addRow(QStringLiteral("采样周期："), intervalSpin);

    auto *sort=new QComboBox(&dialog);sort->setObjectName("monitorSortBy");sort->addItem("CPU 占用","cpu");sort->addItem("内存占用","memory");sort->setCurrentIndex(m_sortByMemory?1:0);layout->addRow("进程排序：",sort);
    auto *compact=new QCheckBox("以紧凑视图显示",&dialog);compact->setObjectName("monitorCompact");compact->setChecked(m_compact);layout->addRow("视图：",compact);

    auto *framework=new QCheckBox("显示 Kylin-LQUI 框架进程与内存",&dialog);
    framework->setObjectName("monitorFramework");framework->setChecked(m_showFramework);
    framework->setToolTip("复用进程采样；显示当前用户的组件主进程。按需应用未启动不代表故障。PSS 为分摊常驻内存，换出量见悬停说明。");
    layout->addRow("框架状态：",framework);
    auto *premiumCheck = new QCheckBox(
        QStringLiteral("增强卡片光感"), &dialog);
    premiumCheck->setObjectName("monitorCardGlow");
    premiumCheck->setChecked(m_premiumAesthetics);
    layout->addRow(QStringLiteral("高级美化："), premiumCheck);

    auto *resourceHint = new QLabel(
        QStringLiteral("全部指标通过读取 /proc 内核文件系统本地采集；"
                       "只在本机展示和保存指标，不连接模型服务。"),
        &dialog);
    resourceHint->setWordWrap(true);
    layout->addRow(QStringLiteral("资源策略："), resourceHint);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply, &dialog);
    layout->addRow(buttons);

    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [=, &dialog] {
        m_widgetTitle = titleEdit->text().trimmed();
        if (m_widgetTitle.isEmpty())
            m_widgetTitle = defaultWidgetTitle();
        m_widgetFontFamily = fontCombo->currentData().toString();
        m_widgetFontSize = fontSize->value();
        m_windowOpacity = opacitySlider->value();
        m_statIntervalSec = intervalSpin->value();
        m_premiumAesthetics = premiumCheck->isChecked();
        m_sortByMemory=sort->currentData().toString()=="memory";
        m_showFramework=framework->isChecked();
        setCompact(compact->isChecked());
        // 应用新的采样周期
        m_timer.setInterval(statIntervalMs());
        setSkin(static_cast<Skin>(skinChoice->currentIndex()));
        saveSettings();
        refreshStats();
        update();
        emit appearanceChanged();
        dialog.setProperty("settingsDirty", false);
    });
    return form;
}

void SystemMonitor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint logicalPos = logicalPosition(event->pos());
        if (m_editMode && m_resizeHandleRect.contains(logicalPos)) {
            m_resizing = true;
            m_resizeStartGlobal = event->globalPos();
            m_resizeStartSize = size();
            m_resizeStartPosition = pos();
            if (!m_mouseGrabbed) {
                grabMouse();
                m_mouseGrabbed = true;
            }
            setCursor(Qt::SizeFDiagCursor);
            event->accept();
            return;
        }
        if (m_optimizeRect.contains(logicalPos)) {
            refreshStats();
            event->accept();
            return;
        }
        if (m_toggleRect.contains(logicalPos)) {
            setCompact(!m_compact);
            event->accept();
            return;
        }
        if (m_skinRect.contains(logicalPos)) {
            QMenu menu(this);
            MenuStyle::applyVenturaContextMenu(&menu);
            QAction *dark = menu.addAction(QStringLiteral("极客碳黑"));
            QAction *light = menu.addAction(QStringLiteral("优雅亮白"));
            QAction *cyber = menu.addAction(QStringLiteral("赛博霓虹"));
            QAction *glass = menu.addAction(QStringLiteral("晶莹玻璃"));
            QAction *wallpaper =
                menu.addAction(QStringLiteral("壁纸自动取色"));
            QAction *liquid = menu.addAction(QStringLiteral("NextKDE 液态玻璃"));
            const QRect anchor(mapToGlobal(QPoint(qRound(m_skinRect.left()*m_scale),
                                                  qRound(m_skinRect.top()*m_scale))),
                               QSize(qRound(m_skinRect.width()*m_scale),qRound(m_skinRect.height()*m_scale)));
            QAction *chosen = LiquidPopup::execAt(menu,anchor);
            if (chosen == dark) setSkin(Skin::Dark);
            else if (chosen == light) setSkin(Skin::Light);
            else if (chosen == cyber) setSkin(Skin::Cyber);
            else if (chosen == glass) setSkin(Skin::Glass);
            else if (chosen == liquid) setSkin(Skin::Liquid);
            else if (chosen == wallpaper) {
                refreshWallpaperTheme();
                setSkin(Skin::Wallpaper);
            }
            event->accept();
            return;
        }
        if (m_settingsRect.contains(logicalPos)) {
            showSettingsDialog();
            event->accept();
            return;
        }
        if(m_copyRect.contains(logicalPos)){copyResourceSnapshot();event->accept();return;}
        if(m_detailsRect.contains(logicalPos)){openResourceSnapshot();event->accept();return;}
        // 非编辑模式完全固定；只有全局编辑模式打开后才能移动。
        if (m_editMode) {
            m_dragging = true;
            m_dragOffset = event->pos();
            if (!m_mouseGrabbed) {
                grabMouse();
                m_mouseGrabbed = true;
            }
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SystemMonitor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = event->globalPos() - m_resizeStartGlobal;
        const QSize logical = baseSize();

        auto *canvas = qobject_cast<DesktopCanvas *>(parentWidget());
        if (!canvas) return;
        const QRect bounds = canvas->rect();
        const QRect start(m_resizeStartPosition, m_resizeStartSize);
        Qt::Edges fixed = Qt::TopEdge | Qt::LeftEdge;
        // Freeze the anchor chosen at press time; acquiring a snap during the
        // drag must not switch anchors or make the next mouse event jump.
        if (qAbs(start.x()-bounds.x())>8
            && qAbs(start.x()+start.width()-bounds.x()-bounds.width())<=8)
            fixed = (fixed & ~Qt::LeftEdge) | Qt::RightEdge;
        if (qAbs(start.y()-bounds.y())>8
            && qAbs(start.y()+start.height()-bounds.y()-bounds.height())<=8)
            fixed = (fixed & ~Qt::TopEdge) | Qt::BottomEdge;
        const double horizontal = (m_resizeStartSize.width()+delta.x()) /
            static_cast<double>(logical.width());
        const double vertical = (m_resizeStartSize.height()+delta.y()) /
            static_cast<double>(logical.height());
        const double scale = qBound(0.65, (horizontal+vertical)/2.0, 1.60);
        const QRect snapped = canvas->snappedWidgetResize(this, start, logical*scale,
            logical*0.65, logical*1.60,
            logical.width()/static_cast<qreal>(logical.height()), fixed);
        m_scale = snapped.width()/static_cast<double>(logical.width());
        setGeometry(snapped);
        update();
        event->accept();
        return;
    }
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint parentPoint =
            parentWidget() ? parentWidget()->mapFromGlobal(event->globalPos())
                           : event->globalPos();
        move(boundedPosition(parentPoint - m_dragOffset));
        event->accept();
        return;
    }

    const QPoint logicalPos = logicalPosition(event->pos());
    if (m_editMode && m_resizeHandleRect.contains(logicalPos))
        setCursor(Qt::SizeFDiagCursor);
    else
        setCursor(m_editMode ? Qt::SizeAllCursor : Qt::ArrowCursor);

    const QString tooltip = tooltipAt(logicalPos);
    if (tooltip.isEmpty())
        LiquidPopup::hideText();
    else
        LiquidPopup::showText(event->globalPos() + QPoint(12, 18),
                           tooltip, this);
    QWidget::mouseMoveEvent(event);
}

void SystemMonitor::mouseReleaseEvent(QMouseEvent *event)
{
    if ((m_dragging || m_resizing) &&
        event->button() == Qt::LeftButton) {
        m_dragging = false;
        m_resizing = false;
        if (m_mouseGrabbed) {
            releaseMouse();
            m_mouseGrabbed = false;
        }
        setCursor(m_editMode ? Qt::SizeAllCursor : Qt::ArrowCursor);
        saveSettings();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SystemMonitor::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPoint point = logicalPosition(event->pos());
    if (!m_editMode && event->button() == Qt::LeftButton && point.y() <= HEADER_H &&
        !m_settingsRect.contains(point) && !m_skinRect.contains(point) &&
        !m_toggleRect.contains(point) && !m_optimizeRect.contains(point)) {
        setCompact(!m_compact);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SystemMonitor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    MenuStyle::applyVenturaContextMenu(&menu);
    ProcessInfo selectedProcess;
    bool hasSelectedProcess = false;
    const QPoint logicalPos = logicalPosition(event->pos());
    if (!m_compact) {
        for (int i = 0; i < m_processRowRects.size() &&
                        i < m_topProcs.size(); ++i) {
            if (!m_processRowRects[i].contains(logicalPos))
                continue;
            selectedProcess = m_topProcs[i];
            hasSelectedProcess = true;
            break;
        }
    }
    QAction *processCopy=nullptr;
    if(hasSelectedProcess){processCopy=menu.addAction(QStringLiteral("复制进程指标 · %1").arg(selectedProcess.name));menu.addSeparator();}
    QAction *toggle=menu.addAction(m_compact?QStringLiteral("展开详细信息"):QStringLiteral("切换紧凑模式"));
    QAction *snapshot=menu.addAction(QStringLiteral("查看资源快照"));
    QAction *copy=menu.addAction(QStringLiteral("复制资源快照"));
    QAction *exportData=menu.addAction(QStringLiteral("导出资源快照…"));
    QAction *sort=menu.addAction(m_sortByMemory?QStringLiteral("按 CPU 排序"):QStringLiteral("按内存排序"));
    QAction *settings =
        menu.addAction(QStringLiteral("小组件设置"));
    QMenu *skins = menu.addMenu(QStringLiteral("窗口皮肤"));
    MenuStyle::applyVenturaContextMenu(skins);
    QAction *dark = skins->addAction(QStringLiteral("极客碳黑"));
    QAction *light = skins->addAction(QStringLiteral("优雅亮白"));
    QAction *cyber = skins->addAction(QStringLiteral("赛博霓虹"));
    QAction *glass = skins->addAction(QStringLiteral("晶莹玻璃"));
    QAction *wallpaper = skins->addAction(QStringLiteral("壁纸自动取色"));
    QAction *liquid = skins->addAction(QStringLiteral("NextKDE 液态玻璃"));
    menu.addSeparator();
    QAction *close = menu.addAction(QStringLiteral("隐藏小组件"));

    QAction *chosen = menu.exec(event->globalPos());
    if(processCopy&&chosen==processCopy)copyResourceSnapshot(&selectedProcess);
    else if(chosen==toggle)setCompact(!m_compact);
    else if(chosen==snapshot)openResourceSnapshot();
    else if(chosen==copy)copyResourceSnapshot();
    else if(chosen==exportData)exportResourceSnapshot();
    else if(chosen==sort){m_sortByMemory=!m_sortByMemory;saveSettings();refreshStats();}
    else if (chosen == settings)
        showSettingsDialog();
    else if (chosen == dark)
        setSkin(Skin::Dark);
    else if (chosen == light)
        setSkin(Skin::Light);
    else if (chosen == cyber)
        setSkin(Skin::Cyber);
    else if (chosen == glass)
        setSkin(Skin::Glass);
    else if (chosen == liquid)
        setSkin(Skin::Liquid);
    else if (chosen == wallpaper) {
        refreshWallpaperTheme();
        setSkin(Skin::Wallpaper);
    }
    else if (chosen == close)
        emit closeRequested();
}

void SystemMonitor::enterEvent(QEvent *)
{
    m_hovered = true;
    m_liquidGlassPointer = logicalPosition(mapFromGlobal(QCursor::pos()));
    update();
}

void SystemMonitor::leaveEvent(QEvent *)
{
    m_hovered = false;
    LiquidPopup::hideText();
    update();
}

void SystemMonitor::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    m_liquidGlassImage = {};
    m_liquidGlassGeometry = {};
    if (m_skin == Skin::Liquid) update();
}

void SystemMonitor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_liquidGlassImage = {};
    m_liquidGlassGeometry = {};
    publishVideoCutout();
}

void SystemMonitor::publishVideoCutout()
{
    // Every skin paints a 14px logical rounded panel; the native video must
    // stay visible outside it instead of exposing the poster in the corners.
    VideoWallpaperRegion::publish(
        this, VideoWallpaperRegion::rounded(QRectF(rect()), 14.0 * m_scale, devicePixelRatioF()));
}

void SystemMonitor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        setCompact(event->angleDelta().y() < 0);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void SystemMonitor::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshStats();
    if (!m_timer.isActive())
        m_timer.start(statIntervalMs());
}

void SystemMonitor::hideEvent(QHideEvent *event)
{
    m_timer.stop();
    if (m_mouseGrabbed) {
        releaseMouse();
        m_mouseGrabbed = false;
    }
    m_dragging = false;
    m_resizing = false;
    QWidget::hideEvent(event);
}
