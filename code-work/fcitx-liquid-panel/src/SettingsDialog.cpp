#include "SettingsDialog.h"
#include <QTemporaryDir>
#include <QFile>
#include <QPointer>
#include <memory>
#include <QTabWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QMessageBox>
#include <QScreen>
#include <QGuiApplication>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>

SettingsDialog::SettingsDialog(CandidateWindow &live,QWidget *parent)
    :QDialog(parent),m_live(live),m_draft(live.config()) {
    setObjectName("liquidSettings"); setWindowTitle(QStringLiteral("RIME液态输入法设置"));
    setWindowIcon(QIcon(":/fcitx-liquid/tray/settings.png"));
    resize(880,760);
    if (auto *screen=QGuiApplication::primaryScreen()) resize(size().boundedTo(screen->availableGeometry().size()-QSize(40,60)));
    auto *layout=new QVBoxLayout(this);
    auto *intro=new QLabel(QStringLiteral("调整后立即预览；候选数量或候选拼音变更会重新部署 Rime，请在输入结束后应用。"));
    intro->setWordWrap(true); layout->addWidget(intro);
    auto *presets=new QComboBox; presets->setObjectName("preset");
    presets->addItems({QStringLiteral("当前设置"),QStringLiteral("默认玻璃"),QStringLiteral("紧凑两行"),QStringLiteral("大字清晰")});
    auto *presetRow=new QHBoxLayout; presetRow->addWidget(new QLabel(QStringLiteral("快速选择"))); presetRow->addWidget(presets,1); layout->addLayout(presetRow);
    auto *tabs=new QTabWidget; layout->addWidget(tabs,1);
    const QStringList groups={"Interface","Fonts","Geometry","Effects","Colors"};
    const QStringList names={QStringLiteral("外观与界面"),QStringLiteral("字体"),QStringLiteral("尺寸与间距"),QStringLiteral("玻璃效果"),QStringLiteral("颜色")};
    for(int i=0;i<groups.size();++i) {
        auto *page=new QWidget; auto *form=new QFormLayout(page); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        if(groups[i]=="Colors") {
            auto *hint=new QLabel(QStringLiteral("颜色可设置透明度。开启自动适配会改善白底玻璃及文字对比度；关闭后始终使用所选颜色。"));
            hint->setWordWrap(true); form->addRow(hint);
        }
        for(const auto &f:candidateFields()) {
            if(f.group!=groups[i]) continue;
            QWidget *control=nullptr;
            if(f.kind==ConfigField::Number) {
                auto *spin=new QDoubleSpinBox; spin->setRange(f.minimum,f.maximum); spin->setSingleStep(f.step);
                spin->setDecimals(f.step<1?2:0); spin->setKeyboardTracking(false); control=spin;
                connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double){changed();});
            } else if(f.kind==ConfigField::Choice) {
                auto *combo=new QComboBox;combo->addItems(f.choices);control=combo;
                connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){changed();});
            } else if(f.kind==ConfigField::Boolean) {
                auto *check=new QCheckBox; control=check;
                connect(check,&QCheckBox::toggled,this,[this](bool){changed();});
            } else if(f.kind==ConfigField::Font) {
                auto *font=new QFontComboBox; control=font;
                connect(font,&QFontComboBox::currentFontChanged,this,[this](const QFont &){changed();});
            } else {
                auto *button=new QPushButton; control=button;
                connect(button,&QPushButton::clicked,this,[this,button]{
                    QColorDialog dialog(button->property("color").value<QColor>(),this);
                    dialog.setOption(QColorDialog::ShowAlphaChannel); dialog.setWindowTitle(QStringLiteral("选择颜色与透明度"));
                    const QColor before=button->property("color").value<QColor>();
                    auto update=[this,button](const QColor &color){
                        button->setProperty("color",color); button->setText(color.name(QColor::HexArgb));
                        button->setStyleSheet(QString("QPushButton { border-left: 28px solid %1; padding: 5px; }").arg(color.name(QColor::HexRgb)));
                        changed();
                    };
                    connect(&dialog,&QColorDialog::currentColorChanged,this,update);
                    if(dialog.exec()!=QDialog::Accepted) update(before);
                });
            }
            if(f.key=="Interface/ShowCandidatePinyin")control->setToolTip(QStringLiteral("在汉字上方逐字显示带声调的拼音；轻声不标调。与顶部输入栏分别控制。"));
            if(f.key=="Interface/ShowPreedit")control->setToolTip(QStringLiteral("显示正在输入的编码；候选词读音由“显示候选词拼音”单独控制。"));
            if(f.key=="Geometry/PinyinGap")control->setToolTip(QStringLiteral("调整上方拼音与下方汉字的垂直距离。0 表示两行紧贴；不会压缩文字或裁掉声调。"));
            if(f.key=="Geometry/RowHeight")control->setToolTip(QStringLiteral("拼音与汉字合起来的整体行高；0 为自动。过小时按字体与间距所需高度展开，避免裁字。"));
            control->setObjectName(f.key); m_controls.insert(f.key,control); form->addRow(f.label,control);
        }
        auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(page);
        tabs->addTab(scroll,names[i]);
    }
    auto *previewTitle=new QLabel(QStringLiteral("实时预览 · 与实际候选窗使用同一绘制代码，可点击切换选中词"));
    previewTitle->setWordWrap(true); layout->addWidget(previewTitle);
    auto *scroll=new QScrollArea; scroll->setObjectName("previewScroll"); scroll->setMinimumHeight(180);
    scroll->setMaximumHeight(290); scroll->setWidgetResizable(false);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->installEventFilter(this);
    scroll->setStyleSheet("QScrollArea {background:#26364a;border:1px solid #708399;border-radius:6px;}");
    m_preview=new CandidateWindow(nullptr,true); m_preview->setObjectName("settingsPreview");
    scroll->setWidget(m_preview); m_preview->setAutoFillBackground(false); layout->addWidget(scroll);
    auto *background=new QComboBox; background->setObjectName("previewBackground");
    background->addItems({QStringLiteral("彩色背景"),QStringLiteral("白色背景"),QStringLiteral("深色背景")});
    layout->insertWidget(layout->indexOf(scroll),background);
    connect(background,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,scroll](int mode){
        m_preview->setPreviewBackground(mode);
        scroll->setStyleSheet(QString("QScrollArea {background:%1;border:1px solid #708399;border-radius:6px;}").arg(mode==1?"#fafafa":mode==2?"#232323":"#26364a"));
    });
    connect(m_preview,&CandidateWindow::selected,this,[this](int index){m_preview->state.cursor=index; m_preview->schedule();});
    m_status=new QLabel; m_status->setWordWrap(true); layout->addWidget(m_status);
    auto *buttons=new QDialogButtonBox;
    auto *reset=buttons->addButton(QStringLiteral("恢复默认"),QDialogButtonBox::ResetRole); reset->setObjectName("resetDefaults");
    auto *apply=buttons->addButton(QStringLiteral("应用"),QDialogButtonBox::ApplyRole); apply->setObjectName("applySettings");
    auto *save=buttons->addButton(QStringLiteral("保存并关闭"),QDialogButtonBox::AcceptRole);
    auto *cancel=buttons->addButton(QStringLiteral("关闭（不保存预览）"),QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(reset,&QPushButton::clicked,this,&SettingsDialog::resetDefaults);
    connect(apply,&QPushButton::clicked,this,[this]{applySettings();});
    connect(save,&QPushButton::clicked,this,[this]{m_closeAfterSave=true;if(!applySettings())m_closeAfterSave=false;});
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(presets,qOverload<int>(&QComboBox::activated),this,[this](int index){
        if(index==0) m_draft=m_live.config();
        else {m_draft=CandidateConfig();
            if(index==2) {m_draft.fontSize=13;m_draft.windowPadding=2;m_draft.horizontalPadding=12;m_draft.itemSidePadding=4;m_draft.itemHorizontalPadding=20;m_draft.candidateGap=1;m_draft.headerGap=0;m_draft.cornerRadius=12;}
            if(index==3) {m_draft.fontSize=20;m_draft.bold=true;m_draft.shade=.7;}
        }
        populate(); changed();
    });
    m_previewTimer.setSingleShot(true); m_previewTimer.setInterval(60);
    connect(&m_previewTimer,&QTimer::timeout,this,[this]{m_preview->setConfiguration(m_draft);m_preview->refresh();});
    populate(); m_preview->setConfiguration(m_draft); m_preview->showPreview();
    m_status->setText(QStringLiteral("当前默认沿用现有玻璃效果；预览背景为固定示例，实际效果随窗口背后的内容变化。"));
}
bool SettingsDialog::eventFilter(QObject *object,QEvent *event) {
    if(event->type()==QEvent::Resize && m_preview && object==m_preview->parentWidget())
        m_preview->setPreviewWidth(qobject_cast<QWidget *>(object)->width());
    return QDialog::eventFilter(object,event);
}
void SettingsDialog::populate() {
    m_populating=true;
    for(const auto &f:candidateFields()) {
        QWidget *w=m_controls.value(f.key); const auto v=f.get(m_draft);
        if(auto *n=qobject_cast<QDoubleSpinBox *>(w)) n->setValue(v.toDouble());
        else if(auto *c=qobject_cast<QCheckBox *>(w)) c->setChecked(v.toBool());
        else if(auto *font=qobject_cast<QFontComboBox *>(w)) font->setCurrentFont(QFont(v.toString()));
        else if(auto *combo=qobject_cast<QComboBox *>(w)) combo->setCurrentIndex(v.toInt());
        else if(auto *button=qobject_cast<QPushButton *>(w)) {
            QColor color=v.value<QColor>();button->setProperty("color",color);button->setText(color.name(QColor::HexArgb));
            button->setStyleSheet(QString("QPushButton {border-left:28px solid %1;padding:5px;}").arg(color.name(QColor::HexRgb)));
        }
    }
    m_populating=false;
}
void SettingsDialog::changed() {
    if(m_populating) return;
    for(const auto &f:candidateFields()) {
        QWidget *w=m_controls.value(f.key); QVariant value;
        if(auto *n=qobject_cast<QDoubleSpinBox *>(w)) value=n->value();
        else if(auto *c=qobject_cast<QCheckBox *>(w)) value=c->isChecked();
        else if(auto *font=qobject_cast<QFontComboBox *>(w)) value=font->currentFont().family();
        else if(auto *combo=qobject_cast<QComboBox *>(w)) value=combo->currentIndex();
        else value=w->property("color").value<QColor>().name(QColor::HexArgb);
        f.set(m_draft,value);
    }
    m_draft.maxWindowWidth=qMax(m_draft.minWindowWidth,m_draft.maxWindowWidth);
    m_previewTimer.start(); m_status->setText(QStringLiteral("预览已更改，尚未保存。候选词会随可用宽度自动换行；0 表示自动尺寸或继承字号。"));
}
void SettingsDialog::resetDefaults() {m_draft=CandidateConfig();populate();changed();}
void SettingsDialog::reject() {
    if(m_saving){m_status->setText(QStringLiteral("正在保存配置，请稍候…"));return;}
    QDialog::reject();
}
bool SettingsDialog::applySettings() {
    if(m_saving)return false;
    QTemporaryDir staging;
    const QString prepared=staging.path()+"/appearance.ini";
    if(!staging.isValid()||!saveCandidateConfig(m_draft,prepared))return false;
    QFile file(prepared);if(!file.open(QIODevice::ReadOnly))return false;
    const QJsonObject payload{{"appearance",QString::fromUtf8(file.readAll())},{"pageSize",m_draft.pageSize},{"pinyin",m_draft.showCandidatePinyin}};
    const CandidateConfig pending=m_draft;
    m_saving=true;setEnabled(false);m_status->setText(QStringLiteral("正在保存外观与 Rime 配置…"));
    auto *process=new QProcess(this);auto *timer=new QTimer(process);timer->setSingleShot(true);
    auto completed=std::make_shared<bool>(false);
    auto finish=[this,process,timer,pending,completed](bool ok){
        if(*completed)return;*completed=true;timer->stop();m_saving=false;setEnabled(true);
        const auto result=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        if(ok){m_live.setConfiguration(pending);if(result.value("changed").toBool())emit m_live.rimeSettingsChanged();m_status->setText(QStringLiteral("已保存并应用；无需重启输入法。"));}
        else m_status->setText(QStringLiteral("配置未保存，原配置已保留。请重试；若曾中断保存，下次保存会先恢复配置。\n")+QString::fromUtf8(process->readAllStandardError()).left(500));
        emit settingsApplied(ok);process->deleteLater();
        const bool close=m_closeAfterSave;m_closeAfterSave=false;if(ok&&close)accept();
    };
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus status){finish(status==QProcess::NormalExit&&code==0);});
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    connect(timer,&QTimer::timeout,process,[process]{process->kill();});
    const QString helper=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../manage.py");
    process->start("/usr/bin/python3",{helper,"save-settings"});
    process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));process->closeWriteChannel();timer->start(15000);
    return true;
}
