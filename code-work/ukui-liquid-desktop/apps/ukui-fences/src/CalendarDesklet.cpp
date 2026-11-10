#include "LiquidDialog.h"
#include "CalendarDesklet.h"
#include "DesktopCanvas.h"
#include "LiquidPopup.h"
#include "LiquidWheelPicker.h"
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QCoreApplication>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QProcess>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QMenu>
#include <QWidgetAction>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextBoundaryFinder>
#include <QTextDocument>
#include <algorithm>

namespace {
constexpr int CollapsedHeight=270, ExpandedHeight=464;
constexpr int SummaryTop=234, SummaryHeight=28, AgendaTop=269, FooterHeight=34;
const QSize SummaryIconSize(12,14);
constexpr int ItemRole=Qt::UserRole+1, CompletedRole=Qt::UserRole+2;
const QString CompletedSetting=QStringLiteral("desklets/calendar/completed");

QString calendarScript(){
    const QString installed=QCoreApplication::applicationDirPath()+"/calendar_snapshot.py";
    return QFileInfo::exists(installed)?installed:QString::fromUtf8(UKUI_FENCES_CALENDAR_READER_PATH);
}
QDate itemDate(const QJsonObject &item,const QString &key="date"){
    return QDate::fromString(item.value(key).toString(),Qt::ISODate);
}
bool covers(const QJsonObject &item,const QDate &date){
    return itemDate(item)<=date && itemDate(item,"endDate")>=date;
}
QString shortLine(const QString &line,int limit){
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,line);
    int keep=0;
    for(int i=0;i<limit;++i){
        const int end=finder.toNextBoundary();
        if(end<0 || end==line.size())return line;
        if(i+1<limit)keep=end;
    }
    return line.left(keep)+QStringLiteral("…");
}
QIcon summaryIcon(bool pending,qreal dpr){
    QPixmap mark(SummaryIconSize*dpr);mark.setDevicePixelRatio(dpr);mark.fill(Qt::transparent);
    QPainter p(&mark);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(pending?QColor("#ffcf78"):QColor("#a1b7c7"),1.8,Qt::SolidLine,Qt::RoundCap));
    p.drawLine(QPointF(6,3),QPointF(6,8));
    p.drawPoint(QPointF(6,11));p.end();
    return QIcon(mark);
}
int pickDateValue(QWidget *,QPushButton *anchor,const QString &name,int current,int minimum,int maximum,const QString &suffix){
    return LiquidWheelPicker::pick(anchor,name,current,minimum,maximum,[suffix](int n){return QString::number(n)+" "+suffix;});
}
class AgendaDelegate final : public QStyledItemDelegate {
public:
    explicit AgendaDelegate(QObject *parent):QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override{return QSize(100,48);}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        p->save();p->setRenderHint(QPainter::Antialiasing);p->setClipRect(option.rect);
        if(option.state & (QStyle::State_Selected|QStyle::State_MouseOver)){
            p->setPen(Qt::NoPen);p->setBrush(QColor(154,232,219,35));p->drawRoundedRect(option.rect.adjusted(2,1,-2,-1),7,7);
        }
        const QStringList lines=index.data().toString().split('\n');
        QFont f=option.font;f.setPixelSize(10);p->setFont(f);p->setPen(QColor("#b8cbd6"));
        const QRect line=option.rect.adjusted(7,3,-7,-25);
        p->drawText(line,Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(f).elidedText(lines.value(0),Qt::ElideRight,line.width()));
        const bool done=index.data(CompletedRole).toBool();
        f.setPixelSize(12);f.setStrikeOut(done);p->setFont(f);p->setPen(done?QColor("#a1b5c1"):QColor("#f4f7ff"));
        const QRect title=option.rect.adjusted(7,22,-7,-3);
        p->drawText(title,Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(f).elidedText(lines.mid(1).join(" "),Qt::ElideRight,title.width()));p->restore();
    }
};
}

