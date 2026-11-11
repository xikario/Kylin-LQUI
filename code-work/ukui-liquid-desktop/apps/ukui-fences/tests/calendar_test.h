#ifdef KeyPress
#undef KeyPress
#endif
#include "CalendarDesklet.h"
#include <QListWidget>
#include <QProcess>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QFileSystemWatcher>
#include <QSignalBlocker>
static int runCalendarTest(const QString &root){
    QSettings settings;settings.setValue("smartSpace/autoStart",false);settings.setValue("systemMonitor/autoStart",false);settings.setValue("desklets/calendar/agendaCollapsed",false);
    settings.setValue("desklets/calendar/completed",QStringList{QStringLiteral("[\"old\"]")});settings.sync();
    const QString database=root+"/config/.kylin_calendar_Schedule2.db";QDir().mkpath(root+"/config");
    auto sql=[&](const QString &statement){return QProcess::execute("/usr/bin/python3",{"-c","import sqlite3,sys; c=sqlite3.connect(sys.argv[1]);c.executescript(sys.argv[2]);c.commit();c.close()",database,statement})==0;};
    check(sql("CREATE TABLE Schedule(id,start_date,end_date,start_hour,start_minute,descript,repeat,isAllDay,isLunar,beginrepeat,endrepeat);"),"isolated native calendar database created");
    const QString today=QDate::currentDate().toString(Qt::ISODate);
    check(sql(QString("INSERT INTO Schedule VALUES('one','%1','%1','17','30','系统待办测试','不重复',0,0,'','');").arg(today)),"native schedule fixture inserted");
    {
        DesktopCanvas canvas;bool wallpaperReady=false;
        QObject::connect(&canvas,&DesktopCanvas::initialWallpaperReady,&canvas,[&]{wallpaperReady=true;});
        canvas.show();canvas.setCalendarWidgetVisible(true);
        for(int waited=0;waited<5000 && !wallpaperReady;waited+=20)settle(20);
        settle(100);
        auto *calendar=canvas.findChild<CalendarDesklet *>();
        for(int waited=0;calendar && waited<3000 && calendar->scheduleCount()!=1;waited+=50)settle(50);
        check(calendar && calendar->scheduleCount()==1,"calendar displays existing system schedules");if(!calendar)return 1;
        auto *list=calendar->findChild<QListWidget *>("calendarAgenda");check(list && list->count()==1 && list->item(0)->text().contains("系统待办测试"),"agenda shows native task title and time");
        canvas.setClockWidgetVisible(true);canvas.setActivityWidgetVisible(true);canvas.setMusicWidgetVisible(true);settle(100);
        check(canvas.findChildren<QPushButton *>("deskletClose").isEmpty(),"all new desklets have no close X button");
        bool foundSync=false;
        QTimer::singleShot(80,calendar,[&]{
            auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if(menu) {auto *action=menu->findChild<QAction *>("calendarSyncHolidays");
                foundSync=action && menu->actions().contains(action) && action->isEnabled();menu->close();}
        });
        QContextMenuEvent menuEvent(QContextMenuEvent::Mouse,QPoint(20,20),calendar->mapToGlobal(QPoint(20,20)));
        QApplication::sendEvent(calendar,&menuEvent);
        check(foundSync,"calendar context menu exposes enabled holiday synchronization action");
        const int builds=calendar->materialBuilds();
        check(sql(QString("INSERT INTO Schedule VALUES('two','%1','%1','18','0','新增待办','不重复',0,0,'','');").arg(today)),"external system calendar update written");
        for(int waited=0;waited<3000 && calendar->scheduleCount()!=2;waited+=50)settle(50);
        check(calendar->scheduleCount()==2 && calendar->materialBuilds()==builds,"database watcher updates tasks without rebuilding glass");
        QFile::copy(database,database+".replacement");
        check(sql("DELETE FROM Schedule WHERE id='two';"),"native deletion fixture applied");
        // Reload is debounced and read by an async helper process; poll instead of a fixed sleep.
        for(int waited=0;waited<3000 && calendar->scheduleCount()!=1;waited+=50)settle(50);
        check(calendar->scheduleCount()==1,"native calendar deletion is reflected");
        QFile::remove(database);QFile::rename(database+".replacement",database);
        for(int waited=0;waited<3000 && calendar->scheduleCount()!=2;waited+=50)settle(50);
        check(calendar->scheduleCount()==2,"atomic database replacement reattaches the file watch");
        calendar->selectDate(QDate::currentDate().addDays(1));check(!list->item(0)->data(Qt::UserRole).isValid(),"date selection filters native schedules");
        calendar->findChild<QPushButton *>("calendarAll")->click();check(list->count()==2,"all tasks view restores the native agenda");
        calendar->selectDate(QDate(2026,9,25));settle(500);
        // Five-row month: inspect lunar/holiday tooltip on Mid-Autumn.
        QMouseEvent hover(QEvent::MouseMove,QPointF(294,174),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(calendar,&hover);
        check(calendar->toolTip().contains("八月十五") && calendar->toolTip().contains("放假"),"lunar festival and official holiday reach the calendar UI");
        const QString output=QDir::currentPath()+"/../test-results/calendar-"+QString::number(calendar->devicePixelRatioF());QDir().mkpath(output);
        calendar->grab().save(output+"/lunar-september.png");
        calendar->selectDate(QDate(2026,8,1));settle(500);calendar->grab().save(output+"/lunar-six-weeks.png");
        calendar->selectDate(QDate::currentDate());settle(500);
        for(auto *widget:canvas.findChildren<LiquidDesklet *>())widget->grab().save(output+"/"+widget->objectName()+".png");
        auto *collapse=calendar->findChild<QPushButton *>("calendarCollapse");
        calendar->resize(calendar->width(),520);settle(400);const QPoint expandedPos=calendar->pos();
        auto awaitHeight=[&](int target){for(int w=0;w<2000 && (calendar->height()!=target || calendar->drawerRunning());w+=20)settle(20);settle(200);};
        int drawerBuilds=calendar->materialBuilds();
        collapse->click();settle(140);calendar->grab().save(output+"/collapsing.png");
        check(calendar->drawerRunning() && calendar->height()>270 && calendar->height()<520,"collapse animates like a fence drawer");
        awaitHeight(270);
        check(calendar->materialBuilds()-drawerBuilds<=1,"collapse rebuilds glass at most once");
        check(calendar->height()==270 && !list->isVisible() && !calendar->findChild<QPushButton *>("calendarAll")->isVisible(),"collapse hides agenda and shrinks card");
        check(calendar->pos()==expandedPos && calendar->minimumHeight()==270,"collapse preserves position");
        auto *summary=calendar->findChild<QPushButton *>("calendarSummary");
        check(summary && summary->isVisible() && summary->x()>calendar->width()/3 &&
              summary->y()>224 && calendar->rect().contains(summary->geometry()),
              "compact reminder fits below the grid on the right without growing the card");
        if(!summary)return 1;
        const auto preview=summary->text().split('\n');
        check(preview.size()==2 && preview[0].startsWith("17:30 ") && preview[1].startsWith("18:00 ") &&
              preview[0].size()<=10 && preview[1].size()<=10,"two today lines include time and stay within ten characters each");
        calendar->grab().save(output+"/collapsed.png");

        const auto tomorrow=QDate::currentDate().addDays(1);
        auto clickDate=[&](QDate date){
            const QDate first(date.year(),date.month(),1);
            const QDate current=calendar->selectedDate();
            if(current.year()!=date.year() || current.month()!=date.month())calendar->selectDate(first);
            const int offset=first.dayOfWeek()-1, index=offset+date.day()-1;
            const int weeks=(offset+date.daysInMonth()+6)/7;
            const int gridLeft=int(calendar->width()*.34), gridWidth=calendar->width()-gridLeft-16;
            const QPointF center(gridLeft+(index%7+.5)*gridWidth/7,60+(index/7+.5)*164/weeks);
            QMouseEvent event(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(calendar,&event);
        };
        clickDate(tomorrow);
        check(calendar->selectedDate()==tomorrow && summary->text()==tomorrow.toString("d日无待办"),
              "clicking a date changes the compact summary even with the drawer closed");
        auto *watcher=calendar->findChild<QFileSystemWatcher *>();
        if(!watcher)return 1;
        {
            // Drop filesystem notifications to exercise the visible-card
            // fallback, as opposed to the normal watcher test above.
            const QSignalBlocker missedSignals(watcher);
            const QString iso=tomorrow.toString(Qt::ISODate);
            check(sql(QString("INSERT INTO Schedule VALUES('tomorrow','%1','%1','9','45','跨日待办','不重复',0,0,'','');").arg(iso)),
                  "external calendar commits a task for the selected date");
            QElapsedTimer sync;sync.start();
            while(sync.elapsed()<3000 && summary->text()!="09:45 跨日待办")settle(30);
            check(summary->text()=="09:45 跨日待办" && calendar->height()==270 && sync.elapsed()<3000,
                  "external task reaches the collapsed selected-date preview within three seconds without a watcher signal");
            qInfo()<<"Calendar missed-notification refresh in"<<sync.elapsed()<<"ms";
        }
        calendar->findChild<QPushButton *>("calendarToday")->click();
        check(summary->text().split('\n')==preview,"Today brings back today's preview");
        clickDate(tomorrow);
        check(summary->text()=="09:45 跨日待办","clicking back to the date immediately shows its loaded task");
        calendar->grab().save(output+"/selected-date.png");
        drawerBuilds=calendar->materialBuilds();
        summary->click();awaitHeight(520);
        check(calendar->materialBuilds()-drawerBuilds<=1,"expand rebuilds glass at most once");
        check(calendar->height()==520 && list->isVisible() && list->count()==1 && list->item(0)->text().contains("跨日待办"),
              "reminder expands the selected day's task list at the saved height");
        check(sql("DELETE FROM Schedule WHERE id='tomorrow';"),"selected-date fixture removed");
        for(int w=0;w<3000 && calendar->scheduleCount()!=2;w+=50)settle(50);
        calendar->findChild<QPushButton *>("calendarAll")->click();

        const QString oldDate=QDate::currentDate().addDays(-62).toString(Qt::ISODate);
        check(sql(QString("INSERT INTO Schedule VALUES('old','%1','%1','12','0','【工作待办】#701 历史待办','不重复',0,0,'','');").arg(oldDate)),"older work task fixture written");
        auto selectTask=[&](const QString &name){
            for(int i=0;i<list->count();++i)if(list->item(i)->text().contains(name)){list->setCurrentRow(i);return true;}
            return false;
        };
        for(int w=0;w<3000 && calendar->scheduleCount()!=3;w+=50)settle(50);
        auto *complete=calendar->findChild<QPushButton *>("calendarComplete");
        auto *edit=calendar->findChild<QPushButton *>("calendarEdit");
        auto *remove=calendar->findChild<QPushButton *>("calendarDelete");
        check(complete && edit && remove,"expanded agenda exposes the three task actions");
        if(!complete || !edit || !remove)return 1;
        check(selectTask("历史待办") && complete->isEnabled(),"old task can be selected from the expanded list");
        check(list->currentItem() && list->currentItem()->text().contains("已完成") && complete->text()=="恢复",
              "legacy calendar-row completion follows the imported work task");
        complete->click();
        check(list->currentItem() && !list->currentItem()->text().contains("已完成"),"completed task can be reopened");
        complete->click();
        check(sql(QString("DELETE FROM Schedule WHERE id='old'; INSERT INTO Schedule VALUES('pushed-again','%1','%1','12','0','【工作待办】#701 历史待办','不重复',0,0,'','');").arg(oldDate)),
              "work sync recreates the same task with a new native row ID");
        auto repushed=[&]{
            return selectTask("历史待办") && list->currentItem()->data(Qt::UserRole+1).toJsonObject()["sourceId"].toString()=="pushed-again";
        };
        for(int w=0;w<3000 && !repushed();w+=50)settle(50);
        check(repushed() && list->currentItem()->text().contains("已完成"),"work task remains completed after reimport");
        calendar->grab().save(output+"/expanded-actions.png");

        check(selectTask("系统待办测试"),"original task selected for edit");
        edit->click();settle(180);
        auto *editor=calendar->findChild<QDialog *>("calendarEditDialog");
        check(editor && editor->isVisible(),"editing opens the inline task form");
        if(!editor)return 1;
        editor->findChild<QPlainTextEdit *>("calendarEditTitle")->setPlainText("编辑后的待办完整内容");
        editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        for(int w=0;w<3000 && !selectTask("编辑后的待办完整内容");w+=50)settle(50);
        check(selectTask("编辑后的待办完整内容"),"saving edits refreshes the source task in the agenda");
        check(selectTask("新增待办"),"second task selected for deletion");
        auto deleteChoice=[&](QMessageBox::StandardButton choice){
            remove->click();settle(180);
            auto *confirm=calendar->findChild<QDialog *>("calendarDeleteDialog");
            if(!confirm)return false;
            for(auto *button:confirm->findChildren<QPushButton *>())if(button->property("messageChoice").toInt()==int(choice)){
                button->click();return true;
            }
            return false;
        };
        check(deleteChoice(QMessageBox::No),"delete confirmation can be cancelled");settle(450);
        check(calendar->scheduleCount()==3,"cancel leaves the native task untouched");
        check(deleteChoice(QMessageBox::Yes),"delete requires explicit confirmation");
        for(int w=0;w<3000 && calendar->scheduleCount()!=2;w+=50)settle(50);
        check(calendar->scheduleCount()==2 && !selectTask("新增待办"),"confirmed deletion removes only the selected task");
        auto choose=[&](const QString &buttonName,const QString &menuName,int value){
            QTimer::singleShot(160,calendar,[&,menuName,value]{
                auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
                check(menu && menu->isVisible(),"date selector opens visible menu");
                if(!menu)return;
                check(menu->property("liquidPopupSkin").toBool(),"date selector uses shared liquid popup");
                menu->grab().save(output+"/"+menuName+".png");
                auto *wheel=menu->findChild<QWidget *>(menuName.left(menuName.size()-4)+"Wheel");
                check(wheel && wheel->height()==5*wheel->property("rowHeight").toInt()+10,
                      "picker keeps five rows with a font-aware height");
                if(!wheel){menu->close();return;}
                int current=wheel->property("selectedValue").toInt();
                while(current!=value){
                    QKeyEvent key(QEvent::KeyPress,current<value?Qt::Key_Down:Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(wheel,&key);
                    const int next=wheel->property("selectedValue").toInt();if(next==current)break;current=next;
                }
                check(current==value,"wheel scrolls to requested value");
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(wheel,&enter);
            });
            // Prevent a test failure from leaving a nested menu loop running.
            QTimer watchdog;watchdog.setSingleShot(true);
            QObject::connect(&watchdog,&QTimer::timeout,calendar,[]{if(auto *menu=QApplication::activePopupWidget())menu->close();});
            watchdog.start(1000);
            calendar->findChild<QPushButton *>(buttonName)->click();
        };
        calendar->selectDate(QDate(2028,2,29));settle(400);
        choose("calendarYear","calendarYearMenu",2027);
        check(calendar->selectedDate()==QDate(2027,2,28),"year menu clamps leap day to valid day");
        calendar->selectDate(QDate(2026,1,31));settle(400);
        choose("calendarMonth","calendarMonthMenu",2);
        check(calendar->selectedDate()==QDate(2026,2,28),"month menu clamps day 31 to February end");
        auto *monthButton=calendar->findChild<QPushButton *>("calendarMonth");
        const QFont normalFont=monthButton->font();QFont largeFont=normalFont;largeFont.setPointSize(30);monthButton->setFont(largeFont);
        QTimer::singleShot(100,calendar,[&]{
            auto *menu=QApplication::activePopupWidget();auto *wheel=menu?menu->findChild<QWidget *>("calendarMonthWheel"):nullptr;
            check(wheel && wheel->property("rowHeight").toInt()>34 &&
                  wheel->property("rowHeight").toInt()>=QFontMetrics(largeFont).height(),
                  "large custom font increases row height without clipping");
        });
        choose("calendarMonth","calendarMonthMenu",2);monthButton->setFont(normalFont);
        check(calendar->findChild<QPushButton *>("calendarYear")->text().contains("2026") && calendar->findChild<QPushButton *>("calendarMonth")->text().startsWith("2 "),"date header follows selection");
        const QDate beforeCancel=calendar->selectedDate();
        QTimer::singleShot(160,calendar,[&]{
            auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            auto *wheel=menu?menu->findChild<QWidget *>("calendarMonthWheel"):nullptr;
            check(wheel!=nullptr,"wheel supports direct scrolling and dragging");if(!wheel){if(menu)menu->close();return;}
            const QPointF center(wheel->rect().center());
            QWheelEvent scroll(center,wheel->mapToGlobal(center.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(wheel,&scroll);check(wheel->property("selectedValue").toInt()==3,"one wheel notch advances one month");
            QMouseEvent press(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(wheel,&press);
            QMouseEvent drag(QEvent::MouseMove,center-QPointF(0,34),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(wheel,&drag);
            QMouseEvent release(QEvent::MouseButtonRelease,center-QPointF(0,34),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(wheel,&release);
            check(wheel->property("selectedValue").toInt()==4 && menu->isVisible(),"drag advances and keeps wheel open");
            QKeyEvent end(QEvent::KeyPress,Qt::Key_End,Qt::NoModifier);QApplication::sendEvent(wheel,&end);QApplication::sendEvent(wheel,&scroll);
            check(wheel->property("selectedValue").toInt()==12,"wheel clamps at December");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(wheel,&escape);
        });
        calendar->findChild<QPushButton *>("calendarMonth")->click();
        check(calendar->selectedDate()==beforeCancel,"Escape cancels draft selection");
        calendar->selectDate(QDate::currentDate());settle(500);
        const QRect original=calendar->geometry();
        auto drag=[&]{QPoint l(90,20),g=calendar->mapToGlobal(l);QPoint delta=QPoint(10,10)-calendar->pos();
            QMouseEvent a(QEvent::MouseButtonPress,l,g,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(calendar,&a);
            QMouseEvent b(QEvent::MouseMove,l+delta,g+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(calendar,&b);
            QMouseEvent c(QEvent::MouseButtonRelease,l,g+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(calendar,&c);};
        drag();check(calendar->geometry()==original,"calendar remains locked outside editing");canvas.setGlobalEditMode(true);drag();settle(150);
        check(calendar->pos()==QPoint(0,0),"calendar supports edit mode and edge snapping");canvas.setGlobalEditMode(false);
        bool avoids=true;for(auto *icon:canvas.findChildren<DesktopIcon *>(QString(),Qt::FindDirectChildrenOnly))if(icon->isVisible() && icon->geometry().intersects(calendar->geometry().adjusted(-8,-8,8,8)))avoids=false;
        check(avoids,"desktop icons avoid calendar geometry");
        calendar->grab().save(output+"/calendar.png");
        canvas.setCalendarWidgetVisible(false);check(!calendar->isVisible(),"calendar can be hidden through desktop widget menu action");canvas.setCalendarWidgetVisible(true);
        collapse->click();awaitHeight(270);LiquidDesklet::setAutoStart("calendar",true);calendar->savePlacement();
    }
    {DesktopCanvas canvas;canvas.show();settle(1900);auto *calendar=canvas.findChild<CalendarDesklet *>();check(calendar && calendar->isVisible() && calendar->pos()==QPoint(0,0) && calendar->scheduleCount()==2,"calendar restarts with its placement and system tasks");
        check(calendar && calendar->height()==270 && !calendar->findChild<QListWidget *>("calendarAgenda")->isVisible(),"collapsed state survives restart");
        if(calendar){calendar->findChild<QPushButton *>("calendarCollapse")->click();for(int w=0;w<2000 && (calendar->height()!=520 || calendar->drawerRunning());w+=20)settle(20);check(calendar->height()==520,"expanded custom height survives restart");
            auto *items=calendar->findChild<QListWidget *>("calendarAgenda");
            bool completed=false;
            for(int i=0;items && i<items->count();++i)if(items->item(i)->text().contains("历史待办"))completed=items->item(i)->text().contains("已完成");
            check(completed,"completion persists after the desktop card restarts");}
    }
    return failures?1:0;
}
