#include "SettingsDialog.h"
#include "../core/FileManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QPageSize>
#include <QPageLayout>
#include <QDoubleSpinBox>

SettingsDialog::SettingsDialog(FileManager *fileManager, QWidget *parent)
    : QDialog(parent)
    , m_fileManager(fileManager)
{
    setMinimumWidth(500);
    setupUi();
    loadSettings();
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    // === 通用 ===
    QWidget *generalTab = new QWidget();
    QVBoxLayout *generalLayout = new QVBoxLayout(generalTab);

    m_startupGroup = new QGroupBox();
    QVBoxLayout *startupLayout = new QVBoxLayout();
    m_restoreSessionCheck = new QCheckBox();
    startupLayout->addWidget(m_restoreSessionCheck);
    m_startupGroup->setLayout(startupLayout);
    generalLayout->addWidget(m_startupGroup);

    generalLayout->addStretch();
    m_tabs->addTab(generalTab, QString());

    // === 编辑器 ===
    QWidget *editorTab = new QWidget();
    QFormLayout *editorForm = new QFormLayout(editorTab);
    m_fontCombo = new QFontComboBox();
    m_fontLabel = new QLabel();
    editorForm->addRow(m_fontLabel, m_fontCombo);
    m_fontSizeSpin = new QSpinBox();
    m_fontSizeSpin->setRange(10, 32);
    m_fontSizeSpin->setSuffix(" px");
    m_fontSizeLabel = new QLabel();
    editorForm->addRow(m_fontSizeLabel, m_fontSizeSpin);
    m_lineNumbersCheck = new QCheckBox();
    m_lineNumbersLabel = new QLabel();
    editorForm->addRow(m_lineNumbersLabel, m_lineNumbersCheck);
    m_wordWrapCheck = new QCheckBox();
    m_wordWrapCheck->setChecked(true);
    m_wordWrapLabel = new QLabel();
    editorForm->addRow(m_wordWrapLabel, m_wordWrapCheck);
    m_tabSizeSpin = new QSpinBox();
    m_tabSizeSpin->setRange(2, 8);
    m_tabSizeSpin->setSuffix(tr(" 空格"));
    m_tabSizeLabel = new QLabel();
    editorForm->addRow(m_tabSizeLabel, m_tabSizeSpin);

    m_widthSpin = new QSpinBox();
    m_widthSpin->setRange(50, 100);
    m_widthSpin->setSingleStep(5);
    m_widthSpin->setSuffix("%");
    m_widthSpin->setValue(80);
    m_widthLabel = new QLabel();
    editorForm->addRow(m_widthLabel, m_widthSpin);
    m_tabs->addTab(editorTab, QString());

    // === 外观 ===
    QWidget *appearanceTab = new QWidget();
    QFormLayout *appearanceForm = new QFormLayout(appearanceTab);
    m_themeCombo = new QComboBox();
    m_themeCombo->addItem(QString(), "system");
    m_themeCombo->addItem(QString(), "light");
    m_themeCombo->addItem(QString(), "dark");
    m_themeLabel = new QLabel();
    appearanceForm->addRow(m_themeLabel, m_themeCombo);
    m_langCombo = new QComboBox();
    m_langCombo->addItem(QString(), "zh_CN");
    m_langCombo->addItem(QString(), "en_US");
    m_langLabel = new QLabel();
    appearanceForm->addRow(m_langLabel, m_langCombo);
    m_customCssEdit = new QPlainTextEdit();
    m_customCssEdit->setPlaceholderText(tr("/* 可选：覆盖编辑器与导出的默认样式 */"));
    m_customCssEdit->setMinimumHeight(120);
    m_customCssLabel = new QLabel();
    appearanceForm->addRow(m_customCssLabel, m_customCssEdit);
    m_tabs->addTab(appearanceTab, QString());

    // === 导出 ===
    QWidget *exportTab = new QWidget();
    QVBoxLayout *exportLayout = new QVBoxLayout(exportTab);

    // 纸张
    QFormLayout *paperForm = new QFormLayout();
    m_paperSizeCombo = new QComboBox();
    m_paperSizeCombo->addItem("A4", QPageSize::A4);
    m_paperSizeCombo->addItem("Letter", QPageSize::Letter);
    m_paperLabel = new QLabel();
    paperForm->addRow(m_paperLabel, m_paperSizeCombo);
    exportLayout->addLayout(paperForm);

    // 页边距
    m_marginGroup = new QGroupBox();
    m_marginGroup->setStyleSheet("QGroupBox{padding:8px 16px;}");
    auto makeMargin = []() {
        auto *s = new QDoubleSpinBox();
        s->setRange(0.5, 5.0); s->setSingleStep(0.1);
        s->setDecimals(1); s->setFixedWidth(80);
        return s;
    };
    m_marginTop = makeMargin(); m_marginBottom = makeMargin();
    m_marginLeft = makeMargin(); m_marginRight = makeMargin();
    QGridLayout *marginGrid = new QGridLayout();
    marginGrid->setHorizontalSpacing(8);
    marginGrid->setColumnStretch(3, 1); // 左组和右组之间留空
    auto makeEdgeLabel = [](const QString &text) {
        QLabel *lbl = new QLabel(text);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return lbl;
    };
    m_marginTopLbl = makeEdgeLabel(tr("上:"));
    m_marginBottomLbl = makeEdgeLabel(tr("下:"));
    m_marginLeftLbl = makeEdgeLabel(tr("左:"));
    m_marginRightLbl = makeEdgeLabel(tr("右:"));
    m_cmTopLbl = new QLabel(tr("厘米"));
    m_cmBottomLbl = new QLabel(tr("厘米"));
    m_cmLeftLbl = new QLabel(tr("厘米"));
    m_cmRightLbl = new QLabel(tr("厘米"));
    marginGrid->addWidget(m_marginTopLbl, 0, 0);
    marginGrid->addWidget(m_marginTop, 0, 1);
    marginGrid->addWidget(m_cmTopLbl, 0, 2);
    marginGrid->addWidget(m_marginBottomLbl, 0, 4);
    marginGrid->addWidget(m_marginBottom, 0, 5);
    marginGrid->addWidget(m_cmBottomLbl, 0, 6);
    marginGrid->addWidget(m_marginLeftLbl, 1, 0);
    marginGrid->addWidget(m_marginLeft, 1, 1);
    marginGrid->addWidget(m_cmLeftLbl, 1, 2);
    marginGrid->addWidget(m_marginRightLbl, 1, 4);
    marginGrid->addWidget(m_marginRight, 1, 5);
    marginGrid->addWidget(m_cmRightLbl, 1, 6);
    m_marginGroup->setLayout(marginGrid);
    exportLayout->addWidget(m_marginGroup);

    // 方向（图标选择）
    m_orientGroup = new QGroupBox();
    QVBoxLayout *orientOuter = new QVBoxLayout();
    QHBoxLayout *orientLayout = new QHBoxLayout();
    m_portraitBtn = new QPushButton();
    m_portraitBtn->setIcon(QIcon(":/icons/light/portrait.svg"));
    m_portraitBtn->setIconSize(QSize(32, 40));
    m_portraitBtn->setCheckable(true);
    m_portraitBtn->setFixedSize(60, 60);
    m_landscapeBtn = new QPushButton();
    m_landscapeBtn->setIcon(QIcon(":/icons/light/landscape.svg"));
    m_landscapeBtn->setIconSize(QSize(40, 32));
    m_landscapeBtn->setCheckable(true);
    m_landscapeBtn->setFixedSize(60, 60);
    connect(m_portraitBtn, &QPushButton::clicked, this, [this]() {
        m_selectedOrientation = QPageLayout::Portrait;
        m_portraitBtn->setChecked(true);
        m_landscapeBtn->setChecked(false);
    });
    connect(m_landscapeBtn, &QPushButton::clicked, this, [this]() {
        m_selectedOrientation = QPageLayout::Landscape;
        m_landscapeBtn->setChecked(true);
        m_portraitBtn->setChecked(false);
    });
    orientLayout->addWidget(m_portraitBtn);
    orientLayout->addWidget(m_landscapeBtn);
    orientLayout->addStretch();
    // 文字标签
    QHBoxLayout *labelLayout = new QHBoxLayout();
    m_portraitLbl = new QLabel();
    m_portraitLbl->setAlignment(Qt::AlignCenter);
    m_portraitLbl->setFixedWidth(60);
    m_landscapeLbl = new QLabel();
    m_landscapeLbl->setAlignment(Qt::AlignCenter);
    m_landscapeLbl->setFixedWidth(60);
    labelLayout->addWidget(m_portraitLbl);
    labelLayout->addWidget(m_landscapeLbl);
    labelLayout->addStretch();
    orientOuter->addLayout(orientLayout);
    orientOuter->addLayout(labelLayout);
    m_orientGroup->setLayout(orientOuter);
    exportLayout->addWidget(m_orientGroup);

    // PDF 页眉/页脚（paged.js 分页引擎，任一开启即启用——见 2026-09-06 spec §1）
    m_pdfHeaderCheck = new QCheckBox(exportTab);
    m_pdfFooterCheck = new QCheckBox(exportTab);
    exportLayout->addWidget(m_pdfHeaderCheck);
    exportLayout->addWidget(m_pdfFooterCheck);

    exportLayout->addStretch();
    m_tabs->addTab(exportTab, QString());

    mainLayout->addWidget(m_tabs);

    // 按钮
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // 初始化全部界面文本；语言切换后 MainWindow 会再次调用 retranslateUi()
    retranslateUi();
}

