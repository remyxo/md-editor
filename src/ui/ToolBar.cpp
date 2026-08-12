#include "ToolBar.h"
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QJsonObject>
#include <QIcon>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>
#include <QGridLayout>
#include <QPushButton>
#include <QColorDialog>
#include <QCursor>
#include <QRegularExpression>

ToolBar::ToolBar(QWidget *parent)
    : QToolBar(parent)
    , m_headingCombo(nullptr)
    , m_undoAction(nullptr)
    , m_redoAction(nullptr)
    , m_boldAction(nullptr)
    , m_italicAction(nullptr)
    , m_strikeAction(nullptr)
    , m_codeAction(nullptr)
    , m_codeBlockAction(nullptr)
    , m_underlineAction(nullptr)
    , m_highlightAction(nullptr)
    , m_subscriptAction(nullptr)
    , m_superscriptAction(nullptr)
    , m_textColorAction(nullptr)
    , m_bgColorAction(nullptr)
    , m_alignLeftAction(nullptr)
    , m_alignCenterAction(nullptr)
    , m_alignRightAction(nullptr)
    , m_alignGroup(nullptr)
    , m_bulletListAction(nullptr)
    , m_orderedListAction(nullptr)
    , m_taskListAction(nullptr)
    , m_blockquoteAction(nullptr)
    , m_isDark(false)
{
    setObjectName("mainToolBar");
    setMovable(false);
    setIconSize(QSize(20, 20));
    setupUi();
}

// 注：m_iconPaths 在 setupUi 后由外部填充
static QAction *makeAction(QToolBar *bar, QMap<QAction*, QString> *reg,
                           const QString &objName, const QString &tip,
                           const QString &iconPath)
{
    QAction *a = bar->addAction(QIcon(iconPath), "");
    a->setObjectName(objName);
    a->setToolTip(tip);
    a->setCheckable(false);
    if (reg) reg->insert(a, iconPath);
    return a;
}

