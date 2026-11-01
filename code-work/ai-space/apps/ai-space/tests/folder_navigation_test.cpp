#include "SmartSpaceWidget.h"
#include <QApplication>
#include <QDir>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QMessageBox>
#include <QLabel>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

class SmartSpaceStartupTestAccess
{
  public:
    static void run()
    {
        SmartSpaceWidget widget;
        QTemporaryDir launchFixture;
        const QString marker=launchFixture.path()+"/executed",desktop=launchFixture.path()+"/invoice.pdf.desktop";
        QFile launcher(desktop);require(launcher.open(QIODevice::WriteOnly),"fixture launcher writable");
        launcher.write(("[Desktop Entry]\nType=Application\nName=invoice\nExec=/usr/bin/touch "+marker+"\n").toUtf8());launcher.close();
        bool confirmed=false;
        QTimer::singleShot(0,[&]{
            auto *dialog=widget.findChild<QDialog*>("smartSpaceMessage");
            auto *label=dialog?dialog->findChild<QLabel*>("smartMessageText"):nullptr;
            if(dialog){confirmed=label&&label->textFormat()==Qt::PlainText&&label->text().contains(desktop);dialog->reject();}
        });
        widget.openPath(desktop);
        require(confirmed&&!QFile::exists(marker),"executable search results require confirmation and Cancel does not launch");
        SmartSpaceEntry misleading;misleading.path=desktop;misleading.name="<b>invoice.pdf.desktop";
        auto *fileCard=widget.createFileCard(misleading,nullptr);bool plain=false;
        for(auto *label:fileCard->findChildren<QLabel*>())if(label->text()==misleading.name)plain=label->textFormat()==Qt::PlainText;
        require(plain,"filename markup is displayed literally");fileCard->deleteLater();
        widget.resize(1000, 750);
        widget.m_roots = QStringList{QStringLiteral("/data/materials")};
        for (int group = 0; group < 30; ++group) {
            for (int child = 0; child < 12; ++child) {
                SmartSpaceEntry entry;
                entry.root = widget.m_roots.first();
                entry.path = QStringLiteral("/data/materials/group-%1/child-%2/deep/document.txt")
                                 .arg(group, 2, 10, QLatin1Char('0'))
                                 .arg(child, 2, 10, QLatin1Char('0'));
                entry.name = QStringLiteral("document.txt");
                entry.suffix = "txt";
                entry.category = "documents";
                widget.m_entries.append(entry);
            }
        }
        widget.updateResults(true);
        widget.show();
        QApplication::processEvents();
        auto *root = widget.findChild<QListWidget *>("smartRootFolderList");
        require(root && root->count() == 30, "all root groups are available");
        const int fullHeight = root->height();
        root->setCurrentRow(0);
        emit root->itemClicked(root->item(0));
        QApplication::processEvents();
        require(widget.m_childFolderScroll->isVisible(), "first click opens children");
        auto *children = widget.findChild<QListWidget *>("smartChildFolderList");
        require(children && children->count() == 12, "children are listed in the lower region");
        auto halves = [&] {
            const int rootHeight = widget.m_folderLevelWidgets.first()->height();
            const int childHeight = widget.m_childFolderScroll->height();
            require(qAbs(rootHeight - childHeight) <= 2,
                    "upper and lower regions share height equally");
        };
        halves();
        require(root->verticalScrollBar()->maximum() > 0 &&
                    children->verticalScrollBar()->maximum() > 0,
                "root and child lists scroll independently");
        const auto scope = widget.m_scopePath;
        emit root->itemClicked(root->item(0));
        QApplication::processEvents();
        require(!widget.m_childFolderScroll->isVisible(), "second click closes children");
        require(root->height() == fullHeight, "collapsed root list regains its entire region");
        require(widget.m_scopePath == scope && widget.resultCount() == 12,
                "collapsing retains the selected results scope");
        emit root->itemClicked(root->item(0));
        QApplication::processEvents();
        children = widget.findChild<QListWidget *>("smartChildFolderList");
        emit children->itemClicked(children->item(0));
        QApplication::processEvents();
        require(widget.m_folderLevelWidgets.size() == 3, "nested children can expand");
        halves();
        emit children->itemClicked(children->item(0));
        QApplication::processEvents();
        require(widget.m_folderLevelWidgets.size() == 2, "nested children toggle closed");
        emit root->itemClicked(root->item(1));
        QApplication::processEvents();
        require(widget.m_folderLevelWidgets.size() == 2 && widget.m_scopePath.endsWith("group-01"),
                "switching parents replaces the lower contents");
        for (int density : {-4, 0, 3}) {
            widget.setResultDensity(density);
            widget.resize(1000, 500);
            QApplication::processEvents();
            halves();
            widget.resize(1100, 850);
            QApplication::processEvents();
            halves();
        }
        widget.updateResults(true);
        QApplication::processEvents();
        require(!widget.m_childFolderScroll->isVisible(), "reset removes child navigation");
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    require(home.isValid(), "isolated home");
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("AI_SPACE_TEST_NO_BOOTSTRAP", "1");
    QApplication app(argc, argv);
    app.setOrganizationName("xikario");
    app.setApplicationName("ai-space");
    SmartSpaceStartupTestAccess::run();
    std::cout << "Folder navigation toggles and equal regions passed" << std::endl;
}
