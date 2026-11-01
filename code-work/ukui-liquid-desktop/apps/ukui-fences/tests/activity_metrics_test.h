#pragma once
#include "ProcessMetrics.h"
#include <QTableWidget>
#include <QJsonArray>
#include <QFileInfo>

struct ActivityMetricsTestAccess {
    static int run(const QString &root) {
        ProcessMetrics::Snapshot before,after;before.monotonicMs=1000;before.bootTicks=10000;before.cores=8;
        after=before;after.monotonicMs=2000;after.bootTicks=10100;
        before.processes[100]={100,1,100,100,12,"terminal",{}};
        before.processes[101]={101,100,101,200,20,"editor",{}};
        before.processes[102]={102,101,102,300,30,"worker",{}};
        after.processes=before.processes;after.processes[100].ticks+=50;after.processes[101].ticks+=150;after.processes[102].ticks+=50;
        const QHash<int,QString> roots{{100,"terminal"},{101,"editor"}};
        auto groups=ProcessMetrics::aggregate(after,before,roots,{"terminal","editor"},30000);
        check(qAbs(groups["terminal"].cpuPercent-50)<.001&&qAbs(groups["editor"].cpuPercent-200)<.001,"process grouping respects nearest application roots and multi-core CPU");
        check(groups["terminal"].rssMb==12&&groups["editor"].rssMb==50&&groups["editor"].processes==2,"GUI child and descendant memory are counted once, outside the terminal group");
        auto reused=after;reused.processes[102].startTicks=9999;reused.processes[102].ticks=9000;
        groups=ProcessMetrics::aggregate(reused,before,roots,{"editor"},30000);
        check(qAbs(groups["editor"].cpuPercent-150)<.001,"PID reuse never subtracts counters from an unrelated old process");
        auto suspended=after;suspended.bootTicks+=360000;
        check(ProcessMetrics::aggregate(suspended,before,roots,{"editor"},30000)["editor"].cpuPercent<0,"suspend gap does not become an artificial CPU spike");
        auto late=after;late.monotonicMs=90000;
        check(ProcessMetrics::aggregate(late,before,roots,{"editor"},30000)["editor"].cpuPercent<0,"late samples discard an invalid CPU interval");
        ResourceStats stats;const auto now=QDateTime::currentDateTime();stats.observe(80,40,1000,1,now);stats.observe(20,80,3000,2,now.addSecs(3));
        check(stats.cpuMean()==35&&stats.rssMeanMb()==70&&stats.cpuPeak==80&&stats.rssPeakMb==80,"resource means use observed duration while peaks retain real samples");
        const auto restored=ResourceStats::fromJson(stats.toJson());check(restored.cpuMean()==35&&restored.samples==2,"resource aggregate persists without losing weighting");
        ActivityLedger crossing;const QDate date=QDate::currentDate();crossing.addResources(QDateTime(date,QTime(0,0,5)),10000,"editor","Editor",20,30,2);
        check(crossing.days[date].apps["editor"].resources.observedMs==5000&&crossing.days[date.addDays(-1)].apps["editor"].resources.observedMs==5000,"resource coverage splits correctly at local midnight");

        QSettings settings;settings.setValue("systemMonitor/autoStart",false);for(const auto *id:{"clock","activity","music","calendar"})settings.setValue(QString("desklets/%1/autoStart").arg(id),false);
        settings.setValue("desklets/activity/metricsEnabled",false);
        const QString ledgerPath=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/ukui-fences/widgets/activity.json";QDir().mkpath(QFileInfo(ledgerPath).absolutePath());
        QJsonObject old{{"schemaVersion",1},{"days",QJsonObject{{date.toString(Qt::ISODate),QJsonObject{{"uptimeMs",3600000},{"apps",QJsonObject{{"editor",QJsonObject{{"name","测试编辑器"},{"ms",1800000}}}}}}}}}};
        {QFile file(ledgerPath);check(file.open(QIODevice::WriteOnly),"legacy ledger fixture opens");file.write(QJsonDocument(old).toJson());}
        DesktopCanvas canvas;canvas.showAndActivate();canvas.setActivityWidgetVisible(true);settle(100);
        auto *recorder=canvas.findChild<ActivityRecorder *>();auto *activity=canvas.findChild<ActivityDesklet *>();check(recorder&&activity,"activity card and recorder are available");
        check(recorder->ledger().days.value(date).apps.value("editor").ms==1800000,"v1 ledger migration preserves the existing application duration");
        check(!QDir(QFileInfo(ledgerPath).absolutePath()).entryList({"activity.json.v1-*.bak"}).isEmpty(),"legacy ledger is backed up before writing schema v2");
        recorder->configureMetrics(true,10,7);recorder->m_metricsTimer.stop();
        before.monotonicMs=1000;recorder->m_previousResources=before;after.monotonicMs=11000;after.at=now;after.bootTicks=11000;
        groups={{"editor",{26.,96.,2}}};recorder->acceptResources(after,groups,{{"editor","测试编辑器"}});
        const auto observed=recorder->ledger().days.value(date).apps.value("editor").resources;
        check(observed.observedMs==10000&&observed.cpuMean()==26&&observed.rssPeakMb==96,"accepted process-group sample updates the daily aggregate");
        recorder->flush();QFile ledger(ledgerPath);check(ledger.open(QIODevice::ReadOnly),"migrated ledger remains readable");check(QJsonDocument::fromJson(ledger.readAll()).object().value("schemaVersion").toInt()==2,"recording writes schema v2 atomically");ledger.close();
        QFile log(recorder->metricsDirectory()+"/"+date.toString(Qt::ISODate)+".jsonl");check(log.open(QIODevice::ReadOnly),"minute batch creates a local metrics log");const auto record=QJsonDocument::fromJson(log.readLine()).object();
        check(record.value("appId").toString()=="editor"&&record.value("cpuBasis").toString()=="single-core-100-percent","metrics log carries application identity and accounting units");
        check(!(QFile::permissions(log.fileName())&QFileDevice::ReadOther),"metrics log is private to its owner");log.close();
        const QString obsolete=recorder->metricsDirectory()+"/"+date.addDays(-10).toString(Qt::ISODate)+".jsonl";
        {QFile file(obsolete);file.open(QIODevice::WriteOnly);file.write("{}\n");}const QString keep=recorder->metricsDirectory()+"/notes.txt";{QFile file(keep);file.open(QIODevice::WriteOnly);file.write("keep");}
        recorder->pruneMetricLogs();check(!QFile::exists(obsolete)&&QFile::exists(keep),"retention removes only dated expired log files");
        recorder->m_ledger.addInterval(now,2000,"formula","=HYPERLINK(\"fixture\")");check(recorder->csvForDay(date).contains("\"'=HYPERLINK"),"CSV exports neutralize application names that look like spreadsheet formulas");
        recorder->setLocked(true);check(!recorder->m_metricsTimer.isActive(),"lock screen stops resource sampling");recorder->setLocked(false);check(recorder->m_metricsTimer.isActive(),"unlock resumes configured sampling");
        recorder->setRecording(false);check(!recorder->m_metricsTimer.isActive(),"pausing activity also pauses resource sampling");recorder->setRecording(true);recorder->m_metricsTimer.stop();
        recorder->m_liveResources=groups;activity->grab();
        check(!activity->m_appRows.isEmpty()&&activity->m_appRows.size()<=5,"activity card limits its application list to the top five");
        const QPoint row=activity->m_appRows.first().first.center().toPoint();QString hoverKey;const QString hover=activity->hoverText(row,&hoverKey);
        check(hover.contains("CPU")&&hover.contains("峰值")&&hover.contains("26.0%"),"application hover includes measured CPU, memory and peaks");
        LiquidPopup::hideText();const QPoint global=activity->mapToGlobal(row);QCursor::setPos(global);QMouseEvent move(QEvent::MouseMove,row,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(activity,&move);
        check(activity->m_hoverTimer.isActive()&&activity->m_hoverTimer.interval()==1000,"application hover starts the one-second help delay");settle(1100);
        bool shown=false;for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()=="liquidPopupShell"&&widget->isVisible())shown=true;check(shown,"delayed application tooltip is actually visible");LiquidPopup::hideText();
        recorder->showDetails(activity,"editor");settle(80);auto *details=recorder->m_details.data();check(details&&details->isVisible(),"activity detail window opens");
        auto *table=details->findChild<QTableWidget *>("activityHistoryTable");check(table&&table->rowCount()==1&&table->item(0,2)->text()=="26.0%","selected application history shows its measured aggregates");
        QDir().mkpath("artifacts/activity-metrics");activity->grab().save("artifacts/activity-metrics/card.png");details->grab().save("artifacts/activity-metrics/details.png");details->close();settle(50);
        canvas.showSettingsPage("activity");settle(80);QWidget *settingsWindow=nullptr;for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()=="fencesSettingsWindow")settingsWindow=widget;
        check(settingsWindow,"Fences activity settings are reachable");auto *enabled=settingsWindow->findChild<QCheckBox *>("activityMetricsEnabled");auto *interval=settingsWindow->findChild<QSlider *>("activityMetricsInterval");auto *retention=settingsWindow->findChild<QComboBox *>("activityLogRetention");
        check(enabled&&interval&&retention&&settingsWindow->findChild<QPushButton *>("activityHistoryDetails"),"Fences exposes all activity recording and history controls");
        enabled->setChecked(false);interval->setValue(15);retention->setCurrentIndex(retention->findData(3));settings.sync();check(!recorder->metricsEnabled()&&recorder->metricsIntervalSeconds()==15&&recorder->logRetentionDays()==3,"Fences resource settings update the live sampler and persistence together");
        settingsWindow->grab().save("artifacts/activity-metrics/settings.png");settingsWindow->close();settle(80);recorder->flush();
        return 0;
    }
};