void ToolBar::setupUi()
{
    // === 撤销/重做 ===
    m_undoAction = makeAction(this, &m_iconPaths, "undoAction",
        "撤销 (Ctrl+Z)", ":/icons/light/undo.svg");
    m_undoAction->setEnabled(false);

    m_redoAction = makeAction(this, &m_iconPaths, "redoAction",
        "重做 (Ctrl+Y)", ":/icons/light/redo.svg");
    m_redoAction->setEnabled(false);

    addSeparator();

    // 标题下拉选择器
    QLabel *headingLabel = new QLabel(" 标题 ");
    headingLabel->setObjectName("headingLabel");
    addWidget(headingLabel);
    m_headingCombo = new QComboBox(this);
    m_headingCombo->addItem("正文", 0);
    m_headingCombo->addItem("H1", 1);
    m_headingCombo->addItem("H2", 2);
    m_headingCombo->addItem("H3", 3);
    m_headingCombo->addItem("H4", 4);
    m_headingCombo->addItem("H5", 5);
    m_headingCombo->addItem("H6", 6);
    m_headingCombo->setFixedWidth(72);
    addWidget(m_headingCombo);

    connect(m_headingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                int level = m_headingCombo->itemData(index).toInt();
                emit headingSelected(level);
            });

    addSeparator();

    // === 文本格式按钮 ===
    m_boldAction = makeAction(this, &m_iconPaths, "boldAction",
        "加粗 (Ctrl+B)", ":/icons/light/bold.svg");
    m_boldAction->setCheckable(true);

    m_italicAction = makeAction(this, &m_iconPaths, "italicAction",
        "斜体 (Ctrl+I)", ":/icons/light/italic.svg");
    m_italicAction->setCheckable(true);

    m_strikeAction = makeAction(this, &m_iconPaths, "strikeAction",
        "删除线", ":/icons/light/strikethrough.svg");
    m_strikeAction->setCheckable(true);

    m_underlineAction = makeAction(this, &m_iconPaths, "underlineAction",
        "下划线", ":/icons/light/underline.svg");
    m_underlineAction->setCheckable(true);

    m_highlightAction = makeAction(this, &m_iconPaths, "highlightAction",
        "高亮", ":/icons/light/highlight.svg");
    m_highlightAction->setCheckable(true);

    m_subscriptAction = makeAction(this, &m_iconPaths, "subscriptAction",
        "下标", ":/icons/light/subscript.svg");
    m_subscriptAction->setCheckable(true);

    m_superscriptAction = makeAction(this, &m_iconPaths, "superscriptAction",
        "上标", ":/icons/light/superscript.svg");
    m_superscriptAction->setCheckable(true);

    m_clearFormatAction = makeAction(this, &m_iconPaths, "clearFormatAction",
        "清除样式", ":/icons/light/clear-format.svg");

    addSeparator();

    // 颜色按钮：点击弹出调色板（非 checkable）
    m_textColorAction = makeAction(this, &m_iconPaths, "textColorAction",
        "文字颜色", ":/icons/light/text-color.svg");
    m_bgColorAction = makeAction(this, &m_iconPaths, "bgColorAction",
        "背景颜色", ":/icons/light/bg-color.svg");

    addSeparator();

    // === 文字对齐（互斥按钮组） ===
    m_alignGroup = new QActionGroup(this);
    m_alignGroup->setExclusive(true);
    m_alignLeftAction = makeAction(this, &m_iconPaths, "alignLeftAction",
        "左对齐 (Ctrl+Shift+L)", ":/icons/light/align-left.svg");
    m_alignLeftAction->setCheckable(true);
    m_alignCenterAction = makeAction(this, &m_iconPaths, "alignCenterAction",
        "居中对齐 (Ctrl+Shift+E)", ":/icons/light/align-center.svg");
    m_alignCenterAction->setCheckable(true);
    m_alignRightAction = makeAction(this, &m_iconPaths, "alignRightAction",
        "右对齐 (Ctrl+Shift+R)", ":/icons/light/align-right.svg");
    m_alignRightAction->setCheckable(true);
    m_alignGroup->addAction(m_alignLeftAction);
    m_alignGroup->addAction(m_alignCenterAction);
    m_alignGroup->addAction(m_alignRightAction);
    m_alignLeftAction->setChecked(true);

    addSeparator();

    // === 结构按钮 ===
    m_bulletListAction = makeAction(this, &m_iconPaths, "bulletListAction",
        "无序列表", ":/icons/light/list-bullet.svg");
    m_bulletListAction->setCheckable(true);

    m_orderedListAction = makeAction(this, &m_iconPaths, "orderedListAction",
        "有序列表", ":/icons/light/list-ordered.svg");
    m_orderedListAction->setCheckable(true);

    m_taskListAction = makeAction(this, &m_iconPaths, "taskListAction",
        "任务列表", ":/icons/light/task-list.svg");
    m_taskListAction->setCheckable(true);

    m_blockquoteAction = makeAction(this, &m_iconPaths, "blockquoteAction",
        "引用", ":/icons/light/blockquote.svg");
    m_blockquoteAction->setCheckable(true);

    addSeparator();

    // === 插入按钮 ===
    makeAction(this, &m_iconPaths, "linkAction",
        "插入链接", ":/icons/light/link.svg");
    makeAction(this, &m_iconPaths, "imageAction",
        "插入图片", ":/icons/light/image.svg");
    makeAction(this, &m_iconPaths, "tableAction",
        "插入表格", ":/icons/light/table.svg");
    m_codeAction = makeAction(this, &m_iconPaths, "codeAction",
        "行内代码", ":/icons/light/code.svg");
    m_codeAction->setCheckable(true);
    m_codeBlockAction = makeAction(this, &m_iconPaths, "codeBlockAction",
        "代码块", ":/icons/light/code-block.svg");
    m_codeBlockAction->setCheckable(true);
    makeAction(this, &m_iconPaths, "hrAction",
        "分割线", ":/icons/light/horizontal-rule.svg");
    makeAction(this, &m_iconPaths, "mathAction",
        "插入公式 (Ctrl+M)", ":/icons/light/math.svg");
    makeAction(this, &m_iconPaths, "diagramAction",
        "插入 Mermaid 图表", ":/icons/light/diagram.svg");
    makeAction(this, &m_iconPaths, "emojiAction",
        "插入表情", ":/icons/light/emoji.svg");
    makeAction(this, &m_iconPaths, "footnoteAction",
        "插入脚注", ":/icons/light/footnote.svg");

    addSeparator();

    // === 视图 / 导出 ===
    makeAction(this, &m_iconPaths, "sourceAction",
        "切换源码模式", ":/icons/light/source-code.svg");
    // 导出下拉按钮：用 QToolButton + addWidget，不依赖 QAction setMenu
    {
        QToolButton *exportBtn = new QToolButton(this);
        exportBtn->setIcon(QIcon(":/icons/light/pdf.svg"));
        exportBtn->setToolTip("导出");
        exportBtn->setPopupMode(QToolButton::InstantPopup);
        exportBtn->setObjectName("exportBtn");
        QMenu *m = new QMenu(exportBtn);
        m->addAction("导出 PDF...", this, &ToolBar::pdfExportClicked);
        m->addAction("导出 DOCX...", this, &ToolBar::docxExportClicked);
        exportBtn->setMenu(m);
        addWidget(exportBtn);
    }

    // === 信号连接 ===
    connect(m_boldAction, &QAction::triggered, this, &ToolBar::boldClicked);
    connect(m_italicAction, &QAction::triggered, this, &ToolBar::italicClicked);
    connect(m_strikeAction, &QAction::triggered, this, &ToolBar::strikethroughClicked);
    connect(m_codeAction, &QAction::triggered, this, &ToolBar::codeClicked);
    connect(m_underlineAction, &QAction::triggered, this, &ToolBar::underlineClicked);
    connect(m_highlightAction, &QAction::triggered, this, &ToolBar::highlightClicked);
    connect(m_subscriptAction, &QAction::triggered, this, &ToolBar::subscriptClicked);
    connect(m_superscriptAction, &QAction::triggered, this, &ToolBar::superscriptClicked);
    connect(m_clearFormatAction, &QAction::triggered, this, &ToolBar::clearFormatClicked);
    connect(m_alignLeftAction, &QAction::triggered, this, [this]{ emit alignSelected("left"); });
    connect(m_alignCenterAction, &QAction::triggered, this, [this]{ emit alignSelected("center"); });
    connect(m_alignRightAction, &QAction::triggered, this, [this]{ emit alignSelected("right"); });
    connect(m_textColorAction, &QAction::triggered, this, [this]{
        buildColorMenu(true)->exec(QCursor::pos());
    });
    connect(m_bgColorAction, &QAction::triggered, this, [this]{
        buildColorMenu(false)->exec(QCursor::pos());
    });
    connect(m_bulletListAction, &QAction::triggered, this, &ToolBar::bulletListClicked);
    connect(m_orderedListAction, &QAction::triggered, this, &ToolBar::orderedListClicked);
    connect(m_taskListAction, &QAction::triggered, this, &ToolBar::taskListClicked);
    connect(m_blockquoteAction, &QAction::triggered, this, &ToolBar::blockquoteClicked);

    for (QAction *a : findChildren<QAction *>()) {
        if (a->objectName() == "linkAction")
            connect(a, &QAction::triggered, this, &ToolBar::linkClicked);
        else if (a->objectName() == "imageAction")
            connect(a, &QAction::triggered, this, &ToolBar::imageClicked);
        else if (a->objectName() == "tableAction")
            connect(a, &QAction::triggered, this, &ToolBar::tableClicked);
        else if (a->objectName() == "hrAction")
            connect(a, &QAction::triggered, this, &ToolBar::horizontalRuleClicked);
        else if (a->objectName() == "codeBlockAction")
            connect(a, &QAction::triggered, this, &ToolBar::codeBlockClicked);
        else if (a->objectName() == "sourceAction")
            connect(a, &QAction::triggered, this, &ToolBar::sourceToggleClicked);
        else if (a->objectName() == "pdfAction")
            connect(a, &QAction::triggered, this, &ToolBar::pdfExportClicked);
        else if (a->objectName() == "mathAction")
            connect(a, &QAction::triggered, this, &ToolBar::mathClicked);
        else if (a->objectName() == "diagramAction")
            connect(a, &QAction::triggered, this, &ToolBar::mermaidClicked);
        else if (a->objectName() == "emojiAction")
            connect(a, &QAction::triggered, this, &ToolBar::emojiClicked);
        else if (a->objectName() == "footnoteAction")
            connect(a, &QAction::triggered, this, &ToolBar::footnoteClicked);
    }

    // 收集所有 action，用于源码模式禁用
    m_allActions = findChildren<QAction *>().toList();
}

