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
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QGridLayout>
class Test:public QObject {
Q_OBJECT
private slots:
void optInGridOrderIsStableAndTabsRemainFiltered(){
    QWidget gridRoot;gridRoot.setProperty("settingsUiVersion",1);QGridLayout grid(&gridRoot);
    QLineEdit c,a,b;c.setObjectName("c");a.setObjectName("a");b.setObjectName("b");grid.addWidget(&c,1,0);grid.addWidget(&b,0,1);grid.addWidget(&a,0,0);
    LquiSettings::Bridge ordered("fixture",[&](const QString&){return &gridRoot;},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
    const auto fields=QJsonDocument::fromJson(ordered.Page("general").toUtf8()).object()["fields"].toArray();QCOMPARE(fields.size(),3);QCOMPARE(fields[0].toObject()["name"].toString(),QString("a"));QCOMPARE(fields[1].toObject()["name"].toString(),QString("b"));QCOMPARE(fields[2].toObject()["name"].toString(),QString("c"));
    QTabWidget tabs;tabs.setProperty("settingsUiVersion",1);QWidget one,two;QFormLayout l1(&one),l2(&two);QLineEdit x,y;x.setObjectName("onlyFirst");y.setObjectName("onlySecond");l1.addRow("甲",&x);l2.addRow("乙",&y);tabs.addTab(&one,"一");tabs.addTab(&two,"二");
    LquiSettings::Bridge bridge("fixture",[&](const QString&){return &tabs;},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
    auto data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QVERIFY(QJsonDocument(data).toJson().contains("onlyFirst"));QVERIFY(!QJsonDocument(data).toJson().contains("onlySecond"));QString id;
    for(const auto &v:data["fields"].toArray())if(v.toObject()["kind"]=="tabs")id=v.toObject()["id"].toString();QVERIFY(!id.isEmpty());const auto changed=bridge.Set("general",id,"{\"value\":1}");QVERIFY(changed.contains("onlySecond"));QVERIFY(!changed.contains("onlyFirst"));
}
void optInLayoutOrderAndNestedLabels(){
    QWidget root;root.setProperty("settingsUiVersion",1);QVBoxLayout outer(&root);
    auto *second=new QLineEdit(&root);second->setObjectName("second");
    auto *first=new QSlider(Qt::Horizontal,&root);first->setObjectName("first");first->setRange(5,60);
    auto *form=new QFormLayout;auto *row=new QHBoxLayout;row->addWidget(first);auto *readout=new QLabel("10 秒");readout->setProperty("sliderValue",true);row->addWidget(readout);form->addRow("采样间隔",row);outer.addLayout(form);outer.addWidget(second);
    auto *help=new QLabel("独立说明",&root);outer.addWidget(help);
    LquiSettings::Bridge bridge("fixture",[&](const QString&){return &root;},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
    auto data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QCOMPARE(data["ui"].toObject()["version"].toInt(),1);
    const auto fields=data["fields"].toArray();QCOMPARE(fields.size(),3);QCOMPARE(fields[0].toObject()["name"].toString(),QString("first"));QCOMPARE(fields[0].toObject()["label"].toString(),QString("采样间隔"));QCOMPARE(fields[1].toObject()["name"].toString(),QString("second"));
    QSignalSpy released(first,&QSlider::sliderReleased);bridge.Set("general",fields[0].toObject()["id"].toString(),"{\"value\":20}");QCOMPARE(first->value(),20);QCOMPARE(released.count(),1);
    root.setProperty("settingsUiVersion",QVariant());data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QVERIFY(!data.contains("ui"));QCOMPARE(data["fields"].toArray().first().toObject()["name"].toString(),QString("second"));
}
void semanticPreviewKeepsActionsAndCachesOnlyContent(){
    QWidget root;root.setProperty("settingsUiVersion",1);QVBoxLayout outer(&root);
    auto *preview=new QWidget;preview->setObjectName("wallpaperPreview");preview->setProperty("settingsPreviewRole","wallpaper");QVBoxLayout children(preview);
    QLabel picture;QPixmap red(260,160);red.fill(Qt::red);picture.setPixmap(red);children.addWidget(&picture);
    QPushButton choose("选择图片");choose.setObjectName("choose");children.addWidget(&choose);outer.addWidget(preview);
    QLineEdit secret("hidden-password");secret.setEchoMode(QLineEdit::Password);outer.addWidget(&secret);
    LquiSettings::Bridge bridge("fixture",[&](const QString&){return &root;},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
    auto data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QCOMPARE(data["fields"].toArray().size(),3);QVERIFY(!QJsonDocument(data).toJson().contains("hidden-password"));QCOMPARE(bridge.property("previewEncodeCount").toInt(),1);
    QString encoded=data["fields"].toArray()[0].toObject()["value"].toString();QVERIFY(!encoded.isEmpty());QVERIFY(QByteArray::fromBase64(encoded.toLatin1()).size()<2*1024*1024);
    bridge.Page("general");QCOMPARE(bridge.property("previewEncodeCount").toInt(),1);
    red.fill(Qt::blue);picture.setPixmap(red);data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QVERIFY(data["fields"].toArray()[0].toObject()["value"].toString()!=encoded);QCOMPARE(bridge.property("previewEncodeCount").toInt(),2);
    picture.clear();data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QVERIFY(data["fields"].toArray()[0].toObject()["value"].toString().isEmpty());
}
void metadataOnlyExportsWhitelistedProperties(){
    QWidget root;root.setProperty("settingsUiVersion",1);QVBoxLayout layout(&root);QCheckBox check("原标签");check.setObjectName("record");check.setAccessibleName("实际标签");check.setProperty("settingsUiGroup","metrics");check.setProperty("settingsUiOrder",20);check.setProperty("settingsUiVisibleWhen",QVariantMap{{"name","mode"},{"equals",0},{"script","never-export"}});layout.addWidget(&check);
    LquiSettings::Bridge bridge("fixture",[&](const QString&){return &root;},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
    auto data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();auto field=data["fields"].toArray().first().toObject();QCOMPARE(field["label"].toString(),QString("实际标签"));QCOMPARE(field["ui"].toObject()["order"].toInt(),20);QVERIFY(!QJsonDocument(data).toJson().contains("never-export"));
    check.setProperty("settingsUiLabel","明确标签");data=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QCOMPARE(data["fields"].toArray().first().toObject()["label"].toString(),QString("明确标签"));
}
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
