#include "Drawer.h"
#include "SettingsBridge.h"
#include "ResidentStartup.h"
#include <QCheckBox>
#include "LiquidDialog.h"
#include "UiColors.h"
#include "SettingsHelp.h"
#include "WindowIdentity.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QWindow>
#include <memory>

QImage drawerWallpaper(const QRect &area,const QRect &screen,qreal dpr);

QRect Drawer::preferencesGeometry(QSize size) const {
    auto *screen=panelAnchor.isValid()?QGuiApplication::screenAt(panelAnchor.center()):nullptr;
    if(!screen)screen=windowHandle()&&windowHandle()->screen()?windowHandle()->screen():QGuiApplication::primaryScreen();
    const QRect available=screen->availableGeometry().adjusted(8,8,-8,-8);size=size.boundedTo(available.size());
    const QRect anchor=panelAnchor.isValid()?panelAnchor:targetGeometry();
    const bool below=panelAnchor.isValid()&&anchor.center().y()<available.center().y();
    if(panelAnchor.isValid())size.setHeight(qMin(size.height(),qMax(220,below?available.bottom()-anchor.bottom()-8:anchor.top()-available.top()-8)));
    const int x=qBound(available.left(),anchor.right()-size.width()+1,available.right()-size.width()+1);
    const int y=qBound(available.top(),below?anchor.bottom()+8:anchor.top()-size.height()-8,available.bottom()-size.height()+1);
    return QRect(QPoint(x,y),size);
}
void Drawer::PreferencesAt(int x,int y,int width,int height) {
    const QRect anchor(x,y,width,height);
    if(width>0&&height>0&&width<4096&&height<4096&&QGuiApplication::screenAt(anchor.center()))panelAnchor=anchor;
    Preferences();
}
void Drawer::Preferences() {
    if(LquiSettings::open("ai:general"))return;
    hideTimer.stop();if(preferences){if(LquiSettings::buildingForm())return;preferences->setGeometry(preferencesGeometry(preferences->size()));preferences->show();preferences->raise();preferences->activateWindow();return;}
    QScreen *screen=panelAnchor.isValid()?QGuiApplication::screenAt(panelAnchor.center()):nullptr;
    if(!screen)screen=windowHandle()&&windowHandle()->screen()?windowHandle()->screen():QGuiApplication::primaryScreen();
    LiquidDialog::Options options;options.colors=false;options.placement=[this](QSize size){return preferencesGeometry(size);};options.backdrop=[](const QRect &area,qreal dpr){auto *screen=QGuiApplication::screenAt(area.center());if(!screen)screen=QGuiApplication::primaryScreen();return drawerWallpaper(area,screen->geometry(),dpr);};
    auto *dialog=new LiquidDialog::Dialog(nullptr,Qt::Window|Qt::WindowStaysOnTopHint,options);preferences=dialog;
    dialog->setFont(font());
    dialog->setObjectName("cliPreferences");
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("AI小站设置");dialog->setWindowIcon(QIcon(":/icons/app.svg"));
    const auto available=screen->availableGeometry();dialog->resize(qMin(540,available.width()-30),qMin(720,available.height()-40));
    const auto colors=UiColors::forSurface(lightSurface);dialog->setPalette(colors.palette(dialog->palette()));
    dialog->setStyleSheet(QString(
        "QDialog#cliPreferences{background:transparent;} QWidget{color:%1;} QLabel{background:transparent;} QLineEdit,QComboBox{background:%4;border:1px solid %5;border-radius:7px;padding:5px 8px;min-height:22px;}"
        "QComboBox QAbstractItemView{background:%4;color:%1;selection-background-color:#397d73;selection-color:white;}"
        "QPushButton{color:%1;border:1px solid %5;border-radius:8px;padding:5px 10px;background:rgba(130,190,170,25);}"
        "QPushButton:hover{background:rgba(130,190,170,55);} QPushButton:focus,QComboBox:focus,QLineEdit:focus{border-color:%3;}"
        "QLabel#cliModelStatus{color:%1;font-size:11px;} QLabel#cliModelReference,QLabel#settingsNote{color:%2;font-size:11px;} QLabel[sliderValue=true]{color:%1;}"
        "QSlider:horizontal{min-height:26px;background:transparent;} QSlider::groove:horizontal{height:4px;background:rgba(140,170,185,70);border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:%3;border-radius:2px;} QSlider::handle:horizontal{width:14px;margin:-5px 0;border-radius:7px;background:%1;border:1px solid %5;}"
        "QSlider::handle:horizontal:focus{border:2px solid %3;} QSlider:disabled{color:%2;}"
        "QScrollArea#cliSettingsScroll,QWidget#cliSettingsViewport,QWidget#cliSettingsFields{background:transparent;border:0;}")
        .arg(colors.text,colors.muted,colors.accent,colors.field,colors.border));
    auto *layout=new QVBoxLayout(dialog);auto *scroll=new QScrollArea(dialog);scroll->setObjectName("cliSettingsScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *scrollbar=scroll->verticalScrollBar();scrollbar->setObjectName("cliSettingsScrollBar");scrollbar->setAutoFillBackground(false);
    scrollbar->setStyleSheet(QString(
        "QScrollBar:vertical{background:transparent;border:0;width:8px;margin:2px 0;}"
        "QScrollBar::handle:vertical{background:rgba(145,175,185,100);min-height:30px;border:0;border-radius:3px;margin:0 1px;}"
        "QScrollBar::handle:vertical:hover{background:rgba(145,195,185,170);}"
        "QScrollBar::handle:vertical:pressed{background:%1;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;width:0;border:0;background:transparent;}"
        "QScrollBar::up-arrow:vertical,QScrollBar::down-arrow:vertical{height:0;width:0;background:transparent;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}").arg(colors.accent));
    auto *fields=new QWidget(scroll);fields->setObjectName("cliSettingsFields");fields->setAutoFillBackground(false);
    auto *form=new QFormLayout(fields);form->setVerticalSpacing(10);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);scroll->setWidget(fields);
    scroll->viewport()->setObjectName("cliSettingsViewport");scroll->viewport()->setAutoFillBackground(false);layout->addWidget(scroll,1);
    auto number=[&](const QString &label,const QString &name,int value,int low,int high,const QString &suffix,int step=1){
        auto *row=new QWidget(fields);auto *line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);line->setSpacing(10);
        auto *w=new QSlider(Qt::Horizontal,row);w->setObjectName(name);w->setRange(low,high);w->setSingleStep(step);w->setPageStep(step*5);w->setValue(value);w->setProperty("settingsSuffix",suffix);w->setAccessibleName(label);w->setFocusPolicy(Qt::StrongFocus);
        auto *readout=new QLabel(row);readout->setObjectName(name+"Value");readout->setProperty("sliderValue",true);readout->setMinimumWidth(66);readout->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        const auto update=[=](int v){readout->setText(name=="settingIdleStop"&&v==0?QString("不休眠"):QString::number(v)+suffix);w->setAccessibleDescription(readout->text());};
        connect(w,&QSlider::valueChanged,row,update);update(value);line->addWidget(w,1);line->addWidget(readout);form->addRow(label,row);return w;
    };
    auto choice=[&](const QString &label,const QString &name,const QList<QPair<QString,QString>> &items,const QString &current){
        auto *w=new QComboBox(fields);w->setObjectName(name);for(const auto &i:items)w->addItem(i.first,i.second);
        w->setCurrentIndex(qMax(0,w->findData(current)));form->addRow(label,w);return w;
    };
    auto draft=std::make_shared<Settings>(settings);
    auto *backendChoice=choice("AI 后台","settingBackend",{{"Codex","codex"},{"千问 Qwen Code","qwen"},{"DeepSeek Harness","deepseek"}},settings.backend);
    auto *modelRow=new QWidget(fields);auto *modelLayout=new QHBoxLayout(modelRow);modelLayout->setContentsMargins(0,0,0,0);modelLayout->setSpacing(6);
    auto *model=new QComboBox(modelRow);model->setObjectName("settingModel");model->setMinimumContentsLength(12);model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);model->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    auto *refresh=new QPushButton("刷新",modelRow);refresh->setObjectName("refreshModels");modelLayout->addWidget(model,1);modelLayout->addWidget(refresh);form->addRow("模型版本",modelRow);
    auto *effort=number("思考强度","settingEffort",0,0,0,"");auto *effortLabel=fields->findChild<QLabel*>("settingEffortValue");
    auto *sessionMode=choice("会话模式","settingSessionMode",{{"跟随会话配置",""}},settings.sessionMode);
    auto *modelStatus=new QLabel(fields);modelStatus->setObjectName("cliModelStatus");modelStatus->setWordWrap(true);form->addRow(QString(),modelStatus);
    auto *modelReference=new QLabel(fields);modelReference->setObjectName("cliModelReference");modelReference->setWordWrap(true);modelReference->hide();form->addRow(QString(),modelReference);
    auto *width=number("窗口宽度","settingWidth",settings.width,360,1000," px",10);auto *height=number("窗口高度","settingHeight",settings.height,420,1400," px",10);
    auto *animation=choice("呼出方式","settingAnimation",{{"上浮（轻滑 + 淡入）","float"},{"向上滑入","slide"},{"无动画","none"}},settings.animation);
    auto *duration=number("动画时长","settingDuration",settings.animationMs,80,500," ms",10);
    auto *delay=number("失焦后收起","settingDelay",settings.hideDelay,200,3000," ms",50);
    auto *sharedMaterial=new QCheckBox("与开始菜单共用液态材质",fields);sharedMaterial->setObjectName("settingFollowLauncherMaterial");sharedMaterial->setChecked(settings.followLauncherMaterial);form->addRow("窗口材质",sharedMaterial);
    auto *liquid=number("液态强度","settingLiquid",settings.liquid,0,100," %");auto *darken=number("背景压暗","settingDarken",settings.darken,0,65," %");
    auto updateMaterialControls=[=]{liquid->setEnabled(!sharedMaterial->isChecked());darken->setEnabled(!sharedMaterial->isChecked());};connect(sharedMaterial,&QCheckBox::toggled,dialog,updateMaterialControls);updateMaterialControls();
    auto *font=number("文字大小","settingFont",settings.fontSize,8,20," pt");
    auto *translation=new QLineEdit(settings.translationLanguage,fields);translation->setObjectName("settingTranslationLanguage");translation->setMaxLength(80);translation->setToolTip("截图翻译的目标语言；点击翻译立即发送截图进行处理。");form->addRow("截图翻译语言",translation);
    auto *dictationNote=new QLabel("Codex 听写暂未接入。当前桌面客户端单次上限为 9 分 55 秒（26.928.21956 版本）。",fields);dictationNote->setObjectName("dictationAvailability");dictationNote->setWordWrap(true);form->addRow("语音听写",dictationNote);
    auto *directory=new QLineEdit(settings.directory,fields);directory->setObjectName("settingDirectory");form->addRow("工作目录",directory);
    auto *command=new QLineEdit(settings.command,fields);command->setObjectName("settingCommand");form->addRow("CLI 命令",command);
    auto *permissions=choice("执行权限","settingPermissions",{{"跟随 Codex 配置","config"},{"每次询问","ask"},{"工作目录内自动","workspace"},{"完全放开","full"}},settings.permissions);
    auto *limit=number("压缩提示","settingContextLimit",settings.contextLimitK,32,765," K",1);
    auto *idle=number("模型后台休眠","settingIdleStop",settings.idleStopMinutes,0,240," 分钟");
    auto *resident=new QCheckBox("登录时常驻，点击小球立即打开",fields);resident->setObjectName("settingResident");resident->setChecked(ResidentStartup::enabled());form->addRow("界面常驻",resident);
    auto *note=new QLabel("当前后台的模型与强度选择后即保存；切换后台及其他选项点“应用”。\n每个后台分别保留聊天、草稿与会话。",dialog);note->setObjectName("settingsNote");note->setWordWrap(true);layout->addWidget(note);

    struct Catalog {QJsonArray models;QString model,effort;bool ready=false;};auto catalog=std::make_shared<Catalog>();
    auto probe=std::make_shared<QPointer<AgentClient>>();
    const auto followText=[=]{return draft->backend=="codex"?QString("跟随 Codex 配置"):QString("跟随会话配置");};
    auto updateEfforts=[=](const QString &wanted){
        QString selected=model->currentData().toString();const bool follow=selected.isEmpty();if(follow)selected=catalog->model;
        QJsonObject entry;
        for(const auto &v:catalog->models){const auto m=v.toObject();if(m.value("model").toString()==selected){entry=m;break;}if(selected.isEmpty()&&m.value("isDefault").toBool())entry=m;}
        QString defaultEffort=follow&&!catalog->effort.isEmpty()?catalog->effort:entry.value("defaultReasoningEffort").toString();
        QStringList values{QString()};for(const auto &v:entry.value("supportedReasoningEfforts").toArray())values<<v.toObject().value("reasoningEffort").toString();
        if(entry.isEmpty()&&!wanted.isEmpty())values<<wanted;
        const QSignalBlocker block(effort);effort->setProperty("efforts",values);effort->setProperty("defaultEffort",defaultEffort);effort->setRange(0,values.size()-1);
        effort->setValue(qMax(0,values.indexOf(wanted)));effort->setEnabled(values.size()>1);
        QStringList labels=values;labels[0]=defaultEffort.isEmpty()?"跟随":"默认 "+defaultEffort;effort->setProperty("settingsValueLabels",labels);
        effortLabel->setText(effort->value()==0?(defaultEffort.isEmpty()?"跟随":"默认 "+defaultEffort):values[effort->value()]);
    };
    const auto collectSelection=[=]{
        draft->command=command->text();draft->directory=directory->text();draft->model=model->currentData().toString();
        draft->reasoningEffort=effort->property("efforts").toStringList().value(effort->value());
        draft->sessionMode=sessionMode->currentData().toString();draft->permissions=permissions->currentData().toString();
    };
    const auto updateModelStatus=[=]{
        if(!catalog->ready)return;
        const QString selected=model->currentData().toString();QString name=model->currentText();
        if(selected.isEmpty()) {
            name=catalog->model;
            for(const auto &v:catalog->models){const auto entry=v.toObject();if(entry.value("model").toString()==catalog->model){name=entry.value("displayName").toString(catalog->model);break;}}
        }
        if(name.isEmpty())name="自动选择模型";
        QString strength=effort->property("efforts").toStringList().value(effort->value());
        if(strength.isEmpty())strength=effort->property("defaultEffort").toString();
        modelStatus->setText((draft->backend==settings.backend?QString("下一条消息："):QString("切换后使用："))+name+" · "+(strength.isEmpty()?QString("自动强度"):strength)+(draft->backend==settings.backend?QString():"（待应用）"));
        modelReference->setText((draft->backend=="codex"?QString("CLI 默认（仅供参考）："):QString("会话原配置（仅供参考）："))+
            (catalog->model.isEmpty()?QString("自动"):catalog->model)+(catalog->effort.isEmpty()?QString():" · "+catalog->effort));
        modelReference->show();
    };
    const auto saveModel=[=]{
        if(!catalog->ready)return;collectSelection();
        if(draft->backend!=settings.backend){updateModelStatus();return;}
        auto next=settings;next.model=draft->model;next.reasoningEffort=draft->reasoningEffort;next.sessionMode=draft->sessionMode;
        QString error;if(!ResidentStartup::save(resident->isChecked(),&error)){note->setText("启动项保存失败："+error);return;}if(!next.save(&error)){modelStatus->setText("保存失败："+error);return;}
        applySettings(next);updateModelStatus();
    };
    connect(model,QOverload<int>::of(&QComboBox::activated),dialog,[=]{
        updateEfforts(draft->reasoningEffort);saveModel();
        if(draft->backend!="codex")refresh->click(); // Model-dependent ACP options.
    });
    connect(sessionMode,QOverload<int>::of(&QComboBox::activated),dialog,[=]{saveModel();});
    connect(effort,&QSlider::valueChanged,dialog,[=](int value){
        const auto values=effort->property("efforts").toStringList();
        effortLabel->setText(value==0?(effort->property("defaultEffort").toString().isEmpty()?"跟随":"默认 "+effort->property("defaultEffort").toString()):values.value(value));saveModel();
    });
    const auto resetBackend=[=]{
        modelReference->hide();
        refresh->setEnabled(true);model->setEnabled(true);
        catalog->ready=false;catalog->models={};catalog->model.clear();catalog->effort.clear();
        {const QSignalBlocker block(model);model->clear();model->addItem(followText(),QString());if(!draft->model.isEmpty())model->addItem(draft->model,draft->model);model->setCurrentIndex(qMax(0,model->findData(draft->model)));}
        command->setText(draft->command);directory->setText(draft->directory);permissions->setCurrentIndex(qMax(0,permissions->findData(draft->permissions)));
        const bool codex=draft->backend=="codex";permissions->setVisible(codex);form->labelForField(permissions)->setVisible(codex);
        sessionMode->setVisible(!codex);form->labelForField(sessionMode)->setVisible(!codex);
        {const QSignalBlocker block(sessionMode);sessionMode->clear();sessionMode->addItem("跟随会话配置",QString());if(!draft->sessionMode.isEmpty())sessionMode->addItem(draft->sessionMode,draft->sessionMode);sessionMode->setCurrentIndex(qMax(0,sessionMode->findData(draft->sessionMode)));}
        updateEfforts(draft->reasoningEffort);
    };
    connect(refresh,&QPushButton::clicked,dialog,[=]{
        if(command->text().trimmed().isEmpty()||!QDir(directory->text()).exists()){modelStatus->setText("请先填写有效的目录和 CLI 命令");return;}
        if(*probe){(*probe)->disconnect(dialog);(*probe)->stop();(*probe)->deleteLater();}
        collectSelection();auto config=*draft;config.directory=directory->text();config.normalize();
        auto *current=createAgentClient(config,dialog);*probe=current;
        catalog->ready=false;modelReference->hide();model->setEnabled(false);effort->setEnabled(false);refresh->setEnabled(false);modelStatus->setText("正在读取 "+config.cliName+" 配置…");
        connect(current,&AgentClient::modesReady,dialog,[=](const QJsonArray &modes,const QString &currentMode){
            if(*probe!=current)return;const QSignalBlocker block(sessionMode);sessionMode->clear();sessionMode->addItem(currentMode.isEmpty()?"跟随会话配置":"跟随会话 · "+currentMode,QString());
            for(const auto &v:modes){const auto o=v.toObject();sessionMode->addItem(o.value("name").toString(o.value("id").toString()),o.value("id").toString());}
            if(!draft->sessionMode.isEmpty()&&sessionMode->findData(draft->sessionMode)<0)sessionMode->addItem(draft->sessionMode+"（当前配置）",draft->sessionMode);
            sessionMode->setCurrentIndex(qMax(0,sessionMode->findData(draft->sessionMode)));sessionMode->setEnabled(!modes.isEmpty()||!draft->sessionMode.isEmpty());
            const bool visible=!modes.isEmpty()||!draft->sessionMode.isEmpty();sessionMode->setVisible(visible);form->labelForField(sessionMode)->setVisible(visible);
        });
        connect(current,&AgentClient::modelsReady,dialog,[=](const QJsonArray &models,const QString &defaultModel,const QString &strength){
            if(*probe!=current)return;catalog->models=models;catalog->model=defaultModel;catalog->effort=strength;
            {const QSignalBlocker block(model);model->clear();model->addItem(followText()+(defaultModel.isEmpty()?QString():" · "+defaultModel),QString());
                for(const auto &v:models){const auto m=v.toObject();const QString id=m.value("model").toString();if(id.isEmpty()||m.value("hidden").toBool())continue;model->addItem(m.value("displayName").toString(id),id);model->setItemData(model->count()-1,id,Qt::ToolTipRole);}
                if(!draft->model.isEmpty()&&model->findData(draft->model)<0)model->addItem(draft->model+"（当前配置）",draft->model);
                model->setCurrentIndex(qMax(0,model->findData(draft->model)));}
            updateEfforts(draft->reasoningEffort);catalog->ready=true;model->setEnabled(true);refresh->setEnabled(true);
            updateModelStatus();
        });
        connect(current,&AgentClient::modelsFailed,dialog,[=](const QString &error){if(*probe!=current)return;modelStatus->setText("读取失败："+error.left(220));refresh->setEnabled(true);model->setEnabled(true);});
        current->discoverModels();
    });
    connect(backendChoice,QOverload<int>::of(&QComboBox::activated),dialog,[=]{
        collectSelection();draft->selectBackend(backendChoice->currentData().toString());resetBackend();refresh->click();
    });
    resetBackend();
    auto *clear=new QPushButton("清空聊天记录并开始新对话…",dialog);clear->setObjectName("clearConversation");layout->addWidget(clear,0,Qt::AlignLeft);
    connect(clear,&QPushButton::clicked,dialog,[this,dialog,note]{
        if(LiquidDialog::question(dialog,"清空对话","清空 "+settings.cliName+" 的聊天记录和附图，并开始新对话？\n此操作无法撤销。")!=QMessageBox::Yes)return;
        clearConversation();note->setText("已清空。下一条消息会开始新的对话。");
    });
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close,dialog);buttons->button(QDialogButtonBox::Apply)->setText("应用");buttons->button(QDialogButtonBox::Close)->setText("关闭");layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,dialog,[=]{
        if(!QDir(directory->text()).exists()||command->text().trimmed().isEmpty()){note->setText("请填写存在的目录和有效的 CLI 命令。");return;}
        if(draft->backend!=settings.backend&&backend->busy()){note->setText("请先停止当前回复再切换后台。");return;}
        collectSelection();auto next=*draft;next.width=width->value();next.height=height->value();next.animation=animation->currentData().toString();next.animationMs=duration->value();
        next.followLauncherMaterial=sharedMaterial->isChecked();next.residentOnLogin=resident->isChecked();next.hideDelay=delay->value();next.liquid=liquid->value();next.darken=darken->value();next.fontSize=font->value();
        next.translationLanguage=translation->text();next.directory=directory->text();next.command=command->text();next.permissions=permissions->currentData().toString();next.contextLimitK=limit->value();next.idleStopMinutes=idle->value();next.normalize();
        QString error;if(!ResidentStartup::save(resident->isChecked(),&error)){note->setText("启动项保存失败："+error);return;}if(!next.save(&error)){note->setText("保存失败："+error);return;}applySettings(next);updateModelStatus();note->setText("已应用并保存。");
    });
    connect(buttons->button(QDialogButtonBox::Close),&QPushButton::clicked,dialog,&QDialog::close);
    for(auto *surface:QList<QWidget*>{fields,scroll->viewport()}) {
        QPalette palette=surface->palette();palette.setColor(QPalette::Window,Qt::transparent);palette.setColor(QPalette::Base,Qt::transparent);surface->setPalette(palette);
        surface->setAutoFillBackground(false);
    }
    const QList<QPair<QString,QString>> help{
        {"settingFollowLauncherMaterial","使用开始菜单同一套折射、模糊和自适应压暗效果。关闭后可单独调节小站材质。"},
        {"settingTranslationLanguage","截图翻译使用的目标语言。点击翻译后立即处理截图，不会发送已有草稿。"},
        {"settingResident","只保持界面常驻并预热背景；不会提前连接模型。模型后台休眠不关闭窗口进程。"},
        {"settingBackend","选择 CLI 后台；每个后台分别保留聊天和草稿，点击应用后切换。"},
        {"settingModel","读取所选 CLI 提供的模型；下一条消息生效。"},
        {"settingSessionMode","仅显示 CLI 提供的模式；授权范围由该后台决定。"},
        {"settingEffort","越向右思考越深入，通常更慢；档位随模型变化。"},
        {"settingWidth","调整聊天窗口的宽度。"},{"settingHeight","调整聊天窗口的高度。"},
        {"settingAnimation","选择窗口从小球附近出现的方式。"},{"settingDuration","调整展开和收起动画的时长。"},
        {"settingDelay","失去焦点后等待多久收起；固定展开时不收起。"},
        {"settingLiquid","调整玻璃的折射与液态质感。"},{"settingDarken","越向右背景越暗，文字对比更强。"},
        {"settingFont","调整聊天正文和输入区的字号。"},{"settingDirectory","CLI 的工作目录；下次连接时生效。"},
        {"settingCommand","填写 CLI 程序及必要参数；协议参数由小站添加。"},
        {"settingPermissions","决定运行命令和修改文件时使用的权限。"},
        {"settingContextLimit","达到所设用量时提醒压缩；最高 765K。"},
        {"settingIdleStop","空闲多久后关闭所选后台；0 为不休眠。"}
    };
    const auto describe=[](QWidget *widget,const QString &title,const QString &body){
        widget->setProperty("settingsHelp",title.isEmpty()?body.toHtmlEscaped():QString("<b>%1</b><br>%2").arg(title.toHtmlEscaped(),body.toHtmlEscaped()));
        widget->setToolTip(body);widget->setToolTipDuration(5000);
    };
    for(const auto &item:help)if(auto *control=dialog->findChild<QWidget*>(item.first)) {
        QWidget *field=control;auto *label=form->labelForField(field);
        if(!label){field=control->parentWidget();label=form->labelForField(field);}
        const QString title=qobject_cast<QLabel*>(label)?qobject_cast<QLabel*>(label)->text():control->accessibleName();
        describe(field,title,item.second);if(label)describe(label,title,item.second);
    }
    describe(refresh,"刷新模型","读取 CLI 模型和可用强度；ACP 会创建临时会话，不发送消息。");
    describe(clear,"开始新对话","清空聊天记录和附图；操作前会再次确认。");
    describe(buttons->button(QDialogButtonBox::Apply),"应用","保存并应用窗口、外观和会话设置。");
    describe(buttons->button(QDialogButtonBox::Close),"关闭","关闭设置，保留已应用的选项。");
    for(const auto &item:QList<QPair<QString,QString>>{{"liquidDialogClose","关闭设置"},{"liquidDialogMinimize","最小化设置"},{"liquidDialogTitle","拖动标题栏可移动设置窗。"}})
        if(auto *widget=dialog->findChild<QWidget*>(item.first))describe(widget,"",item.second);
    new SettingsHelp(dialog);
    dialog->setGeometry(preferencesGeometry(dialog->size()));applyStationWindowIdentity(dialog);if(!LquiSettings::buildingForm()){dialog->show();dialog->raise();dialog->activateWindow();}QTimer::singleShot(0,refresh,&QPushButton::click);
}

QWidget *Drawer::settingsBridgeRoot(){Preferences();return preferences.data();}
