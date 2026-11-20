#include "SettingsDialog.h"
#include "StartMenu.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QSignalBlocker>

SettingsDialog::SettingsDialog(StartMenu *menu, QWidget *parent)
    : LiquidDialog::Dialog(parent, Qt::Window|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint), m_menu(menu)
{
    setObjectName("launcherSettingsDialog");
    setWindowTitle(QString::fromUtf8("开始菜单设置"));
    setMinimumSize(400, 390);
    resize(460,440);
    buildUi();
    loadCurrent();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    // 字体组
    auto *fontBox = new QGroupBox(QString::fromUtf8("字体"), this);
    fontBox->setObjectName("launcherFontGroup");
    auto *fontForm = new QFormLayout(fontBox);
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setObjectName("launcherFont");
    m_fontCombo->setEditable(false);
    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setObjectName("launcherFontSize");
    m_sizeSpin->setRange(9, 22);
    m_sizeSpin->setSingleStep(1);
    m_sizeSpin->setSuffix(QString::fromUtf8(" pt"));
    fontForm->addRow(QString::fromUtf8("字体"), m_fontCombo);
    fontForm->addRow(QString::fromUtf8("字号"), m_sizeSpin);
    root->addWidget(fontBox);

    root->addStretch();

    // 按钮区
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_resetBtn = new QPushButton(QString::fromUtf8("恢复默认"), this);
    m_resetBtn->setObjectName("launcherReset");
    m_applyBtn = new QPushButton(QString::fromUtf8("应用"), this);
    m_applyBtn->setObjectName("launcherApply");
    m_applyBtn->setDefault(true);
    m_closeBtn = new QPushButton(QString::fromUtf8("关闭"), this);
    m_closeBtn->setObjectName("launcherClose");
    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_closeBtn);
    root->addLayout(btnRow);

    connect(m_applyBtn, &QPushButton::clicked, this, &SettingsDialog::apply);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsDialog::reset);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void SettingsDialog::loadCurrent()
{
    StartMenuConfig &cfg = StartMenuConfig::instance();
    const QString fam = cfg.fontFamily();
    if (!fam.isEmpty())
        m_fontCombo->setCurrentFont(QFont(fam));
    m_sizeSpin->setValue(cfg.fontSize());
}

void SettingsDialog::apply()
{
    StartMenuConfig &cfg = StartMenuConfig::instance();
    const QString family=m_fontCombo->currentFont().family();
    if(!cfg.setAppearance(Skin::EcoLiquid,family,m_sizeSpin->value(),cfg.panelOpacity()))return;
    m_menu->applyFont(family,m_sizeSpin->value());
}

void SettingsDialog::reset()
{
    m_fontCombo->setCurrentFont(QFont("sans-serif"));
    m_sizeSpin->setValue(14);
    apply();
}