CalendarDesklet::CalendarDesklet(DesktopCanvas *canvas):LiquidDesklet(canvas,"calendar","日历 · 系统待办",QSize(400,CollapsedHeight)),
    m_selected(QDate::currentDate()),m_month(m_selected.year(),m_selected.month(),1),m_today(m_selected),
    m_reader(new QProcess(this)),m_watcher(new QFileSystemWatcher(this)) {
    m_database=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/.kylin_calendar_Schedule2.db";
    m_previous=button("‹","calendarPrevious");m_next=button("›","calendarNext");m_todayButton=button("今天","calendarToday");
    m_all=button("全部","calendarAll");m_open=button("系统日历","calendarOpen");
    m_yearButton=button("","calendarYear");m_monthButton=button("","calendarMonth");
    m_collapse=button("−","calendarCollapse");
    m_summary=button("今日待办","calendarSummary");
    m_summary->setIconSize(SummaryIconSize);
    QFont summaryFont=m_summary->font();summaryFont.setPixelSize(10);m_summary->setFont(summaryFont);
    m_summary->setAccessibleName("今日待办");
    m_summary->setStyleSheet(
        "QPushButton{color:#cbd7e6;text-align:left;padding:0 3px;background:transparent;"
        "border:1px solid transparent;border-radius:5px;}"
        "QPushButton:hover{color:#f4f7ff;background:rgba(220,236,255,18);}"
        "QPushButton:focus{border-color:rgba(220,236,255,70);}"
        "QPushButton:pressed{background:rgba(220,236,255,34);}");
    m_complete=button("完成","calendarComplete");m_edit=button("编辑","calendarEdit");m_delete=button("删除","calendarDelete");
    connect(m_complete,&QPushButton::clicked,this,&CalendarDesklet::toggleComplete);
    connect(m_edit,&QPushButton::clicked,this,&CalendarDesklet::editSelected);
    connect(m_delete,&QPushButton::clicked,this,&CalendarDesklet::deleteSelected);
    m_yearButton->setToolTip("选择年份");m_monthButton->setToolTip("选择月份");
    connect(m_yearButton,&QPushButton::clicked,this,&CalendarDesklet::showYearMenu);
    connect(m_monthButton,&QPushButton::clicked,this,&CalendarDesklet::showMonthMenu);
    connect(m_collapse,&QPushButton::clicked,this,[this]{setAgendaCollapsed(!m_agendaCollapsed,true);});
    connect(m_summary,&QPushButton::clicked,this,[this]{
        if(m_agendaCollapsed){m_allDates=true;updateList();m_list->scrollToTop();}
        setAgendaCollapsed(!m_agendaCollapsed,true);
    });
    m_previous->setToolTip("上个月");m_next->setToolTip("下个月");m_all->setToolTip("显示本月及今天附近的系统待办");
    m_list=new QListWidget(this);m_list->setObjectName("calendarAgenda");m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new AgendaDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet("QListWidget{background:transparent;color:#edf5fa;border:0;font-size:12px;}QListWidget::item{padding:6px;border-radius:7px;}QListWidget::item:selected{background:rgba(154,232,219,35);}QListWidget::item:hover{background:rgba(154,232,219,20);}QScrollBar:vertical{width:5px;background:transparent;}QScrollBar::handle:vertical{background:rgba(210,235,241,80);border-radius:2px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    connect(m_previous,&QPushButton::clicked,this,[this]{selectDate(m_month.addMonths(-1));});
    connect(m_next,&QPushButton::clicked,this,[this]{selectDate(m_month.addMonths(1));});
    connect(m_todayButton,&QPushButton::clicked,this,[this]{selectDate(QDate::currentDate());});
    connect(m_all,&QPushButton::clicked,this,[this]{m_allDates=true;updateList();m_list->scrollToTop();update();});
    connect(m_open,&QPushButton::clicked,this,[this]{openSystemCalendar(m_selected);});
    connect(m_list,&QListWidget::currentItemChanged,this,[this]{updateActions();update();});
    connect(m_list,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){const QDate d=item->data(Qt::UserRole).toDate();if(d.isValid())openSystemCalendar(d);});
    m_debounce.setSingleShot(true);m_debounce.setInterval(180);connect(&m_debounce,&QTimer::timeout,this,&CalendarDesklet::reload);
    auto changed=[this](const QString &){rewatch();m_debounce.start();};
    connect(m_watcher,&QFileSystemWatcher::fileChanged,this,changed);
    // Only react to database creation/replacement, not unrelated config writes.
    connect(m_watcher,&QFileSystemWatcher::directoryChanged,this,[this](const QString &){
        const QStringList files=m_watcher->files();
        for(const QString &p:{m_database,m_database+"-wal"})if(QFileInfo::exists(p)!=files.contains(p)){rewatch();m_debounce.start();break;}
    });
    m_timeout.setSingleShot(true);m_timeout.setInterval(3000);connect(&m_timeout,&QTimer::timeout,m_reader,&QProcess::kill);
    connect(m_reader,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int exit,QProcess::ExitStatus status){
        m_timeout.stop();QJsonParseError error;const auto document=QJsonDocument::fromJson(m_reader->readAllStandardOutput(),&error);
        if(exit==0 && status==QProcess::NormalExit && error.error==QJsonParseError::NoError && document.isObject()){
            m_items=document.object()["items"].toArray();m_warning=document.object()["warning"].toString();
            m_dates=document.object()["dates"].toObject();m_calendarWarning=document.object()["calendarWarning"].toString();
        }else m_warning="系统待办读取失败，保留上次内容";
        rewatch();updateList();updateSummary();if(isVisible())update();
        if(m_again){m_again=false;m_debounce.start();}
    });
    connect(m_reader,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){m_timeout.stop();m_warning="无法启动日历读取程序";updateList();updateSummary();update();}});
    m_dayTimer.setInterval(60000);connect(&m_dayTimer,&QTimer::timeout,this,[this]{
        const QDate now=QDate::currentDate();if(now!=m_today){bool followed=m_selected==m_today;m_today=now;if(followed){m_selected=now;m_month=QDate(now.year(),now.month(),1);}reload();updateDateButtons();updateList();updateSummary();update();}
    });
    QSettings settings;
    const QStringList completed=settings.value(CompletedSetting).toStringList();
    for(const auto &key:completed)m_completed.insert(key);
    const bool collapsed=settings.value("desklets/calendar/agendaCollapsed",true).toBool();
    m_expandedHeight=qBound(ExpandedHeight,(collapsed?settings.value("desklets/calendar/expandedHeight",ExpandedHeight).toInt():height()),700);
    m_agendaCollapsed=collapsed;setAgendaCollapsed(collapsed);
    updateDateButtons();updateSummary();updateActions();rewatch();arrangeControls();reload();
}
CalendarDesklet::~CalendarDesklet(){
    if(!m_agendaCollapsed)QSettings().setValue("desklets/calendar/expandedHeight",height());
    for(auto *process:{m_holidaySync,m_reader,m_writer}){
        if(!process || process->state()==QProcess::NotRunning)continue;
        disconnect(process,nullptr,this,nullptr);process->kill();process->waitForFinished(500);
    }
}
void CalendarDesklet::rewatch(){
    QStringList wanted;for(const QString &p:{QFileInfo(m_database).absolutePath(),m_database,m_database+"-wal"})if(QFileInfo::exists(p))wanted<<p;
    const auto existing=m_watcher->files()+m_watcher->directories();for(const QString &p:wanted)if(!existing.contains(p))m_watcher->addPath(p);
}
void CalendarDesklet::reload(){
    if(m_reader->state()!=QProcess::NotRunning){m_again=true;return;}
    const QDate begin=qMin(m_month,QDate::currentDate().addDays(-30));
    const QDate end=qMax(m_month.addMonths(1).addDays(-1),QDate::currentDate().addDays(90));
    // Lunar metadata stays bounded for distant navigation; the reader always
    // includes today's agenda independently of the month being browsed.
    const bool distant=begin.daysTo(end)>400;
    m_reader->start("/usr/bin/python3",{calendarScript(),"--database",m_database,
        "--begin",(distant?m_month:begin).toString(Qt::ISODate),
        "--end",(distant?m_month.addMonths(1).addDays(-1):end).toString(Qt::ISODate),
        "--agenda-today",QDate::currentDate().toString(Qt::ISODate)});m_timeout.start();
}
void CalendarDesklet::selectDate(QDate d){if(!d.isValid())return;const bool month=d.month()!=m_month.month() || d.year()!=m_month.year();m_selected=d;m_month=QDate(d.year(),d.month(),1);m_allDates=false;if(month)reload();updateDateButtons();updateList();updateSummaryToolTip();update();}
QRect CalendarDesklet::gridRect() const{return QRect(width()*.34,60,width()-int(width()*.34)-16,164);}
QRect CalendarDesklet::dateCell(int day) const{
    const QRect grid=gridRect();const int offset=m_month.dayOfWeek()-1,index=offset+day-1,weeks=(offset+m_month.daysInMonth()+6)/7;
    const int left=grid.x()+index%7*grid.width()/7,right=grid.x()+(index%7+1)*grid.width()/7;
    const int top=grid.y()+index/7*grid.height()/weeks,bottom=grid.y()+(index/7+1)*grid.height()/weeks;
    return QRect(left,top,right-left,bottom-top);
}
void CalendarDesklet::arrangeControls(){
    if(!m_list)return;
    m_previous->setGeometry(16,16,28,25);m_next->setGeometry(width()-78,16,28,25);
    m_yearButton->setGeometry(width()/2-92,16,96,25);m_monthButton->setGeometry(width()/2+10,16,66,25);
    m_collapse->setGeometry(width()-44,16,28,25);
    m_todayButton->setGeometry(32,196,66,26);
    const int summaryLeft=gridRect().left()+12;
    m_summary->setGeometry(summaryLeft,SummaryTop,width()-summaryLeft-16,SummaryHeight);
    m_all->setGeometry(width()-160,AgendaTop+6,48,24);m_open->setGeometry(width()-105,AgendaTop+6,89,24);
    const int listTop=AgendaTop+37, actionsTop=height()-60;
    m_list->setGeometry(12,listTop,width()-24,qMax(0,actionsTop-listTop-3));
    m_complete->setGeometry(width()-204,actionsTop,66,24);
    m_edit->setGeometry(width()-130,actionsTop,52,24);
    m_delete->setGeometry(width()-70,actionsTop,54,24);
}
void CalendarDesklet::updateDateButtons(){
    m_yearButton->setText(QString::number(m_month.year())+" 年 ▾");
    m_monthButton->setText(QString::number(m_month.month())+" 月 ▾");
}
void CalendarDesklet::setAgendaCollapsed(bool collapsed, bool animate){
    if(collapsed && !m_agendaCollapsed && !drawerRunning())m_expandedHeight=qBound(ExpandedHeight,height(),700);
    m_agendaCollapsed=collapsed;
    m_collapse->setText(collapsed?"＋":"−");
    m_collapse->setToolTip(collapsed?"展开系统待办":"收起系统待办");m_collapse->setAccessibleName(m_collapse->toolTip());
    QSettings settings;settings.setValue("desklets/calendar/agendaCollapsed",collapsed);
    settings.setValue("desklets/calendar/expandedHeight",m_expandedHeight);settings.sync();
    updateSummaryToolTip();
    const QWidgetList agenda={m_list,m_all,m_open,m_complete,m_edit,m_delete};
    auto settle=[this,collapsed,agenda]{
        // Keep a separate expanded height so repeated toggles and restarts restore it.
        setMinimumHeight(collapsed?CollapsedHeight:ExpandedHeight);setMaximumHeight(collapsed?CollapsedHeight:700);
        for(QWidget *w:agenda)w->setVisible(!collapsed);
        savePlacement();
    };
    if(!animate || !isVisible()){
        setMinimumHeight(collapsed?CollapsedHeight:ExpandedHeight);setMaximumHeight(collapsed?CollapsedHeight:700);
        resize(width(),collapsed?CollapsedHeight:m_expandedHeight);
        move(x(),qBound(0,y(),qMax(0,m_canvas->height()-height())));
        settle();arrangeControls();update();return;
    }
    // The agenda slides like a fence drawer under the month grid.
    runDrawer(!collapsed,CollapsedHeight,m_expandedHeight,AgendaTop,FooterHeight,agenda,settle);
}
void CalendarDesklet::showMonthMenu(){
    const int month=pickDateValue(this,m_monthButton,"calendarMonth",m_month.month(),1,12,"月");
    if(month==m_month.month())return;
    const QDate first(m_month.year(),month,1);
    selectDate(QDate(first.year(),first.month(),qMin(m_selected.day(),first.daysInMonth())));
}
void CalendarDesklet::showYearMenu(){
    const int year=pickDateValue(this,m_yearButton,"calendarYear",m_month.year(),1900,2100,"年");
    if(year==m_month.year())return;
    const QDate month(year,m_month.month(),1);
    selectDate(QDate(month.year(),month.month(),qMin(m_selected.day(),month.daysInMonth())));
}
void CalendarDesklet::updateList(){
    if(!m_list)return;
    const QString selected=selectedItem().value("id").toString();
    const int scroll=m_list->verticalScrollBar()->value();
    const QSignalBlocker block(m_list);
    m_list->clear();
    QVector<QJsonObject> visible;
    for(const auto &value:m_items){
        const auto item=value.toObject();
        if(m_allDates || covers(item,m_selected))visible.append(item);
    }
    auto group=[this](const QJsonObject &item){
        if(m_completed.contains(item.value("completionKey").toString()))return 3;
        if(covers(item,m_today))return 0;
        return itemDate(item,"endDate")<m_today?1:2;
    };
    std::stable_sort(visible.begin(),visible.end(),[&](const QJsonObject &a,const QJsonObject &b){
        const int ga=group(a),gb=group(b);
        if(ga!=gb)return ga<gb;
        if(ga==1 || ga==3)return itemDate(a)>itemDate(b);
        return false;
    });
    for(const auto &o:visible){
        const QDate date=itemDate(o);
        const bool done=group(o)==3;
        QString prefix=date.toString("MM-dd")+"  "+o.value("time").toString();
        if(done)prefix+=" · 已完成";
        else if(itemDate(o,"endDate")<m_today)prefix+=" · 已过日期";
        auto *item=new QListWidgetItem(prefix+"\n"+o.value("title").toString(),m_list);
        item->setData(Qt::UserRole,m_allDates?date:m_selected);
        item->setData(ItemRole,o);item->setData(CompletedRole,done);
        item->setToolTip(Qt::convertFromPlainText(o.value("title").toString()+"\n单击选中 · 双击在系统日历查看"));
        if(o.value("id").toString()==selected)m_list->setCurrentItem(item);
    }
    if(m_list->count()==0){
        auto *item=new QListWidgetItem(m_warning.isEmpty()?(m_allDates?"暂无系统待办":"这一天没有系统待办"):m_warning,m_list);
        item->setFlags(Qt::NoItemFlags);
    }
    m_list->verticalScrollBar()->setValue(scroll);
    updateActions();
}
void CalendarDesklet::updateSummary(){
    QStringList lines;
    bool anyToday=false;
    for(const auto &value:m_items){
        const auto item=value.toObject();
        if(!covers(item,m_today))continue;
        anyToday=true;
        if(m_completed.contains(item.value("completionKey").toString()))continue;
        const QString title=item.value("title").toString().simplified();
        lines.append(shortLine(item.value("time").toString()+" "+(title.isEmpty()?"未命名":title),10));
        if(lines.size()==2)break;
    }
    const bool pending=!lines.isEmpty();
    if(!pending)lines.append(anyToday?"今日已完成":m_warning.isEmpty()?"今日无待办":"待办请展开");
    m_summary->setText(lines.join('\n'));
    m_summary->setAccessibleDescription(m_summary->text());
    m_summary->setIcon(summaryIcon(pending,devicePixelRatioF()));
    updateSummaryToolTip();
}
QString CalendarDesklet::agendaStatus() const{
    if(!m_warning.isEmpty())return m_warning;
    if(!m_calendarWarning.isEmpty())return m_calendarWarning;
    const auto selected=m_dates.value(m_selected.toString(Qt::ISODate)).toObject();
    if(!selected.isEmpty() && !selected["scheduleKnown"].toBool())
        return QString::number(m_selected.year())+" 年放假调休数据尚未收录";
    return {};
}
void CalendarDesklet::updateSummaryToolTip(){
    QString tip=m_agendaCollapsed?"展开待办 · 选择事项后可完成、编辑或删除":"收起待办";
    const QString status=agendaStatus();
    if(!status.isEmpty())tip+="\n"+status;
    m_summary->setToolTip(tip);
}
QJsonObject CalendarDesklet::selectedItem() const{
    auto *item=m_list?m_list->currentItem():nullptr;
    return item?item->data(ItemRole).toJsonObject():QJsonObject{};
}
void CalendarDesklet::updateActions(){
    const auto item=selectedItem();
    const QString key=item.value("completionKey").toString();
    const bool enabled=!m_writer && !key.isEmpty();
    const bool series=item.value("series").toBool();
    m_complete->setText(m_completed.contains(key)?"恢复":"完成");
    m_complete->setToolTip("完成状态保存在桌面待办");
    m_edit->setToolTip(series?"编辑整组重复事项":"编辑所选事项");
    m_delete->setToolTip(series?"删除整组重复事项":"从系统日历删除所选事项");
    m_complete->setEnabled(enabled);m_edit->setEnabled(enabled);m_delete->setEnabled(enabled);
}
void CalendarDesklet::toggleComplete(){
    const QString key=selectedItem().value("completionKey").toString();
    if(key.isEmpty() || m_writer)return;
    if(!m_completed.remove(key))m_completed.insert(key);
    QStringList completed=m_completed.values();completed.sort();
    QSettings settings;settings.setValue(CompletedSetting,completed);settings.sync();
    if(settings.status()!=QSettings::NoError){
        if(!m_completed.remove(key))m_completed.insert(key);
        LiquidDialog::warning(this,"待办","暂时无法保存完成状态，请稍后重试。");
        return;
    }
    updateList();updateSummary();update();
}
void CalendarDesklet::editSelected(){
    const auto item=selectedItem();
    if(item.isEmpty() || m_writer)return;
    const auto clock=item.value("editableTime").toObject();
    const bool series=item.value("series").toBool(),allDay=item.value("allDay").toBool();
    auto *dialog=new LiquidDialog::Dialog(this);
    dialog->setObjectName("calendarEditDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(series?"编辑重复待办":"编辑待办");dialog->setMinimumWidth(390);
    auto *form=new QFormLayout(dialog);form->setContentsMargins(20,16,20,16);
    auto *title=new QPlainTextEdit(item.value("title").toString(),dialog);
    title->setObjectName("calendarEditTitle");title->setFixedHeight(82);
    form->addRow("事项",title);
    QDateEdit *date=nullptr;
    QTimeEdit *start=nullptr,*end=nullptr;
    if(!clock.isEmpty()){
        date=new QDateEdit(QDate::fromString(clock.value("date").toString(),Qt::ISODate),dialog);
        date->setObjectName("calendarEditDate");date->setDisplayFormat("yyyy-MM-dd");date->setCalendarPopup(true);
        form->addRow("日期",date);
        if(!allDay){
            start=new QTimeEdit(QTime::fromString(clock.value("start").toString(),"HH:mm"),dialog);
            end=new QTimeEdit(QTime::fromString(clock.value("end").toString(),"HH:mm"),dialog);
            start->setObjectName("calendarEditStart");end->setObjectName("calendarEditEnd");
            start->setDisplayFormat("HH:mm");end->setDisplayFormat("HH:mm");
            form->addRow("开始",start);form->addRow("结束",end);
        }
    }else{
        auto *note=new QLabel(series?"内容会更新整组重复事项。时间和重复规则请在系统日历调整。":"此事项的日期和时间请在系统日历调整。",dialog);
        note->setWordWrap(true);form->addRow(note);
    }
    auto *error=new QLabel(dialog);error->setWordWrap(true);error->hide();form->addRow(error);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("保存");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[=]{
        QString message;
        if(title->toPlainText().trimmed().isEmpty())message="请填写事项内容。";
        else if(start && end->time()<start->time())message="结束时间不能早于开始时间。";
        if(!message.isEmpty()){error->setText(message);error->show();return;}
        dialog->accept();
    });
    connect(dialog,&QDialog::accepted,this,[=]{
        QJsonObject changes{{"title",title->toPlainText().trimmed()}};
        if(date)changes["date"]=date->date().toString(Qt::ISODate);
        if(start){changes["start"]=start->time().toString("HH:mm");changes["end"]=end->time().toString("HH:mm");}
        writeItem("edit",item,changes);
    });
    title->setFocus();dialog->open();
}
void CalendarDesklet::deleteSelected(){
    const auto item=selectedItem();
    if(item.isEmpty() || m_writer)return;
    const bool series=item.value("series").toBool();
    const QString title=shortLine(item.value("title").toString().simplified(),100);
    auto *dialog=LiquidDialog::createMessage(this,series?"删除整组重复待办？":"删除待办？",
        item.value("date").toString()+" "+item.value("time").toString()+"\n"+title+"\n\n"+
        (series?"这会从系统日历删除整组重复事项。":"这会从系统日历删除该事项。"),
        QMessageBox::Warning,QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
    dialog->setObjectName("calendarDeleteDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&QDialog::finished,this,[this,item](int result){if(result==QMessageBox::Yes)writeItem("delete",item);});
    dialog->open();
}
void CalendarDesklet::writeItem(const QString &action,const QJsonObject &item,QJsonObject changes){
    if(m_writer)return;
    changes["action"]=action;changes["id"]=item.value("sourceId");changes["version"]=item.value("version");
    const QByteArray payload=QJsonDocument(changes).toJson(QJsonDocument::Compact);
    auto *process=new QProcess(this);m_writer=process;updateActions();
    auto *timeout=new QTimer(process);timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,process,&QProcess::kill);
    auto finish=[this,process,timeout](bool ok){
        if(m_writer!=process)return;
        timeout->stop();m_writer=nullptr;
        const auto reply=QJsonDocument::fromJson(process->readAllStandardOutput());
        if(!ok || !reply.isObject()){
            const QString detail=reply.object().value("error").toString();
            auto *notice=LiquidDialog::createMessage(this,"待办保存失败",
                detail.isEmpty()?"暂时无法保存，请稍后重试。":detail,QMessageBox::Warning);
            notice->setAttribute(Qt::WA_DeleteOnClose);notice->open();
        }
        process->deleteLater();updateActions();rewatch();reload();
    };
    connect(process,&QProcess::started,this,[process,payload]{process->write(payload);process->closeWriteChannel();});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus status){finish(code==0 && status==QProcess::NormalExit);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    process->start("/usr/bin/python3",{calendarScript(),"--database",m_database,"--mutate"});
    timeout->start(3000);
}
void CalendarDesklet::paintContent(QPainter &p){
    if(!m_list)return;const QColor ink=inkColor(),muted=mutedColor(),accent("#9ae8db");
    text(p,QRectF(12,66,width()*.29,55),QString::number(m_selected.day()),42,ink,true);
    text(p,QRectF(12,120,width()*.29,22),QLocale(QLocale::Chinese).dayName(m_selected.dayOfWeek(),QLocale::LongFormat),12,muted);
    const auto selected=m_dates.value(m_selected.toString(Qt::ISODate)).toObject();
    text(p,QRectF(12,144,width()*.29,20),selected["lunar"].toString(),12,accent);
    QString detail=selected["festival"].toString();
    if(selected.contains("off"))detail+=(detail.isEmpty()?selected["holiday"].toString():QString())+(selected["off"].toBool()?" · 休":" · 班");
    if(detail.isEmpty())detail=selected["yearName"].toString();
    text(p,QRectF(12,168,width()*.29,20),detail,10,muted);
    const QRect grid=gridRect();const QStringList labels={"一","二","三","四","五","六","日"};
    for(int col=0;col<7;++col)text(p,QRectF(grid.x()+col*grid.width()/7,42,grid.width()/7,18),labels[col],10,col>=5?accent:muted);
    for(int d=1;d<=m_month.daysInMonth();++d){const QDate date(m_month.year(),m_month.month(),d);const QRect cell=dateCell(d);const QPoint center(cell.center().x(),cell.top()+8);
        const auto info=m_dates.value(date.toString(Qt::ISODate)).toObject();
        if(date==m_selected){p.setPen(Qt::NoPen);p.setBrush(QColor(154,232,219,55));p.drawRoundedRect(QRectF(cell).adjusted(2,1,-2,-1),7,7);}
        if(date==m_today){p.setPen(Qt::NoPen);p.setBrush(accent);p.drawEllipse(QPointF(center),8,8);}
        text(p,QRectF(cell.x(),cell.y(),cell.width(),16),QString::number(d),11,date==m_today?QColor("#203c3a"):ink,date==m_today);
        text(p,QRectF(cell.x(),cell.y()+16,cell.width(),11),info["label"].toString(),9,info["festival"].toString().isEmpty()?muted:accent);
        if(info.contains("off")){
            const bool off=info["off"].toBool();
            text(p,QRectF(cell.right()-9,cell.top(),10,11),off?"休":"班",8,off?QColor("#ffb6a9"):QColor("#f3d39a"));
        }
        bool marked=false;for(const auto &v:m_items){const auto o=v.toObject();if(covers(o,date) && !m_completed.contains(o.value("completionKey").toString())){marked=true;break;}}
        if(marked){p.setPen(Qt::NoPen);p.setBrush(accent);p.drawEllipse(QPointF(cell.left()+3,cell.top()+8),1.4,1.4);}
    }
    if(!m_agendaCollapsed && !drawerCapturing())paintAgendaHeader(p);
    const int footerSplit=m_summary->x();
    text(p,QRectF(16,height()-25,footerSplit-22,18),"休：放假  班：调休上班",10,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    if(!m_agendaCollapsed){
        const QString status=agendaStatus();
        if(!status.isEmpty()){
            QFont small=font();small.setPixelSize(10);
            const int available=width()-footerSplit-16;
            text(p,QRectF(footerSplit,height()-25,available,18),QFontMetrics(small).elidedText(status,Qt::ElideRight,available),
                10,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
        }
    }
}
void CalendarDesklet::paintAgendaHeader(QPainter &p){
    p.setPen(QColor(230,245,250,35));p.drawLine(16,AgendaTop,width()-16,AgendaTop);
    text(p,QRectF(18,AgendaTop+6,width()-184,24),m_allDates?QString("系统待办 · %1 项").arg(m_items.size()):m_selected.toString("M月d日 · 待办"),12,inkColor(),true,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,QRectF(18,height()-60,width()-232,24),m_writer?"正在保存…":"选择事项后操作",10,mutedColor(),false,Qt::AlignLeft|Qt::AlignVCenter);
}
void CalendarDesklet::paintDrawerContent(QPainter &p){paintAgendaHeader(p);}
void CalendarDesklet::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton){for(int d=1;d<=m_month.daysInMonth();++d)if(dateCell(d).contains(e->pos())){selectDate(QDate(m_month.year(),m_month.month(),d));e->accept();return;}}LiquidDesklet::mousePressEvent(e);}
void CalendarDesklet::mouseMoveEvent(QMouseEvent *e){
    LiquidDesklet::mouseMoveEvent(e);QString tip;
    for(int day=1;day<=m_month.daysInMonth();++day)if(dateCell(day).contains(e->pos())){
        const QDate date(m_month.year(),m_month.month(),day);const auto info=m_dates.value(date.toString(Qt::ISODate)).toObject();
        tip=date.toString("yyyy-MM-dd")+"  "+info["yearName"].toString()+info["lunar"].toString();
        if(!info["festival"].toString().isEmpty())tip+="\n"+info["festival"].toString();
        if(info.contains("off"))tip+="\n"+info["holiday"].toString()+(info["off"].toBool()?" 放假":" 调休上班");
        else if(!info.isEmpty() && !info["scheduleKnown"].toBool())tip+="\n本年放假调休数据尚未收录";
        break;
    }
    if(toolTip()!=tip)setToolTip(tip);
}
void CalendarDesklet::showEvent(QShowEvent *e){LiquidDesklet::showEvent(e);m_dayTimer.start();reload();}
void CalendarDesklet::hideEvent(QHideEvent *e){m_dayTimer.stop();LiquidDesklet::hideEvent(e);}
void CalendarDesklet::openSystemCalendar(QDate d){
    auto msg=QDBusMessage::createMethodCall("org.ukui.calendar.Notifications","/org/ukui/calendar/Notifications","org.ukui.calendar.Notifications","OpenSchedule");msg<<d.day()<<d.month()<<d.year();
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1500),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this,w]{QDBusPendingReply<> r=*w;w->deleteLater();if(r.isError()){m_warning="系统日历暂未响应，请从任务栏打开";updateSummaryToolTip();update();}});
}
void CalendarDesklet::syncCalendarData()
{
    if(m_holidaySync) return;
    QString script=QCoreApplication::applicationDirPath()+"/holiday_sync.py";
    if(!QFileInfo::exists(script)) script=QFileInfo(QString::fromUtf8(UKUI_FENCES_CALENDAR_READER_PATH)).dir().filePath("holiday_sync.py");
    auto *process=new QProcess(this);m_holidaySync=process;
    m_calendarWarning="正在同步农历和节假日…";updateSummaryToolTip();update();
    auto *timeout=new QTimer(process);timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,process,&QProcess::kill);
    auto finish=[this,process,timeout](bool started) {
        if(m_holidaySync!=process) return;
        timeout->stop();m_holidaySync=nullptr;
        const auto result=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        const QString message=result["message"].toString();
        m_calendarWarning=message.isEmpty()?"同步失败，保留原农历与节假日数据":message;
        updateSummaryToolTip();reload();update();
        auto *notice=LiquidDialog::createMessage(this,"农历和节假日同步",
            message.isEmpty() ? (started?"网络或数据源暂不可用，原数据已保留。":"无法启动同步程序，原数据已保留。")
                              : "农历按系统 ICU 重新计算。\n"+message,
            QMessageBox::Information,QMessageBox::Ok);
        notice->setAttribute(Qt::WA_DeleteOnClose);notice->open();process->deleteLater();
    };
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int,QProcess::ExitStatus){finish(true);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(false);});
    const int current=QDate::currentDate().year();
    process->start("/usr/bin/python3",{script,"--year",QString::number(m_selected.year()),"--year",QString::number(current),"--year",QString::number(current+1)});
    timeout->start(30000);
}
void CalendarDesklet::extendMenu(QMenu &menu)
{
    connect(menu.addAction("打开系统日历"),&QAction::triggered,this,[this]{openSystemCalendar(m_selected);});
    connect(menu.addAction("刷新系统待办"),&QAction::triggered,this,&CalendarDesklet::reload);
    auto *sync=menu.addAction(m_holidaySync?"正在同步农历和节假日…":"同步最新农历和节假日");
    sync->setObjectName("calendarSyncHolidays");sync->setEnabled(!m_holidaySync);
    sync->setToolTip("重新计算农历，从 holiday-cn 同步当前、所选及下一年的已发布调休数据；失败保留原数据。");
    connect(sync,&QAction::triggered,this,&CalendarDesklet::syncCalendarData);
}
