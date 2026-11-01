#include "ActivityRecorder.h"
#include "LiquidDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString duration(qint64 ms){const auto seconds=ms/1000;return seconds>=3600?QString("%1时%2分").arg(seconds/3600).arg(seconds/60%60):seconds>=60?QString::number(seconds/60)+"分":QString::number(seconds)+"秒";}
QString percent(double v){return v<0?QStringLiteral("—"):QString::number(v,'f',1)+"%";}
QString memory(double v){return v<0?QStringLiteral("—"):v>=1024?QString::number(v/1024,'f',2)+" GB":QString::number(v,'f',1)+" MB";}
class Details final:public LiquidDialog::Dialog {
public:
    Details(ActivityRecorder *recorder,QWidget *parent):LiquidDialog::Dialog(parent),recorder(recorder) {
        setAttribute(Qt::WA_DeleteOnClose);setObjectName("activityDetails");setWindowTitle("应用使用与资源记录");
        const QSize area=QGuiApplication::primaryScreen()->availableGeometry().size()-QSize(24,24);resize(QSize(880,480).boundedTo(area));
        auto *layout=new QVBoxLayout(this);auto *filters=new QHBoxLayout;
        date=new QComboBox(this);date->setObjectName("activityHistoryDate");date->addItem("今天",QDate());
        app=new QComboBox(this);app->setObjectName("activityHistoryApp");filters->addWidget(date);filters->addWidget(app,1);layout->addLayout(filters);
        table=new QTableWidget(0,8,this);table->setObjectName("activityHistoryTable");table->setHorizontalHeaderLabels({"应用","前台停留","CPU 均值","CPU 峰值","内存均值","内存峰值","采样时长","样本"});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->verticalHeader()->hide();table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(table,1);
        auto *explanation=new QLabel("资源只采样今日前五应用可关联的窗口进程及子进程。CPU 按单核 100% 计；RSS 共享页可能重复。均值仅覆盖已采样的运行时段，旧记录没有资源数据。",this);explanation->setWordWrap(true);layout->addWidget(explanation);
        state=new QLabel(this);state->setWordWrap(true);layout->addWidget(state);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);buttons->button(QDialogButtonBox::Close)->setText("关闭");auto *save=buttons->addButton("导出所选日期 CSV",QDialogButtonBox::ActionRole);save->setObjectName("activityExportCsv");
        auto *logs=buttons->addButton("打开指标日志",QDialogButtonBox::ActionRole);logs->setObjectName("activityOpenLogs");layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
        connect(save,&QPushButton::clicked,this,[this]{
            if(!this->recorder)return;const QDate selected=selectedDate();const auto csv=QString(QChar(0xfeff))+this->recorder->csvForDay(selected);const QPointer<Details> guard(this);
            const QString path=LiquidDialog::getSaveFileName(this,"导出应用记录",QDir::homePath()+"/应用记录-"+selected.toString(Qt::ISODate)+".csv","CSV 文件 (*.csv)");
            if(!guard||path.isEmpty())return;QSaveFile file(path);const auto bytes=csv.toUtf8();
            if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())LiquidDialog::warning(this,"导出失败",file.errorString());
        });
        connect(logs,&QPushButton::clicked,this,[this]{if(this->recorder){QDir().mkpath(this->recorder->metricsDirectory());QDesktopServices::openUrl(QUrl::fromLocalFile(this->recorder->metricsDirectory()));}});
        connect(date,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{refresh();});connect(app,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{refreshTable();});
        connect(recorder,&ActivityRecorder::changed,this,[this]{if(isVisible())refresh();});connect(recorder,&QObject::destroyed,this,&QObject::deleteLater);refresh();
    }
    void selectApp(const QString &id){if(id.isEmpty())return;const int index=app->findData(id);if(index>=0)app->setCurrentIndex(index);}
private:
    QDate selectedDate() const{const QDate value=date->currentData().toDate();return value.isValid()?value:QDate::currentDate();}
    void refresh() {
        if(!recorder)return;
        {QSignalBlocker block(date);const auto selected=date->currentData();date->clear();date->addItem("今天",QDate());const auto days=recorder->ledger().days.keys();for(auto it=days.crbegin();it!=days.crend();++it)if(*it!=QDate::currentDate())date->addItem(it->toString(Qt::ISODate),*it);date->setCurrentIndex(qMax(0,date->findData(selected)));}
        {QSignalBlocker block(app);const auto selected=app->currentData();app->clear();app->addItem("全部应用",QString());const auto apps=recorder->ledger().days.value(selectedDate()).apps;for(auto it=apps.cbegin();it!=apps.cend();++it)app->addItem(it->name,it.key());app->setCurrentIndex(qMax(0,app->findData(selected)));}
        refreshTable();state->setText(QString("汇总保留 60 天 · 指标日志保留 %1 天 · %2").arg(recorder->logRetentionDays()).arg(recorder->metricsEnabled()?QString("每 %1 秒采样，约每分钟写入本机日志").arg(recorder->metricsIntervalSeconds()):"资源采样已关闭"));
    }
    void refreshTable() {
        if(!recorder)return;const int scroll=table->verticalScrollBar()->value();const auto apps=recorder->ledger().days.value(selectedDate()).apps;
        auto ids=apps.keys();std::sort(ids.begin(),ids.end(),[&](const QString &a,const QString &b){return apps[a].ms>apps[b].ms;});table->setRowCount(0);
        for(const auto &id:ids) {
            if(!app->currentData().toString().isEmpty()&&app->currentData().toString()!=id)continue;
            const auto value=apps.value(id);const auto &r=value.resources;const QStringList cells{value.name,duration(value.ms),percent(r.cpuMean()),percent(r.cpuPeak),memory(r.rssMeanMb()),memory(r.rssPeakMb),r.observedMs?duration(r.observedMs):"—",QString::number(r.samples)};
            const int row=table->rowCount();table->insertRow(row);for(int column=0;column<cells.size();++column){auto *item=new QTableWidgetItem(cells[column]);item->setData(Qt::UserRole,id);if(column)item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);table->setItem(row,column,item);}
        }
        table->verticalScrollBar()->setValue(scroll);
    }
    QPointer<ActivityRecorder> recorder;QComboBox *date=nullptr,*app=nullptr;QTableWidget *table=nullptr;QLabel *state=nullptr;
};
}
void ActivityRecorder::showDetails(QWidget *parent,const QString &appId) {
    if(!m_details)m_details=new Details(this,parent);
    static_cast<Details *>(m_details.data())->selectApp(appId);m_details->showNormal();m_details->raise();m_details->activateWindow();
}