void SettingsDialog::retranslateUi()
{
    setWindowTitle(tr("设置"));

    m_startupGroup->setTitle(tr("启动"));
    m_restoreSessionCheck->setText(tr("启动时恢复上次会话"));

    m_fontLabel->setText(tr("字体:"));
    m_fontSizeLabel->setText(tr("字号:"));
    m_lineNumbersLabel->setText(tr("显示行号:"));
    m_wordWrapLabel->setText(tr("自动换行:"));
    m_tabSizeLabel->setText(tr("Tab 宽度:"));
    m_widthLabel->setText(tr("编辑区宽度:"));

    m_themeLabel->setText(tr("主题:"));
    m_langLabel->setText(tr("界面语言:"));
    m_customCssLabel->setText(tr("自定义 CSS:"));

    m_paperLabel->setText(tr("纸张:"));
    m_marginGroup->setTitle(tr("页边距"));
    m_marginTopLbl->setText(tr("上:"));
    m_marginBottomLbl->setText(tr("下:"));
    m_marginLeftLbl->setText(tr("左:"));
    m_marginRightLbl->setText(tr("右:"));
    m_cmTopLbl->setText(tr("厘米")); m_cmBottomLbl->setText(tr("厘米"));
    m_cmLeftLbl->setText(tr("厘米")); m_cmRightLbl->setText(tr("厘米"));
    m_orientGroup->setTitle(tr("方向"));
    m_portraitLbl->setText(tr("纵向(P)"));
    m_landscapeLbl->setText(tr("横向(L)"));
    m_pdfHeaderCheck->setText(tr("PDF 页眉（文档标题）"));
    m_pdfFooterCheck->setText(tr("PDF 页码（第 X 页 / 共 Y 页）"));

    m_tabs->setTabText(0, tr("通用"));
    m_tabs->setTabText(1, tr("编辑器"));
    m_tabs->setTabText(2, tr("外观"));
    m_tabs->setTabText(3, tr("导出"));

    // 主题下拉三项 + 语言下拉两项（顺序不变，仅改显示文本；data 保持 system/light/dark、zh_CN/en_US）
    m_themeCombo->setItemText(0, tr("跟随系统"));
    m_themeCombo->setItemText(1, tr("亮色"));
    m_themeCombo->setItemText(2, tr("暗色"));
    m_langCombo->setItemText(0, tr("简体中文"));
    m_langCombo->setItemText(1, tr("English"));

    // 自定义 CSS 占位符也随语言刷新
    m_customCssEdit->setPlaceholderText(tr("/* 可选：覆盖编辑器与导出的默认样式 */"));

    if (QDialogButtonBox *bb = findChild<QDialogButtonBox *>()) {
        if (QAbstractButton *ok = bb->button(QDialogButtonBox::Ok)) ok->setText(tr("确定"));
        if (QAbstractButton *cb = bb->button(QDialogButtonBox::Cancel)) cb->setText(tr("取消"));
    }
}