// 构建颜色选择弹出菜单：无颜色 + 12 预设色网格 + 更多颜色（QColorDialog）。
// forText=true 为文字色按钮，false 为背景色按钮。
QMenu *ToolBar::buildColorMenu(bool forText)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // 无颜色
    QAction *noneAct = menu->addAction(forText ? QStringLiteral("无文字颜色") : QStringLiteral("无背景颜色"));
    connect(noneAct, &QAction::triggered, this, [this, forText]() {
        if (forText) emit textColorCleared();
        else emit bgColorCleared();
    });

    menu->addSeparator();

    // 12 预设色网格（6 列 × 2 行）
    static const QStringList COLORS = {
        "#000000", "#7f7f7f", "#e23b3b", "#e8833a", "#d4a017", "#2e8b57",
        "#17a2b8", "#2563eb", "#7c3aed", "#db2777", "#8b4513", "#ffffff"
    };
    const QString cur = (forText ? m_currentTextColor : m_currentBgColor).toLower();

    QWidget *grid = new QWidget;
    QGridLayout *layout = new QGridLayout(grid);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 6, 6, 6);
    int idx = 0;
    for (const QString &c : COLORS) {
        QPushButton *btn = new QPushButton;
        btn->setFixedSize(24, 24);
        btn->setToolTip(c);
        QString border = (!cur.isEmpty() && cur == c.toLower())
            ? QStringLiteral("2px solid #2196f3")     // 当前色高亮
            : QStringLiteral("1px solid #999");
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;border:%2;border-radius:3px;}"
            "QPushButton:hover{border:2px solid #2196f3;}").arg(c, border));
        connect(btn, &QPushButton::clicked, this, [this, forText, c, menu]() {
            menu->close();
            if (forText) emit textColorSelected(c);
            else emit bgColorSelected(c);
        });
        layout->addWidget(btn, idx / 6, idx % 6);
        ++idx;
    }
    QWidgetAction *gridAct = new QWidgetAction(menu);
    gridAct->setDefaultWidget(grid);
    menu->addAction(gridAct);

    menu->addSeparator();

    // 更多颜色（QColorDialog）
    QAction *moreAct = menu->addAction(QStringLiteral("更多颜色…"));
    connect(moreAct, &QAction::triggered, this, [this, forText]() {
        QColor chosen = QColorDialog::getColor(Qt::white, this,
            forText ? QStringLiteral("选择文字颜色") : QStringLiteral("选择背景颜色"));
        if (!chosen.isValid()) return;   // 用户取消
        if (forText) emit textColorSelected(chosen.name());
        else emit bgColorSelected(chosen.name());
    });
    return menu;
}

