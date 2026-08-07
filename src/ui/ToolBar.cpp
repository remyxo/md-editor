#include "ToolBar.h"
#include <QAction>
#include <QLabel>
#include <QJsonObject>
#include <QIcon>

ToolBar::ToolBar(QWidget *parent)
    : QToolBar(parent)
    , m_headingCombo(nullptr)
    , m_boldAction(nullptr)
    , m_italicAction(nullptr)
    , m_strikeAction(nullptr)
    , m_codeAction(nullptr)
    , m_codeBlockAction(nullptr)
    , m_underlineAction(nullptr)
    , m_highlightAction(nullptr)
    , m_bulletListAction(nullptr)
    , m_orderedListAction(nullptr)
    , m_taskListAction(nullptr)
    , m_blockquoteAction(nullptr)
    , m_readModeAction(nullptr)
    , m_isDark(false)
{
    setObjectName("mainToolBar");
    setMovable(false);
    setIconSize(QSize(20, 20));
    setupUi();
    // 默认编辑模式，图标应为阅读图标（提示可切换）
    setReadMode(false);
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

    addSeparator();

    // === 视图 / 导出 ===
    makeAction(this, &m_iconPaths, "sourceAction",
        "切换源码模式", ":/icons/light/source-code.svg");
    m_readModeAction = makeAction(this, &m_iconPaths, "readModeAction",
        "切换成阅读模式", ":/icons/light/edit-mode.svg");
    makeAction(this, &m_iconPaths, "pdfAction",
        "导出 PDF", ":/icons/light/pdf.svg");

    // === 信号连接 ===
    connect(m_boldAction, &QAction::triggered, this, &ToolBar::boldClicked);
    connect(m_italicAction, &QAction::triggered, this, &ToolBar::italicClicked);
    connect(m_strikeAction, &QAction::triggered, this, &ToolBar::strikethroughClicked);
    connect(m_codeAction, &QAction::triggered, this, &ToolBar::codeClicked);
    connect(m_underlineAction, &QAction::triggered, this, &ToolBar::underlineClicked);
    connect(m_highlightAction, &QAction::triggered, this, &ToolBar::highlightClicked);
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
        else if (a->objectName() == "readModeAction")
            connect(a, &QAction::triggered, this, &ToolBar::toggleReadModeClicked);
    }

    // 收集所有 action，用于阅读模式禁用
    m_allActions = findChildren<QAction *>().toList();
}

void ToolBar::setReadMode(bool readMode)
{
    QString prefix = m_isDark ? ":/icons/dark/" : ":/icons/light/";
    if (readMode) {
        m_readModeAction->setToolTip("切换成编辑模式");
        m_readModeAction->setIcon(QIcon(prefix + "edit-mode.svg"));
    } else {
        m_readModeAction->setToolTip("切换成阅读模式");
        m_readModeAction->setIcon(QIcon(prefix + "read-mode.svg"));
    }
    for (QAction *a : m_allActions) {
        if (a != m_readModeAction)
            a->setVisible(!readMode);
    }
    m_headingCombo->setVisible(!readMode);
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
    TOGGLE_CHECK(m_bulletListAction, "bulletList");
    TOGGLE_CHECK(m_orderedListAction, "orderedList");
    TOGGLE_CHECK(m_taskListAction, "taskList");
    TOGGLE_CHECK(m_blockquoteAction, "blockquote");
    TOGGLE_CHECK(m_codeBlockAction, "codeBlock");
#undef TOGGLE_CHECK
}

void ToolBar::refreshThemeIcons(const QString &theme)
{
    applyIconTheme(theme);
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
