#include "ToolBar.h"
#include <QAction>
#include <QLabel>
#include <QJsonObject>

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

void ToolBar::setupUi()
{
    // 标题下拉选择器
    addWidget(new QLabel(" 标题 "));
    m_headingCombo = new QComboBox(this);
    m_headingCombo->addItem("正文", 0);
    m_headingCombo->addItem("标题 1", 1);
    m_headingCombo->addItem("标题 2", 2);
    m_headingCombo->addItem("标题 3", 3);
    m_headingCombo->addItem("标题 4", 4);
    m_headingCombo->addItem("标题 5", 5);
    m_headingCombo->addItem("标题 6", 6);
    m_headingCombo->setFixedWidth(100);
    addWidget(m_headingCombo);

    connect(m_headingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                int level = m_headingCombo->itemData(index).toInt();
                emit headingSelected(level);
            });

    addSeparator();

    // 文本格式按钮 — 存储为成员，设为 checkable
    m_boldAction = addAction("B");
    m_boldAction->setToolTip("加粗 (Ctrl+B)");
    m_boldAction->setCheckable(true);
    QFont boldFont = m_boldAction->font();
    boldFont.setBold(true);
    m_boldAction->setFont(boldFont);
    connect(m_boldAction, &QAction::triggered, this, &ToolBar::boldClicked);

    m_italicAction = addAction("I");
    m_italicAction->setToolTip("斜体 (Ctrl+I)");
    m_italicAction->setCheckable(true);
    QFont italicFont = m_italicAction->font();
    italicFont.setItalic(true);
    m_italicAction->setFont(italicFont);
    connect(m_italicAction, &QAction::triggered, this, &ToolBar::italicClicked);

    m_strikeAction = addAction("S");
    m_strikeAction->setToolTip("删除线");
    m_strikeAction->setCheckable(true);
    QFont strikeFont = m_strikeAction->font();
    strikeFont.setStrikeOut(true);
    m_strikeAction->setFont(strikeFont);
    connect(m_strikeAction, &QAction::triggered, this, &ToolBar::strikethroughClicked);

    m_codeAction = addAction("<>");
    m_codeAction->setToolTip("行内代码");
    m_codeAction->setCheckable(true);
    connect(m_codeAction, &QAction::triggered, this, &ToolBar::codeClicked);

    m_underlineAction = addAction("U");
    m_underlineAction->setToolTip("下划线");
    m_underlineAction->setCheckable(true);
    QFont ulFont = m_underlineAction->font();
    ulFont.setUnderline(true);
    m_underlineAction->setFont(ulFont);
    connect(m_underlineAction, &QAction::triggered, this, &ToolBar::underlineClicked);

    m_highlightAction = addAction("马克");
    m_highlightAction->setToolTip("高亮");
    m_highlightAction->setCheckable(true);
    connect(m_highlightAction, &QAction::triggered, this, &ToolBar::highlightClicked);

    addSeparator();

    // 结构按钮 — 存储为成员，设为 checkable
    m_bulletListAction = addAction("☰");
    m_bulletListAction->setToolTip("无序列表");
    m_bulletListAction->setCheckable(true);
    connect(m_bulletListAction, &QAction::triggered, this, &ToolBar::bulletListClicked);

    m_orderedListAction = addAction("1.");
    m_orderedListAction->setToolTip("有序列表");
    m_orderedListAction->setCheckable(true);
    connect(m_orderedListAction, &QAction::triggered, this, &ToolBar::orderedListClicked);

    m_taskListAction = addAction("☑");
    m_taskListAction->setToolTip("任务列表");
    m_taskListAction->setCheckable(true);
    connect(m_taskListAction, &QAction::triggered, this, &ToolBar::taskListClicked);

    m_blockquoteAction = addAction("\"");
    m_blockquoteAction->setToolTip("引用");
    m_blockquoteAction->setCheckable(true);
    connect(m_blockquoteAction, &QAction::triggered, this, &ToolBar::blockquoteClicked);

    addSeparator();

    // 插入按钮
    QAction *linkAction = addAction("🔗");
    linkAction->setToolTip("插入链接");
    connect(linkAction, &QAction::triggered, this, &ToolBar::linkClicked);

    QAction *imageAction = addAction("🖼");
    imageAction->setToolTip("插入图片");
    connect(imageAction, &QAction::triggered, this, &ToolBar::imageClicked);

    QAction *tableAction = addAction("⊞");
    tableAction->setToolTip("插入表格");
    connect(tableAction, &QAction::triggered, this, &ToolBar::tableClicked);

    QAction *hrAction = addAction("—");
    hrAction->setToolTip("分割线");
    connect(hrAction, &QAction::triggered, this, &ToolBar::horizontalRuleClicked);

    addSeparator();

    // 视图按钮
    QAction *sourceAction = addAction("<>");
    sourceAction->setToolTip("切换源码模式");
    connect(sourceAction, &QAction::triggered, this, &ToolBar::sourceToggleClicked);

    addSeparator();

    // 导出按钮
    QAction *pdfAction = addAction("📄");
    pdfAction->setToolTip("导出 PDF");
    connect(pdfAction, &QAction::triggered, this, &ToolBar::pdfExportClicked);
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
