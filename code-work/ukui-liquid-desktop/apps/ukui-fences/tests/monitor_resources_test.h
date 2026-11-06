#pragma once
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <QLineEdit>
#include <QSpinBox>

struct SystemMonitorTestAccess {
    static int run() {
        QSettings settings;settings.setValue("systemMonitor/autoStart",false);
        settings.setValue("systemMonitor/apiKey","unused-legacy-fixture");
        settings.setValue("systemMonitor/credentialStore","secret-service");
        QWidget host;host.resize(1600,950);host.show();
        SystemMonitor monitor(&host);monitor.m_timer.stop();monitor.setSkin(SystemMonitor::Skin::Dark);monitor.show();settle(150);
        auto header=[&]{QImage image(monitor.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);monitor.drawHeader(painter,monitor.paletteForSkin());painter.end();return image;};
        check(header().pixelColor(300,2).alpha()>0,"solid monitor skin retains its title plate");
        monitor.setSkin(SystemMonitor::Skin::Liquid);
        check(header().pixelColor(300,2).alpha()==0,"liquid monitor header paints no extra horizontal plate over the lens");
        monitor.setSkin(SystemMonitor::Skin::Dark);
        check(monitor.findChildren<QProcess *>().isEmpty(),"monitor never starts a keyring reader or cloud request process");
        check(settings.value("systemMonitor/apiKey").toString()=="unused-legacy-fixture","removing diagnosis leaves legacy stored credentials untouched");
        const int realCores=monitor.m_coreCount;
        const auto own=SystemMonitor::readProcessDetails(getpid());
        ProcessInfo ownFixture;ownFixture.pid=getpid();ownFixture.startTicks=own.startTicks;
        ownFixture.executablePath="/fixture/ukui-fences";ownFixture.uid=getuid();ownFixture.state="S";
        auto framework=SystemMonitor::readFrameworkSnapshot({ownFixture});
        const auto byId=[](const auto &rows,const QString &id){for(const auto &row:rows)if(row.id==id)return row;return SystemMonitor::Sample::FrameworkInfo();};
        int fencesIndex=-1;for(int i=0;i<framework.size();++i)if(framework[i].id=="fences")fencesIndex=i;
        check(fencesIndex>=0,"framework retains the stable Fences component id");
        for(const QString &id:QStringList{"settings","control-center","fences","launcher","panel","ai-station","input-ui","peony-manager","ai-space","native-desktop"})check(!byId(framework,id).name.isEmpty(),qPrintable("catalog component: "+id));
        check(byId(framework,"fences").pids.contains(getpid()),"framework catalog identifies exact main executable and current user");
        check(!byId(framework,"fences").cpuValid&&byId(framework,"fences").memoryValid&&byId(framework,"fences").pss>0,"first CPU sample is unknown while proportional memory is measured");
        check(byId(framework,"peony-manager").status=="按需启动"&&byId(framework,"peony-manager").pids.isEmpty(),"optional manager is on demand, not a failed service");
        // Use real /proc entries for a native host fixture and a Python
        // launcher. Mapping a harmless fixture file exercises plugin detection
        // without loading a style plugin into this test application.
        QTemporaryDir panelFixture;
        QFile launcherScript(panelFixture.path()+"/ukui-panel-liquid");launcherScript.open(QIODevice::WriteOnly);
        launcherScript.write("import time\ntime.sleep(30)\n");launcherScript.close();
        QProcess launcher;launcher.start("/usr/bin/python3",{launcherScript.fileName()});
        check(launcher.waitForStarted(),"panel launcher fixture starts");
        const auto launcherDetails=SystemMonitor::readProcessDetails(launcher.processId());
        ProcessInfo launcherInfo;launcherInfo.pid=launcher.processId();launcherInfo.startTicks=launcherDetails.startTicks;
        launcherInfo.executablePath="/usr/bin/python3";launcherInfo.uid=getuid();launcherInfo.state="S";
        ProcessInfo panelInfo=ownFixture;panelInfo.executablePath="/usr/bin/ukui-panel";
        auto panelRow=byId(SystemMonitor::readFrameworkSnapshot({panelInfo,launcherInfo}),"panel");
        check(panelRow.name=="原生任务栏" && panelRow.status=="原生样式" && !panelRow.warning,
              "native panel without the plugin is a valid native style, not a failed component");
        QFile pluginMap(panelFixture.path()+"/libukuiliquidpanel.so");pluginMap.open(QIODevice::ReadWrite);pluginMap.resize(4096);
        auto *mapped=pluginMap.map(0,4096);check(mapped!=nullptr,"plugin mapping fixture exists");
        panelRow=byId(SystemMonitor::readFrameworkSnapshot({launcherInfo,panelInfo}),"panel");
        check(panelRow.status=="液态样式" && !panelRow.warning && panelRow.pids.size()==2,
              "launcher is counted for resources without overriding the host's loaded liquid style");
        panelRow=byId(SystemMonitor::readFrameworkSnapshot({launcherInfo}),"panel");
        check(panelRow.status=="启动中","launcher alone is not reported as a running native panel");
        if(mapped)pluginMap.unmap(mapped);
        launcher.terminate();launcher.waitForFinished(3000);
        ownFixture.uid=getuid()+1;
        check(byId(SystemMonitor::readFrameworkSnapshot({ownFixture}),"fences").pids.isEmpty(),"other users' processes are excluded");
        ownFixture.uid=getuid();++ownFixture.startTicks;
        check(byId(SystemMonitor::readFrameworkSnapshot({ownFixture}),"fences").pids.isEmpty(),"PID reuse between common and memory samples is rejected");
        --ownFixture.startTicks;ownFixture.state="T";ownFixture.cpuValid=true;ownFixture.cpuPercent=235.;
        framework=SystemMonitor::readFrameworkSnapshot({ownFixture});
        check(byId(framework,"fences").warning&&byId(framework,"fences").status=="已暂停"&&byId(framework,"fences").cpu==235.,"stopped process status and multi-core CPU are preserved");
        for(bool compact:{false,true}) {
            monitor.m_compact=compact;monitor.resize(monitor.baseSize());
            for(int cores:{1,8,16,32,64}) {
                monitor.m_coreCount=cores;monitor.grab();
                const QRect card(monitor.MARGIN,monitor.HEADER_H+14,(monitor.logicalWidth()-monitor.MARGIN*2-24)/3,156);
                check(monitor.m_coreRects.size()==qMin(cores,monitor.CORE_LIMIT),"CPU tracks represent all supported cores");
                for(const auto &rect:monitor.m_coreRects)check(card.contains(rect),"CPU tracks remain inside their card");
            }
        }
        monitor.m_coreCount=realCores;monitor.m_compact=false;monitor.resize(monitor.baseSize());
        monitor.m_cpuPercent=38.;monitor.m_memPercent=56.;monitor.m_cpuHistory={12.,60.,38.};monitor.m_memHistory={42.,62.,56.};
        monitor.m_lastUpdated=QDateTime::currentDateTime();monitor.m_loads={1.5,1.1,.8};monitor.m_swapUsedGb=.25;monitor.m_diskReadMb=18.5;monitor.m_diskWriteMb=3.2;
        monitor.m_topProcs={{"多核应用","/fixture/application",230.,768.,99,123,true},{"第二应用","/fixture/second",15.,256.,100,124,true}};
        monitor.grab();
        monitor.m_framework=framework;monitor.grab();
        check(monitor.m_frameworkRowRects.size()==framework.size(),"all framework components have a status row");
        for(const auto &row:monitor.m_frameworkRowRects)check(monitor.m_overviewRect.contains(row)&&!row.intersects(monitor.m_copyRect),"framework rows fit without clipping or overlapping actions");
        monitor.m_framework=framework.mid(0,8);monitor.m_framework<<framework.first()<<framework.last();monitor.grab();
        check(monitor.m_frameworkRowRects.size()==10,"ten framework components remain visible");
        for(const auto &row:monitor.m_frameworkRowRects)check(!row.intersects(monitor.m_copyRect)&&!row.intersects(monitor.m_detailsRect),"ten-row framework list reserves its action footer");
        monitor.m_framework=framework;monitor.grab();
        check(monitor.tooltipAt(monitor.m_frameworkRowRects.value(fencesIndex).center()).contains("换出"),"framework hover explains swapped and proportional memory");
        check(monitor.m_processRowRects.size()==2,"wider resource table shows native process rows");
        for(const auto &row:monitor.m_processRowRects)check(!row.intersects(monitor.m_overviewRect),"process rows and local overview do not overlap");
        check(monitor.m_overviewRect.contains(monitor.m_copyRect)&&monitor.m_overviewRect.contains(monitor.m_detailsRect),"snapshot actions fit within the local overview");
        const QString snapshot=monitor.resourceSnapshotText();
        check(snapshot.contains("230.0%")&&snapshot.contains("RSS")&&snapshot.contains("60.0%"),"snapshot preserves multi-core CPU semantics and measured peaks");
        check(!snapshot.contains("DeepSeek")&&!snapshot.contains("unused-legacy-fixture"),"local snapshot contains no provider or credential data");
        monitor.copyResourceSnapshot();check(QApplication::clipboard()->text()==snapshot,"copy action copies the measured local snapshot");
        const QString selected=monitor.resourceSnapshotText(&monitor.m_topProcs.first());check(selected.contains("多核应用")&&!selected.contains("第二应用"),"selected-process copy has the requested scope");
        check(monitor.tooltipAt(monitor.m_processRowRects.first().center()).contains("单核 100%"),"process hover explains its CPU accounting");
        monitor.openResourceSnapshot();settle(80);
        auto *details=monitor.findChild<QDialog *>("monitorResourceSnapshot");check(details&&details->isVisible(),"local detail window opens");
        auto *text=details->findChild<QPlainTextEdit *>("monitorSnapshotText");check(text&&text->isReadOnly()&&text->toPlainText()==snapshot,"details display an immutable readable snapshot");
        details->close();settle(80);
        QWidget settingsHost;auto *page=monitor.createSettingsPage(&settingsHost);
        check(!page->findChild<QWidget *>("monitorApiKey")&&!page->findChild<QWidget *>("monitorApiModel"),"monitor settings have no removed model or credential controls");
        auto *sort=page->findChild<QComboBox *>("monitorSortBy");auto *interval=page->findChild<QSpinBox *>("monitorSampleInterval");
        auto *frameworkSetting=page->findChild<QCheckBox *>("monitorFramework");
        check(frameworkSetting&&frameworkSetting->isChecked(),"framework monitoring is included in Fences settings");
        frameworkSetting->setChecked(false);
        check(sort&&interval&&page->findChild<QCheckBox *>("monitorCompact"),"sorting, refresh cadence and compact view are configurable");
        sort->setCurrentIndex(1);interval->setValue(7);page->findChild<QLineEdit *>("monitorTitle")->setText("资源观测");
        page->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();settle(100);settings.sync();
        check(settings.value("systemMonitor/sortByMemory").toBool()&&settings.value("systemMonitor/statInterval").toInt()==7&&settings.value("systemMonitor/title").toString()=="资源观测","monitor settings persist together through the Fences apply form");
        SystemMonitor restored;restored.m_timer.stop();check(restored.m_sortByMemory&&restored.m_statIntervalSec==7,"recreated monitor restores resource preferences");
        check(!restored.m_showFramework&&!settings.value("systemMonitor/showFramework").toBool(),"framework visibility persists across restart");
        frameworkSetting->setChecked(true);page->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();settle(100);
        monitor.hide();check(!monitor.m_timer.isActive(),"hidden monitor stops sampling");monitor.show();check(monitor.m_timer.isActive(),"showing monitor resumes sampling");monitor.m_timer.stop();
        QDir().mkpath("artifacts/monitor-resources");monitor.grab().save("artifacts/monitor-resources/expanded.png");
        monitor.setCompact(true);monitor.grab().save("artifacts/monitor-resources/compact.png");
        return failures ? 1 : 0;
    }
};
