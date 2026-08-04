#include "ToolBar.h"
#include <QAction>
#include <QLabel>

ToolBar::ToolBar(QWidget *parent)
    : QToolBar(parent)
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

    // 文本格式按钮
    QAction *boldAction = addAction("B");
    boldAction->setToolTip("加粗 (Ctrl+B)");
    boldAction->setCheckable(true);
    QFont boldFont = boldAction->font();
    boldFont.setBold(true);
    boldAction->setFont(boldFont);
    connect(boldAction, &QAction::triggered, this, &ToolBar::boldClicked);

    QAction *italicAction = addAction("I");
    italicAction->setToolTip("斜体 (Ctrl+I)");
    QFont italicFont = italicAction->font();
    italicFont.setItalic(true);
    italicAction->setFont(italicFont);
    connect(italicAction, &QAction::triggered, this, &ToolBar::italicClicked);

    QAction *strikeAction = addAction("S");
    strikeAction->setToolTip("删除线");
    QFont strikeFont = strikeAction->font();
    strikeFont.setStrikeOut(true);
    strikeAction->setFont(strikeFont);
    connect(strikeAction, &QAction::triggered, this, &ToolBar::strikethroughClicked);

    QAction *codeAction = addAction("<>");
    codeAction->setToolTip("行内代码");
    connect(codeAction, &QAction::triggered, this, &ToolBar::codeClicked);

    QAction *underlineAction = addAction("U");
    underlineAction->setToolTip("下划线");
    QFont ulFont = underlineAction->font();
    ulFont.setUnderline(true);
    underlineAction->setFont(ulFont);
    connect(underlineAction, &QAction::triggered, this, &ToolBar::underlineClicked);

    QAction *highlightAction = addAction("马克");
    highlightAction->setToolTip("高亮");
    connect(highlightAction, &QAction::triggered, this, &ToolBar::highlightClicked);

    addSeparator();

    // 结构按钮
    QAction *ulAction = addAction("☰");
    ulAction->setToolTip("无序列表");
    connect(ulAction, &QAction::triggered, this, &ToolBar::bulletListClicked);

    QAction *olAction = addAction("1.");
    olAction->setToolTip("有序列表");
    connect(olAction, &QAction::triggered, this, &ToolBar::orderedListClicked);

    QAction *taskAction = addAction("☑");
    taskAction->setToolTip("任务列表");
    connect(taskAction, &QAction::triggered, this, &ToolBar::taskListClicked);

    QAction *quoteAction = addAction("\"");
    quoteAction->setToolTip("引用");
    connect(quoteAction, &QAction::triggered, this, &ToolBar::blockquoteClicked);

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