void SettingsDialog::loadSettings()
{
    QSettings settings("md-editor", "md-editor");
    m_restoreSessionCheck->setChecked(
        settings.value("session/restore", true).toBool());
    m_fontCombo->setCurrentText(
        settings.value("editor/font", "Sans Serif").toString());
    m_fontSizeSpin->setValue(settings.value("editor/fontSize", 18).toInt());
    m_lineNumbersCheck->setChecked(
        settings.value("editor/lineNumbers", false).toBool());
    m_wordWrapCheck->setChecked(
        settings.value("editor/wordWrap", true).toBool());
    m_tabSizeSpin->setValue(settings.value("editor/tabSize", 4).toInt());
    m_widthSpin->setValue(settings.value("editor/width", 80).toInt());

    // 导出
    int paperIdx = m_paperSizeCombo->findData(
        settings.value("export/paperSize", QPageSize::A4).toInt());
    if (paperIdx >= 0) m_paperSizeCombo->setCurrentIndex(paperIdx);
    m_selectedOrientation = settings.value("export/orientation",
        static_cast<int>(QPageLayout::Portrait)).toInt();
    if (m_selectedOrientation == QPageLayout::Landscape) {
        m_landscapeBtn->setChecked(true);
        m_portraitBtn->setChecked(false);
    } else {
        m_portraitBtn->setChecked(true);
        m_landscapeBtn->setChecked(false);
    }
    m_marginTop->setValue(settings.value("export/marginTop", 1.8).toDouble());
    m_marginBottom->setValue(settings.value("export/marginBottom", 1.8).toDouble());
    m_marginLeft->setValue(settings.value("export/marginLeft", 1.8).toDouble());
    m_marginRight->setValue(settings.value("export/marginRight", 1.8).toDouble());
    m_pdfHeaderCheck->setChecked(
        settings.value("export/pdfHeaderEnabled", true).toBool());
    m_pdfFooterCheck->setChecked(
        settings.value("export/pdfFooterEnabled", true).toBool());

    // 显示偏好模式（system 时下拉框选「跟随系统」，而非当前实际主题）
    QString mode = settings.value("appearance/theme", "light").toString();
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(mode));

    // 界面语言：预选已存语言，避免英文用户打开设置时误选「简体中文」导致切回中文
    QString lang = settings.value("appearance/language", "zh_CN").toString();
    m_langCombo->setCurrentIndex(m_langCombo->findData(lang));

    m_customCssEdit->setPlainText(settings.value("editor/customCss").toString());
}

