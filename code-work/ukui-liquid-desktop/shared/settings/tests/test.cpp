#include "SettingsBridge.h"
#include <QTest>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QJsonDocument>
#include <QFormLayout>
#include <QSlider>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QTabWidget>
#include <QPushButton>
class Test:public QObject {
Q_OBJECT
private slots:
void fieldsUseNativeCallbacksAndValidate(){
    QWidget root;QFormLayout form(&root);QSlider slider(Qt::Horizontal);slider.setRange(10,20);slider.setValue(15);form.addRow("幅度",&slider);
    QComboBox combo;combo.addItems({"第一项","第二项"});form.addRow("模式",&combo);QSignalSpy activated(&combo,QOverload<int>::of(&QComboBox::activated));
    QLineEdit secret("never-export-this");secret.setEchoMode(QLineEdit::Password);form.addRow("凭据",&secret);
    QPlainTextEdit text("原文本");form.addRow("多行",&text);QListWidget list;auto *item=new QListWidgetItem("PDF",&list);item->setCheckState(Qt::Unchecked);form.addRow("格式",&list);
    QPushButton button("应用");form.addRow(&button);QSignalSpy clicked(&button,&QPushButton::clicked);
    LquiSettings::Bridge bridge("fixture",[&](const QString &){QVERIFY_SCOPE();return &root;},[]{return QJsonArray{QJsonObject{{"id","general"},{"title","测试"}}};});
    auto data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QVERIFY(data["ok"].toBool());QVERIFY(!QJsonDocument(data).toJson().contains("never-export-this"));
    auto id=[&](const QString &label){for(const auto &v:data["fields"].toArray())if(v.toObject()["label"]==label)return v.toObject()["id"].toString();return QString();};
    QVERIFY(!id("幅度").isEmpty());QVERIFY(!QJsonDocument::fromJson(bridge.Set("general",id("幅度"),"{\"value\":999}").toUtf8()).object()["ok"].toBool());QCOMPARE(slider.value(),15);
    bridge.Set("general",id("幅度"),"{\"value\":19}");QCOMPARE(slider.value(),19);
    bridge.Set("general",id("模式"),"{\"value\":1}");QCOMPARE(combo.currentIndex(),1);QCOMPARE(activated.size(),1);auto *model=qobject_cast<QStandardItemModel*>(combo.model());model->item(0)->setEnabled(false);QVERIFY(!QJsonDocument::fromJson(bridge.Set("general",id("模式"),"{\"value\":0}").toUtf8()).object()["ok"].toBool());QCOMPARE(combo.currentIndex(),1);
    bridge.Set("general",id("多行"),QString::fromUtf8("{\"value\":\"更新\\n两行\"}"));QCOMPARE(text.toPlainText(),QString("更新\n两行"));
    bridge.Set("general",id("格式"),"{\"value\":{\"row\":0,\"checked\":true}}");QCOMPARE(item->checkState(),Qt::Checked);
    new QListWidgetItem("DOC",&list);list.setSelectionMode(QAbstractItemView::ExtendedSelection);
    bridge.Set("general",id("格式"),"{\"value\":{\"row\":1,\"selectedRows\":[0,1]}}");QCOMPARE(list.selectedItems().size(),2);
    bridge.Activate("general",id("应用"));QTRY_COMPARE(clicked.size(),1);QVERIFY(!root.isVisible());
}
void tabsExposeOnlyCurrentPage(){
    QTabWidget tabs;QWidget first,second;QFormLayout a(&first),b(&second);QLineEdit x,y;a.addRow("第一页",&x);b.addRow("第二页",&y);tabs.addTab(&first,"基础");tabs.addTab(&second,"高级");
    LquiSettings::Bridge bridge("fixture",[&](const QString &){return &tabs;},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
    auto data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QString id;
    for(const auto &v:data["fields"].toArray())if(v.toObject()["kind"]=="tabs")id=v.toObject()["id"].toString();QVERIFY(!id.isEmpty());
    const QString changed=bridge.Set("general",id,"{\"value\":1}");QVERIFY(changed.contains("第二页"));QVERIFY(!changed.contains("第一页"));QVERIFY(!tabs.isVisible());
}
static void QVERIFY_SCOPE(){Q_ASSERT(LquiSettings::buildingForm());}
};
QTEST_MAIN(Test)
#include "test.moc"
