#include "SettingsBridge.h"
#include <QApplication>
#include <QWidget>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusConnectionInterface>
#include <QJsonDocument>
#include <QJsonValue>
#include <QAbstractButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QAbstractItemView>
#include <QTimer>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QBuffer>
#include <QPixmap>
#include <QtMath>
#include <QTabBar>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QItemSelectionModel>

namespace LquiSettings {
namespace {
int formDepth=0;
QString json(const QJsonObject &value){return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));}
QString error(const QString &message){return json({{"ok",false},{"error",message}});}
QString plain(QString text){return text.replace('&',QString()).trimmed();}
}
bool buildingForm(){return formDepth>0;}
FormScope::FormScope(){++formDepth;}
FormScope::~FormScope(){--formDepth;}
bool open(const QString &page){
    if(buildingForm()||qEnvironmentVariableIsSet("LQUI_SETTINGS_LEGACY"))return false;
    const QString launcher=QDir::homePath()+"/.local/bin/kylin-lqui-settings";
    if(!QFileInfo(launcher).isExecutable())return false;
    auto bus=QDBusConnection::sessionBus();
    if(bus.interface()&&bus.interface()->isServiceRegistered("org.kylin.Lqui.Settings").value()) {
        auto message=QDBusMessage::createMethodCall("org.kylin.Lqui.Settings","/Settings","org.kylin.Lqui.Settings","Open");message.setArguments({page});bus.asyncCall(message);return true;
    }
    return QProcess::startDetached(launcher,{"--page",page});
}
Bridge::Bridge(QString name,Factory f,CatalogFactory c,QObject *parent):QObject(parent),module(std::move(name)),factory(std::move(f)),catalog(std::move(c)){}
bool Bridge::registerOnSession(){registered=QDBusConnection::sessionBus().registerObject("/org/kylin/Lqui/Settings",this,QDBusConnection::ExportScriptableSlots);return registered;}
Bridge::~Bridge(){if(registered)QDBusConnection::sessionBus().unregisterObject("/org/kylin/Lqui/Settings");}
QString Bridge::Catalog(){return json({{"ok",true},{"module",module},{"pages",catalog()}});}
QString Bridge::controlId(QWidget *widget){
    QString id=widget->property("lquiControlId").toString();
    if(id.isEmpty()){id=QString::number(nextId++);widget->setProperty("lquiControlId",id);controls[id]=widget;}
    return id;
}
QString Bridge::label(QWidget *widget,QWidget *root) const {
    if(auto *button=qobject_cast<QAbstractButton*>(widget))if(!button->text().isEmpty()&&!button->property("color").isValid())return plain(button->text());
    if(!widget->accessibleName().isEmpty())return plain(widget->accessibleName());
    for(QWidget *field=widget;field&&field!=root;field=field->parentWidget()) {
        auto *parent=field->parentWidget();
        if(parent)if(auto *form=qobject_cast<QFormLayout*>(parent->layout()))if(auto *text=qobject_cast<QLabel*>(form->labelForField(field)))return plain(text->text());
        if(parent&&parent->layout()) {
            const int index=parent->layout()->indexOf(field);
            for(int i=index-1;i>=0;--i)if(auto *text=qobject_cast<QLabel*>(parent->layout()->itemAt(i)->widget()))if(!text->text().isEmpty())return plain(text->text());
        }
    }
    if(!widget->toolTip().isEmpty())return plain(widget->toolTip());
    return widget->objectName().isEmpty()?QStringLiteral("选项"):widget->objectName();
}
QJsonObject Bridge::snapshot(const QString &page) {
    bool valid=false;for(const auto &v:catalog())if(v.toObject().value("id").toString()==page)valid=true;
    if(!valid)return {{"ok",false},{"error","设置页不存在，请刷新分类"}};
    QPointer<QWidget> root;{FormScope scope;root=factory(page);roots[page]=root;}
    if(!root)return {{"ok",false},{"error","模块未提供此设置页"}};
    QJsonArray fields;bool dirty=false;
    QList<QWidget*> widgets{root};widgets.append(root->findChildren<QWidget*>());
    for(auto *w:widgets) {
        dirty|=w->property("settingsDirty").toBool();
        if(w!=root&&!w->isVisibleTo(root))continue;
        bool internal=false;
        for(auto *p=w->parentWidget();p&&p!=root;p=p->parentWidget())if(p->isWindow()||qobject_cast<QComboBox*>(p)||qobject_cast<QAbstractSpinBox*>(p)||qobject_cast<QAbstractItemView*>(p)||qobject_cast<QTextEdit*>(p)||qobject_cast<QPlainTextEdit*>(p)||p->objectName()=="liquidDialogTitlebar")internal=true;
        if(internal)continue;
        QJsonObject field{{"id",controlId(w)},{"name",w->objectName()},{"label",label(w,root)},{"enabled",w->isEnabled()},{"help",w->property("settingsHelp").toString().isEmpty()?w->toolTip():w->property("settingsHelp").toString()}};
        if(w->objectName()=="settingsPreview") {field["kind"]="preview";field["label"]="候选窗预览";QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);w->grab().toImage().scaled(680,200,Qt::KeepAspectRatio,Qt::SmoothTransformation).save(&buffer,"PNG");field["value"]=QString::fromLatin1(bytes.toBase64());}
        else if(auto *check=qobject_cast<QCheckBox*>(w)){field["kind"]="boolean";field["value"]=check->isChecked();}
        else if(auto *radio=qobject_cast<QRadioButton*>(w)){field["kind"]="boolean";field["value"]=radio->isChecked();}
        else if(auto *slider=qobject_cast<QSlider*>(w)){field["kind"]="number";field["value"]=slider->value();field["min"]=slider->minimum();field["max"]=slider->maximum();field["step"]=slider->singleStep();field["suffix"]=slider->property("settingsSuffix").toString();field["valueLabels"]=QJsonArray::fromStringList(slider->property("settingsValueLabels").toStringList());}
        else if(auto *spin=qobject_cast<QSpinBox*>(w)){field["kind"]="number";field["value"]=spin->value();field["min"]=spin->minimum();field["max"]=spin->maximum();field["step"]=spin->singleStep();field["suffix"]=spin->suffix();}
        else if(auto *spin=qobject_cast<QDoubleSpinBox*>(w)){field["kind"]="number";field["value"]=spin->value();field["min"]=spin->minimum();field["max"]=spin->maximum();field["step"]=spin->singleStep();field["decimals"]=spin->decimals();field["suffix"]=spin->suffix();}
        else if(auto *combo=qobject_cast<QComboBox*>(w)){field["kind"]="choice";field["value"]=combo->currentIndex();QJsonArray options,allowed;for(int i=0;i<combo->count();++i){options.append(combo->itemText(i));allowed.append(bool(combo->model()->flags(combo->model()->index(i,combo->modelColumn(),combo->rootModelIndex()))&Qt::ItemIsEnabled));}field["options"]=options;field["optionsEnabled"]=allowed;field["editable"]=combo->isEditable();field["text"]=combo->currentText();}
        else if(auto *edit=qobject_cast<QLineEdit*>(w)){field["kind"]="text";field["value"]=edit->echoMode()==QLineEdit::Normal?edit->text():QString();field["placeholder"]=edit->placeholderText();field["maxLength"]=edit->maxLength();field["readOnly"]=edit->isReadOnly();field["secret"]=edit->echoMode()!=QLineEdit::Normal;}
        else if(auto *tabs=qobject_cast<QTabBar*>(w)){field["kind"]="tabs";field["label"]="分类";field["value"]=tabs->currentIndex();QJsonArray options;for(int i=0;i<tabs->count();++i)options.append(tabs->tabText(i));field["options"]=options;}
        else if(auto *edit=qobject_cast<QPlainTextEdit*>(w)){field["kind"]="multiline";field["value"]=edit->toPlainText();field["readOnly"]=edit->isReadOnly();}
        else if(auto *edit=qobject_cast<QTextEdit*>(w)){field["kind"]=edit->isReadOnly()?"note":"multiline";field["value"]=edit->toPlainText();field["readOnly"]=edit->isReadOnly();}
        else if(auto *list=qobject_cast<QListWidget*>(w)){field["kind"]="list";field["multiple"]=list->selectionMode()==QAbstractItemView::ExtendedSelection||list->selectionMode()==QAbstractItemView::MultiSelection;QJsonArray items;for(int i=0;i<list->count();++i){const auto *item=list->item(i);items.append(QJsonObject{{"text",item->text()},{"selected",item->isSelected()},{"checkable",item->data(Qt::CheckStateRole).isValid()},{"checked",item->checkState()==Qt::Checked},{"enabled",bool(item->flags()&Qt::ItemIsEnabled)}});}field["items"]=items;field["value"]=list->currentRow();}
        else if(auto *button=qobject_cast<QAbstractButton*>(w)) {
            const QString title=plain(button->text());
            if(title.isEmpty()&&button->toolTip().isEmpty()&&button->accessibleName().isEmpty())continue;
            if(title=="关闭"||title=="Close"||title=="取消"||title=="Cancel"||title=="×"||title=="−")continue;
            if(button->property("color").isValid())field["label"]=field["label"].toString()+" · "+title;
            field["kind"]=button->isCheckable()?"toggle":"action";field["value"]=button->isChecked();
        } else if(auto *text=qobject_cast<QLabel*>(w)) {
            if(text->text().isEmpty()||text->pixmap()||w->property("sliderValue").toBool())continue;
            // Form labels are already associated with their input, so avoid duplicates.
            bool formLabel=false;if(auto *form=qobject_cast<QFormLayout*>(w->parentWidget()?w->parentWidget()->layout():nullptr)){int row;QFormLayout::ItemRole role;form->getWidgetPosition(w,&row,&role);formLabel=role==QFormLayout::LabelRole;}
            if(formLabel)continue;
            field["kind"]=(w->objectName()=="cardHeading"||w->objectName()=="pageHeading"||w->property("settingsSection").toBool()||w->property("settingsSectionTitle").toBool())?"heading":"note";field["value"]=text->text();
        } else if(auto *group=qobject_cast<QGroupBox*>(w)){if(group->title().isEmpty())continue;field["kind"]="heading";field["value"]=group->title();}
        else continue;
        fields.append(field);
    }
    return {{"ok",true},{"module",module},{"page",page},{"fields",fields},{"dirty",dirty},{"modalActive",bool(QApplication::activeModalWidget())}};
}
QString Bridge::Page(const QString &page){return json(snapshot(page));}
QString Bridge::Set(const QString &page,const QString &id,const QString &encoded) {
    auto root=roots.value(page);auto widget=controls.value(id);
    if(!root||!widget||!(root==widget||root->isAncestorOf(widget)))return error("设置项已经刷新，请重试");
    if(!widget->isEnabled()||QApplication::activeModalWidget())return error("请先完成当前确认或等待设置就绪");
    const auto data=QJsonDocument::fromJson(encoded.toUtf8());if(!data.isObject()||!data.object().contains("value"))return error("无效的设置值");const auto value=data.object()["value"];
    if(auto *check=qobject_cast<QAbstractButton*>(widget.data())){if(!check->isCheckable()||!value.isBool())return error("无效的开关值");if(check->isChecked()!=value.toBool())check->click();}
    else if(auto *spin=qobject_cast<QSpinBox*>(widget.data())){if(!value.isDouble()||qFloor(value.toDouble())!=value.toDouble()||value.toDouble()<spin->minimum()||value.toDouble()>spin->maximum())return error("数值超出范围");spin->setValue(value.toInt());}
    else if(auto *spin=qobject_cast<QDoubleSpinBox*>(widget.data())){if(!value.isDouble()||value.toDouble()<spin->minimum()||value.toDouble()>spin->maximum())return error("数值超出范围");spin->setValue(value.toDouble());}
    else if(auto *slider=qobject_cast<QSlider*>(widget.data())){if(!value.isDouble()||qFloor(value.toDouble())!=value.toDouble()||value.toDouble()<slider->minimum()||value.toDouble()>slider->maximum())return error("数值超出范围");slider->setValue(value.toInt());QMetaObject::invokeMethod(slider,"sliderReleased",Qt::DirectConnection);}
    else if(auto *combo=qobject_cast<QComboBox*>(widget.data())){if(value.isString()&&combo->isEditable())combo->setEditText(value.toString());else {const int index=value.toInt(-1);if(index<0||index>=combo->count()||!(combo->model()->flags(combo->model()->index(index,combo->modelColumn(),combo->rootModelIndex()))&Qt::ItemIsEnabled))return error("选项已经变化或不可用，请刷新");combo->setCurrentIndex(index);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,index));}}
    else if(auto *edit=qobject_cast<QLineEdit*>(widget.data())){if(edit->isReadOnly()||!value.isString()||value.toString().size()>edit->maxLength())return error("无效的文本值");edit->setText(value.toString());QMetaObject::invokeMethod(edit,"editingFinished",Qt::DirectConnection);}
    else if(auto *tabs=qobject_cast<QTabBar*>(widget.data())){const int index=value.toInt(-1);if(index<0||index>=tabs->count()||!tabs->isTabEnabled(index))return error("无效的分类");tabs->setCurrentIndex(index);}
    else if(auto *edit=qobject_cast<QPlainTextEdit*>(widget.data())){if(!value.isString()||edit->isReadOnly()||value.toString().size()>1048576)return error("无效的文本");edit->setPlainText(value.toString());}
    else if(auto *edit=qobject_cast<QTextEdit*>(widget.data())){if(!value.isString()||edit->isReadOnly()||value.toString().size()>1048576)return error("无效的文本");edit->setPlainText(value.toString());}
    else if(auto *list=qobject_cast<QListWidget*>(widget.data())){
        if(!value.isObject())return error("无效的列表值");const auto change=value.toObject();const int row=change["row"].toInt(-1);
        if(row<0||row>=list->count())return error("列表内容已变化，请刷新");auto *item=list->item(row);if(!(item->flags()&Qt::ItemIsEnabled))return error("此项目不可修改");
        if(change.contains("checked")){if(!item->data(Qt::CheckStateRole).isValid()||!change["checked"].isBool())return error("项目不可勾选");item->setCheckState(change["checked"].toBool()?Qt::Checked:Qt::Unchecked);}
        else if(change.contains("selectedRows")){
            if(!change["selectedRows"].isArray())return error("无效的选择");const auto rows=change["selectedRows"].toArray();if(list->selectionMode()==QAbstractItemView::SingleSelection&&rows.size()>1)return error("此列表只能选择一项");
            QItemSelection selection;for(const auto &entry:rows){const int selected=entry.toInt(-1);if(selected<0||selected>=list->count()||!(list->item(selected)->flags()&Qt::ItemIsEnabled))return error("无效的列表项");const auto index=list->model()->index(selected,0);selection.select(index,index);}
            list->setCurrentRow(row,QItemSelectionModel::NoUpdate);list->selectionModel()->select(selection,QItemSelectionModel::ClearAndSelect);
        }
        else {if(!change["selected"].isBool())return error("无效的选择");if(list->selectionMode()==QAbstractItemView::SingleSelection)list->clearSelection();list->setCurrentRow(row,QItemSelectionModel::NoUpdate);item->setSelected(change["selected"].toBool());}
    }
    else return error("此设置项不可修改");
    return json(snapshot(page));
}
QString Bridge::Activate(const QString &page,const QString &id) {
    auto root=roots.value(page);auto widget=controls.value(id);auto *button=qobject_cast<QAbstractButton*>(widget.data());
    if(!root||!button||!root->isAncestorOf(button)||!button->isEnabled())return error("操作暂时不可用");
    if(QApplication::activeModalWidget())return error("请先完成当前确认");
    QTimer::singleShot(0,button,[button]{button->click();});return json({{"ok",true},{"queued",true}});
}
}