void SettingsDialog::onAccepted()
{
    QSettings settings("md-editor", "md-editor");
    settings.setValue("session/restore", m_restoreSessionCheck->isChecked());
    settings.setValue("editor/font", m_fontCombo->currentText());
    settings.setValue("editor/fontSize", m_fontSizeSpin->value());
    settings.setValue("editor/lineNumbers", m_lineNumbersCheck->isChecked());
    settings.setValue("editor/wordWrap", m_wordWrapCheck->isChecked());
    settings.setValue("editor/tabSize", m_tabSizeSpin->value());
    settings.setValue("editor/width", m_widthSpin->value());
    settings.setValue("editor/customCss", m_customCssEdit->toPlainText());
    settings.sync();

    // 主题走 MainWindow 统一链路（联动编辑区/菜单勾选/持久化），不再直调 themeManager
    emit themeModeChanged(m_themeCombo->currentData().toString());
    // 导出
    settings.setValue("export/paperSize", m_paperSizeCombo->currentData().toInt());
    settings.setValue("export/orientation", m_selectedOrientation);
    settings.setValue("export/marginTop", m_marginTop->value());
    settings.setValue("export/marginBottom", m_marginBottom->value());
    settings.setValue("export/marginLeft", m_marginLeft->value());
    settings.setValue("export/marginRight", m_marginRight->value());
    settings.setValue("export/pdfHeaderEnabled", m_pdfHeaderCheck->isChecked());
    settings.setValue("export/pdfFooterEnabled", m_pdfFooterCheck->isChecked());

    // 语言走 MainWindow 统一链路（切换 + 持久化），不再直写 QSettings
    emit languageChanged(m_langCombo->currentData().toString());

    accept();
}