void ToolBar::setSourceMode(bool on)
{
    // 源码模式下灰化所有编辑/格式化按钮，保留源码切换和 PDF 导出可用
    for (QAction *a : m_allActions) {
        if (a->objectName() != QStringLiteral("sourceAction")
            && a->objectName() != QStringLiteral("pdfAction")) {
            a->setEnabled(!on);
        }
    }
    // 恢复时根据格式状态重新设置按钮可用性（disabled 可能残留）
    if (!on) {
        for (QAction *a : m_allActions) {
            a->setEnabled(true);
        }
    }
    m_headingCombo->setEnabled(!on);
}

void ToolBar::updateFormatState(const QJsonObject &f)
{
    if (f.contains("heading")) {
        int level = f["heading"].toInt();
        int idx = m_headingCombo->findData(level);
        if (idx >= 0) {
            m_headingCombo->blockSignals(true);
            m_headingCombo->setCurrentIndex(idx);
            m_headingCombo->blockSignals(false);
        }
    }
#define TOGGLE_CHECK(act, key) if (f.contains(key)) { act->blockSignals(true); act->setChecked(f[key].toBool()); act->blockSignals(false); }
    TOGGLE_CHECK(m_boldAction, "bold");
    TOGGLE_CHECK(m_italicAction, "italic");
    TOGGLE_CHECK(m_strikeAction, "strike");
    TOGGLE_CHECK(m_codeAction, "code");
    TOGGLE_CHECK(m_underlineAction, "underline");
    TOGGLE_CHECK(m_highlightAction, "highlight");
    TOGGLE_CHECK(m_subscriptAction, "subscript");
    TOGGLE_CHECK(m_superscriptAction, "superscript");
    TOGGLE_CHECK(m_bulletListAction, "bulletList");
    TOGGLE_CHECK(m_orderedListAction, "orderedList");
    TOGGLE_CHECK(m_taskListAction, "taskList");
    TOGGLE_CHECK(m_blockquoteAction, "blockquote");
    TOGGLE_CHECK(m_codeBlockAction, "codeBlock");
#undef TOGGLE_CHECK
    // 文字对齐互斥同步（无对应属性时默认左对齐）
    if (f.contains("textAlign")) {
        QString a = f["textAlign"].toString();
        m_alignLeftAction->blockSignals(true);
        m_alignCenterAction->blockSignals(true);
        m_alignRightAction->blockSignals(true);
        m_alignLeftAction->setChecked(a == "left");
        m_alignCenterAction->setChecked(a == "center");
        m_alignRightAction->setChecked(a == "right");
        m_alignLeftAction->blockSignals(false);
        m_alignCenterAction->blockSignals(false);
        m_alignRightAction->blockSignals(false);
    }
    // 颜色 span 回显：解析当前 colorSpanStyle 缓存 color / background-color
    if (f.contains("colorSpanStyle")) {
        QString style = f["colorSpanStyle"].toString();
        QRegularExpression colorRe("(?:^|;)\\s*color\\s*:\\s*([^;]+)",
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpression bgRe("background-color\\s*:\\s*([^;]+)",
            QRegularExpression::CaseInsensitiveOption);
        auto cm = colorRe.match(style);
        auto bm = bgRe.match(style);
        m_currentTextColor = cm.hasMatch() ? cm.captured(1).trimmed() : QString();
        m_currentBgColor = bm.hasMatch() ? bm.captured(1).trimmed() : QString();
    }
}

void ToolBar::updateHistoryState(bool canUndo, bool canRedo)
{
    if (m_undoAction) m_undoAction->setEnabled(canUndo);
    if (m_redoAction) m_redoAction->setEnabled(canRedo);
}

void ToolBar::refreshThemeIcons(const QString &theme)
{
    applyIconTheme(theme);
    // 导出下拉按钮（QToolButton，不走 QAction 图标注册机制）
    QToolButton *exportBtn = findChild<QToolButton*>("exportBtn");
    if (exportBtn) {
        QString iconPath = (theme == "dark") ? ":/icons/dark/pdf.svg"
                                             : ":/icons/light/pdf.svg";
        exportBtn->setIcon(QIcon(iconPath));
    }
}

void ToolBar::applyIconTheme(const QString &theme)
{
    m_isDark = (theme == "dark");
    for (auto it = m_iconPaths.begin(); it != m_iconPaths.end(); ++it) {
        QAction *action = it.key();
        QString path = it.value();
        if (m_isDark)
            path.replace("/light/", "/dark/");
        action->setIcon(QIcon(path));
    }
}
