#include "ToolBar.h"
#include <QAction>
#include <QLabel>
#include <QJsonObject>
#include <QWidget>

ToolBar::ToolBar(QWidget *parent)
    : QToolBar(parent)
    , m_headingCombo(nullptr)
    , m_boldAction(nullptr)
    , m_italicAction(nullptr)
    , m_strikeAction(nullptr)
    , m_codeAction(nullptr)
    , m_underlineAction(nullptr)
    , m_highlightAction(nullptr)
    , m_bulletListAction(nullptr)
    , m_orderedListAction(nullptr)
    , m_taskListAction(nullptr)
    , m_blockquoteAction(nullptr)
{
    setObjectName("mainToolBar");
    setMovable(false);
    setupUi();
}

// 辅助：创建带 objectName 的 action
static QAction *makeAction(QToolBar *bar, const QString &text,
                           const QString &objName, const QString &tip)
{
    QAction *a = bar->addAction(text);
    a->setObjectName(objName);
    a->setToolTip(tip);
    return a;
}

void ToolBar::setupUi()
{
    // 标题下拉选择器
    QLabel *headingLabel = new QLabel(" 标题 ");
    headingLabel->setStyleSheet("color: #555; font-size: 15px;");
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

    // === 文本格式按钮 — 彩色 ===
    m_boldAction = makeAction(this, "𝐁", "boldAction", "加粗 (Ctrl+B)");
    m_boldAction->setCheckable(true);

    m_italicAction = makeAction(this, "𝑰", "italicAction", "斜体 (Ctrl+I)");
    m_italicAction->setCheckable(true);

    m_strikeAction = makeAction(this, "S̶", "strikeAction", "删除线");
    m_strikeAction->setCheckable(true);

    m_codeAction = makeAction(this, "<>", "codeAction", "行内代码");
    m_codeAction->setCheckable(true);

    m_underlineAction = makeAction(this, "U̲", "underlineAction", "下划线");
    m_underlineAction->setCheckable(true);

    m_highlightAction = makeAction(this, "🖍", "highlightAction", "高亮");
    m_highlightAction->setCheckable(true);

    addSeparator();

    // === 结构按钮 ===
    m_bulletListAction = makeAction(this, "• ≡", "bulletListAction", "无序列表");
    m_bulletListAction->setCheckable(true);

    m_orderedListAction = makeAction(this, "1.", "orderedListAction", "有序列表");
    m_orderedListAction->setCheckable(true);

    m_taskListAction = makeAction(this, "☑", "taskListAction", "任务列表");
    m_taskListAction->setCheckable(true);

    m_blockquoteAction = makeAction(this, "❝", "blockquoteAction", "引用");
    m_blockquoteAction->setCheckable(true);

    addSeparator();

    // === 插入按钮 ===
    makeAction(this, "🔗", "linkAction", "插入链接");
    makeAction(this, "🖼", "imageAction", "插入图片");
    makeAction(this, "▦", "tableAction", "插入表格");
    makeAction(this, "—", "hrAction", "分割线");

    addSeparator();

    // === 视图按钮 ===
    makeAction(this, "<>", "sourceAction", "切换源码模式");

    addSeparator();

    // === 导出按钮 ===
    makeAction(this, "📄", "pdfAction", "导出 PDF");

    // 连接所有 action 信号
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

    // 插入/视图/导出按钮通过 findChild 连接（或直接在 makeAction 后连接）
    for (QAction *a : findChildren<QAction *>()) {
        if (a->objectName() == "linkAction")
            connect(a, &QAction::triggered, this, &ToolBar::linkClicked);
        else if (a->objectName() == "imageAction")
            connect(a, &QAction::triggered, this, &ToolBar::imageClicked);
        else if (a->objectName() == "tableAction")
            connect(a, &QAction::triggered, this, &ToolBar::tableClicked);
        else if (a->objectName() == "hrAction")
            connect(a, &QAction::triggered, this, &ToolBar::horizontalRuleClicked);
        else if (a->objectName() == "sourceAction")
            connect(a, &QAction::triggered, this, &ToolBar::sourceToggleClicked);
        else if (a->objectName() == "pdfAction")
            connect(a, &QAction::triggered, this, &ToolBar::pdfExportClicked);
    }
}

void ToolBar::updateFormatState(const QJsonObject &f)
{
    // 标题下拉
    if (f.contains("heading")) {
        int level = f["heading"].toInt();
        int idx = m_headingCombo->findData(level);
        if (idx >= 0) {
            m_headingCombo->blockSignals(true);
            m_headingCombo->setCurrentIndex(idx);
            m_headingCombo->blockSignals(false);
        }
    }
    // 文本格式 — 切换 checked 状态（不触发信号）
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
#undef TOGGLE_CHECK
}
