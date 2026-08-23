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
#include <QEvent>
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
    , m_textColorAction(nullptr)
    , m_bgColorAction(nullptr)
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
        tr("撤销 (Ctrl+Z)"), ":/icons/light/undo.svg");
    m_undoAction->setEnabled(false);

    m_redoAction = makeAction(this, &m_iconPaths, "redoAction",
        tr("重做 (Ctrl+Y)"), ":/icons/light/redo.svg");
    m_redoAction->setEnabled(false);

    addSeparator();

    // 标题下拉选择器
    m_headingLabel = new QLabel(tr(" 标题 "));
    m_headingLabel->setObjectName("headingLabel");
    addWidget(m_headingLabel);
    m_headingCombo = new QComboBox(this);
    m_headingCombo->addItem(tr("正文"), 0);
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
        tr("加粗 (Ctrl+B)"), ":/icons/light/bold.svg");
    m_boldAction->setCheckable(true);

    m_italicAction = makeAction(this, &m_iconPaths, "italicAction",
        tr("斜体 (Ctrl+I)"), ":/icons/light/italic.svg");
    m_italicAction->setCheckable(true);

    m_strikeAction = makeAction(this, &m_iconPaths, "strikeAction",
        tr("删除线"), ":/icons/light/strikethrough.svg");
    m_strikeAction->setCheckable(true);

    m_underlineAction = makeAction(this, &m_iconPaths, "underlineAction",
        tr("下划线"), ":/icons/light/underline.svg");
    m_underlineAction->setCheckable(true);

    m_highlightAction = makeAction(this, &m_iconPaths, "highlightAction",
        tr("高亮"), ":/icons/light/highlight.svg");
    m_highlightAction->setCheckable(true);

    m_clearFormatAction = makeAction(this, &m_iconPaths, "clearFormatAction",
        tr("清除样式"), ":/icons/light/clear-format.svg");

    addSeparator();

    // 颜色按钮：点击弹出调色板（非 checkable）
    m_textColorAction = makeAction(this, &m_iconPaths, "textColorAction",
        tr("文字颜色"), ":/icons/light/text-color.svg");
    m_bgColorAction = makeAction(this, &m_iconPaths, "bgColorAction",
        tr("背景颜色"), ":/icons/light/bg-color.svg");

    addSeparator();

    // === 文字对齐（下拉折叠：左/中/右） ===
    {
        m_alignButton = new QToolButton(this);
        m_alignButton->setObjectName("alignButton");
        m_alignButton->setToolTip(tr("段落对齐"));
        m_alignButton->setCheckable(true);  // 居中/右对齐时高亮（左对齐为默认态不高亮）
        m_alignButton->setArrowType(Qt::NoArrow); // chevron 由 QSS menu-indicator 提供（同导出按钮）
        m_alignMenu = new QMenu(m_alignButton);
        auto addAlignItem = [this](const QString &objName, const QString &text,
                                   const QString &shortcut, const QString &icon) {
            QAction *a = new QAction(QIcon(icon), text, m_alignMenu);
            a->setObjectName(objName);
            a->setShortcut(QKeySequence(shortcut));  // 仅显示文字，菜单不抢键（见下）
            a->setCheckable(true);
            m_alignMenu->addAction(a);
            // 注册进主题注册表：applyIconTheme 切换 /light/→/dark/
            m_iconPaths.insert(a, icon);
            return a;
        };
        m_alignLeftAct = addAlignItem("alignLeftItem", tr("左对齐(&L)"), "Ctrl+Shift+L",
                     ":/icons/light/align-left.svg");
        m_alignCenterAct = addAlignItem("alignCenterItem", tr("居中对齐(&E)"), "Ctrl+Shift+E",
                     ":/icons/light/align-center.svg");
        m_alignRightAct = addAlignItem("alignRightItem", tr("右对齐(&R)"), "Ctrl+Shift+R",
                     ":/icons/light/align-right.svg");
        m_alignButton->setMenu(m_alignMenu);
        // 快捷键仅作显示：绑定到菜单自身上下文，避免抢 WebEngine 按键
        for (QAction *a : m_alignMenu->actions())
            a->setShortcutContext(Qt::WidgetShortcut);
        // 三项互斥勾选：消除点击瞬时的双勾闪烁（rebuildAlignMenuChecks 仍可显式重设）
        QActionGroup *alignGroup = new QActionGroup(this);
        for (QAction *a : m_alignMenu->actions())
            alignGroup->addAction(a);
        m_alignButton->installEventFilter(this);
        addWidget(m_alignButton);
        setAlignIcon("left");
        rebuildAlignMenuChecks("left");
    }

    // === 上下标（下拉折叠：上标/下标） ===
    {
        m_subsupButton = new QToolButton(this);
        m_subsupButton->setObjectName("subsupButton");
        m_subsupButton->setToolTip(tr("上标/下标"));
        m_subsupButton->setCheckable(true);  // 光标处于上下标内容时高亮
        m_subsupButton->setArrowType(Qt::NoArrow);
        m_subsupMenu = new QMenu(m_subsupButton);
        auto addSubsupItem = [this](const QString &objName, const QString &text,
                                    const QString &shortcut, const QString &icon) {
            QAction *a = new QAction(QIcon(icon), text, m_subsupMenu);
            a->setObjectName(objName);
            a->setShortcut(QKeySequence(shortcut));
            a->setCheckable(true);
            m_subsupMenu->addAction(a);
            // 注册进主题注册表：applyIconTheme 切换 /light/→/dark/
            m_iconPaths.insert(a, icon);
            return a;
        };
        m_superscriptAct = addSubsupItem("superscriptItem", tr("上标(&P)"), "Ctrl+.",
                      ":/icons/light/superscript.svg");
        m_subscriptAct = addSubsupItem("subscriptItem", tr("下标(&B)"), "Ctrl+,",
                      ":/icons/light/subscript.svg");
        m_subsupButton->setMenu(m_subsupMenu);
        for (QAction *a : m_subsupMenu->actions())
            a->setShortcutContext(Qt::WidgetShortcut);
        m_subsupButton->installEventFilter(this);
        addWidget(m_subsupButton);
        setSubsupIcon(false, false);
        rebuildSubsupMenuChecks(false, false);
    }

    addSeparator();

    // === 结构按钮 ===
    m_bulletListAction = makeAction(this, &m_iconPaths, "bulletListAction",
        tr("无序列表"), ":/icons/light/list-bullet.svg");
    m_bulletListAction->setCheckable(true);

    m_orderedListAction = makeAction(this, &m_iconPaths, "orderedListAction",
        tr("有序列表"), ":/icons/light/list-ordered.svg");
    m_orderedListAction->setCheckable(true);

    m_taskListAction = makeAction(this, &m_iconPaths, "taskListAction",
        tr("任务列表"), ":/icons/light/task-list.svg");
    m_taskListAction->setCheckable(true);

    m_blockquoteAction = makeAction(this, &m_iconPaths, "blockquoteAction",
        tr("引用"), ":/icons/light/blockquote.svg");
    m_blockquoteAction->setCheckable(true);

    addSeparator();

    // === 插入按钮 ===
    makeAction(this, &m_iconPaths, "linkAction",
        tr("插入链接"), ":/icons/light/link.svg");
    makeAction(this, &m_iconPaths, "imageAction",
        tr("插入图片"), ":/icons/light/image.svg");
    makeAction(this, &m_iconPaths, "tableAction",
        tr("插入表格"), ":/icons/light/table.svg");
    m_codeAction = makeAction(this, &m_iconPaths, "codeAction",
        tr("行内代码"), ":/icons/light/code.svg");
    m_codeAction->setCheckable(true);
    m_codeBlockAction = makeAction(this, &m_iconPaths, "codeBlockAction",
        tr("代码块"), ":/icons/light/code-block.svg");
    m_codeBlockAction->setCheckable(true);
    makeAction(this, &m_iconPaths, "hrAction",
        tr("分割线"), ":/icons/light/horizontal-rule.svg");
    makeAction(this, &m_iconPaths, "mathAction",
        tr("插入公式 (Ctrl+M)"), ":/icons/light/math.svg");
    makeAction(this, &m_iconPaths, "diagramAction",
        tr("插入 Mermaid 图表"), ":/icons/light/diagram.svg");
    makeAction(this, &m_iconPaths, "emojiAction",
        tr("插入表情"), ":/icons/light/emoji.svg");
    makeAction(this, &m_iconPaths, "footnoteAction",
        tr("插入脚注"), ":/icons/light/footnote.svg");

    addSeparator();

    // === 视图 / 导出 ===
    m_sourceAction = makeAction(this, &m_iconPaths, "sourceAction",
        tr("切换源码模式"), ":/icons/light/source-code.svg");
    // 导出下拉按钮：直接用 QToolButton + InstantPopup
    {
        m_exportButton = new QToolButton(this);
        m_exportButton->setObjectName("pdfAction");
        m_exportButton->setIcon(QIcon(":/icons/light/pdf.svg"));
        m_exportButton->setToolTip(tr("导出"));
        m_exportButton->setCheckable(false);
        m_exportButton->setArrowType(Qt::NoArrow); // chevron 由 QSS menu-indicator 提供（同对齐/上下标按钮）
        m_exportMenu = new QMenu(m_exportButton);
        m_exportMenu->addAction(tr("导出 PDF..."), this, &ToolBar::pdfExportClicked);
        m_exportMenu->addAction(tr("导出 DOCX..."), this, &ToolBar::docxExportClicked);
        m_exportMenu->addAction(tr("导出 HTML..."), this, &ToolBar::htmlExportClicked);
        m_exportButton->setMenu(m_exportMenu);
        m_exportButton->installEventFilter(this);
        addWidget(m_exportButton);
    }

    // === 信号连接 ===
    connect(m_boldAction, &QAction::triggered, this, &ToolBar::boldClicked);
    connect(m_italicAction, &QAction::triggered, this, &ToolBar::italicClicked);
    connect(m_strikeAction, &QAction::triggered, this, &ToolBar::strikethroughClicked);
    connect(m_codeAction, &QAction::triggered, this, &ToolBar::codeClicked);
    connect(m_underlineAction, &QAction::triggered, this, &ToolBar::underlineClicked);
    connect(m_highlightAction, &QAction::triggered, this, &ToolBar::highlightClicked);
    connect(m_clearFormatAction, &QAction::triggered, this, &ToolBar::clearFormatClicked);
    // 对齐/上下标下拉菜单项 → 复用原信号，MainWindow 零改动
    for (QAction *a : m_alignMenu->actions()) {
        QString align = (a->objectName() == "alignLeftItem") ? "left"
                      : (a->objectName() == "alignCenterItem") ? "center" : "right";
        connect(a, &QAction::triggered, this, [this, align]{ emit alignSelected(align); });
    }
    connect(m_subsupMenu->findChild<QAction*>("superscriptItem"),
            &QAction::triggered, this, &ToolBar::superscriptClicked);
    connect(m_subsupMenu->findChild<QAction*>("subscriptItem"),
            &QAction::triggered, this, &ToolBar::subscriptClicked);
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
            ; // 下拉菜单：不连接 triggered，由子菜单项触发
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

    retranslateUi();  // 统一重设文案（首次设置，语言切换后 MainWindow 再次调用）
}

// 语言切换后重设全部 tooltip/菜单项文案
void ToolBar::retranslateUi()
{
    m_undoAction->setToolTip(tr("撤销 (Ctrl+Z)"));
    m_redoAction->setToolTip(tr("重做 (Ctrl+Y)"));

    m_headingLabel->setText(tr(" 标题 "));
    m_headingCombo->setItemText(0, tr("正文"));  // H1~H6 保持不变

    m_boldAction->setToolTip(tr("加粗 (Ctrl+B)"));
    m_italicAction->setToolTip(tr("斜体 (Ctrl+I)"));
    m_strikeAction->setToolTip(tr("删除线"));
    m_underlineAction->setToolTip(tr("下划线"));
    m_highlightAction->setToolTip(tr("高亮"));
    m_clearFormatAction->setToolTip(tr("清除样式"));
    m_textColorAction->setToolTip(tr("文字颜色"));
    m_bgColorAction->setToolTip(tr("背景颜色"));

    m_alignButton->setToolTip(tr("段落对齐"));
    m_alignLeftAct->setText(tr("左对齐(&L)"));
    m_alignCenterAct->setText(tr("居中对齐(&E)"));
    m_alignRightAct->setText(tr("右对齐(&R)"));

    m_subsupButton->setToolTip(tr("上标/下标"));
    m_superscriptAct->setText(tr("上标(&P)"));
    m_subscriptAct->setText(tr("下标(&B)"));

    m_bulletListAction->setToolTip(tr("无序列表"));
    m_orderedListAction->setToolTip(tr("有序列表"));
    m_taskListAction->setToolTip(tr("任务列表"));
    m_blockquoteAction->setToolTip(tr("引用"));

    findChild<QAction*>("linkAction")->setToolTip(tr("插入链接"));
    findChild<QAction*>("imageAction")->setToolTip(tr("插入图片"));
    findChild<QAction*>("tableAction")->setToolTip(tr("插入表格"));
    m_codeAction->setToolTip(tr("行内代码"));
    m_codeBlockAction->setToolTip(tr("代码块"));
    findChild<QAction*>("hrAction")->setToolTip(tr("分割线"));
    findChild<QAction*>("mathAction")->setToolTip(tr("插入公式 (Ctrl+M)"));
    findChild<QAction*>("diagramAction")->setToolTip(tr("插入 Mermaid 图表"));
    findChild<QAction*>("emojiAction")->setToolTip(tr("插入表情"));
    findChild<QAction*>("footnoteAction")->setToolTip(tr("插入脚注"));

    if (m_sourceAction)
        m_sourceAction->setToolTip(m_sourceMode ? tr("切换为所见即所得模式")
                                                : tr("切换源码模式"));

    m_exportButton->setToolTip(tr("导出"));
    m_exportMenu->actions().at(0)->setText(tr("导出 PDF..."));
    m_exportMenu->actions().at(1)->setText(tr("导出 DOCX..."));
    m_exportMenu->actions().at(2)->setText(tr("导出 HTML..."));
}

// 按对齐状态切换按钮图标与高亮（跟随光标回显）。
// 左对齐是文档默认态，不高亮；居中/右对齐为显式设置，高亮提示。
void ToolBar::setAlignIcon(const QString &alignment)
{
    m_currentAlign = alignment;
    QString theme = m_isDark ? "dark" : "light";
    QString icon = (alignment == "center") ? "align-center"
                 : (alignment == "right") ? "align-right" : "align-left";
    m_alignButton->setIcon(QIcon(QString(":/icons/%1/%2.svg").arg(theme, icon)));
    m_alignButton->setChecked(alignment == "center" || alignment == "right");
}

// 按上下标激活状态切换按钮图标与 checked 高亮（默认上标图标）
void ToolBar::setSubsupIcon(bool sub, bool sup)
{
    QString theme = m_isDark ? "dark" : "light";
    QString icon = sub ? "subscript" : "superscript";
    m_subsupButton->setIcon(QIcon(QString(":/icons/%1/%2.svg").arg(theme, icon)));
    m_subsupButton->setChecked(sub || sup);
}

void ToolBar::rebuildAlignMenuChecks(const QString &alignment)
{
    for (QAction *a : m_alignMenu->actions()) {
        QString align = (a->objectName() == "alignLeftItem") ? "left"
                      : (a->objectName() == "alignCenterItem") ? "center" : "right";
        a->setChecked(align == alignment);
    }
}

void ToolBar::rebuildSubsupMenuChecks(bool sub, bool sup)
{
    if (QAction *a = m_subsupMenu->findChild<QAction*>("superscriptItem"))
        a->setChecked(sup);
    if (QAction *a = m_subsupMenu->findChild<QAction*>("subscriptItem"))
        a->setChecked(sub);
}

// 构建颜色选择弹出菜单：无颜色 + 12 预设色网格 + 更多颜色（QColorDialog）。
// forText=true 为文字色按钮，false 为背景色按钮。
QMenu *ToolBar::buildColorMenu(bool forText)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // 无颜色
    QAction *noneAct = menu->addAction(forText ? tr("无文字颜色") : tr("无背景颜色"));
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
    QAction *moreAct = menu->addAction(tr("更多颜色…"));
    connect(moreAct, &QAction::triggered, this, [this, forText]() {
        QColor chosen = QColorDialog::getColor(Qt::white, this,
            forText ? tr("选择文字颜色") : tr("选择背景颜色"));
        if (!chosen.isValid()) return;   // 用户取消
        if (forText) emit textColorSelected(chosen.name());
        else emit bgColorSelected(chosen.name());
    });
    return menu;
}

void ToolBar::setSourceMode(bool on)
{
    m_sourceMode = on;
    // 源码模式下灰化所有编辑/格式化按钮，保留源码切换和导出按钮可用
    for (QAction *a : m_allActions) {
        if (a->objectName() != QStringLiteral("sourceAction")) {
            a->setEnabled(!on);
        }
    }
    if (!on) {
        for (QAction *a : m_allActions) {
            a->setEnabled(true);
        }
    }
    m_headingCombo->setEnabled(!on);
    // 导出按钮是 QToolButton widget，单独控制
    if (m_exportButton)
        m_exportButton->setEnabled(true);
    // 对齐/上下标下拉按钮是 QToolButton widget，随编辑按钮一起启停
    if (m_alignButton) m_alignButton->setEnabled(!on);
    if (m_subsupButton) m_subsupButton->setEnabled(!on);
    // 图标与提示随模式互换：普通态显示源码图标（点击进入源码），
    // 源码态显示 WYSIWYG 图标（点击返回所见即所得）
    if (m_sourceAction) {
        QString lightPath = on ? ":/icons/light/wysiwyg.svg"
                               : ":/icons/light/source-code.svg";
        m_iconPaths[m_sourceAction] = lightPath;  // 主题注册表同步，切主题不回退
        QString path = lightPath;
        if (m_isDark)
            path.replace("/light/", "/dark/");
        m_sourceAction->setIcon(QIcon(path));
        m_sourceAction->setToolTip(on ? tr("切换为所见即所得模式")
                                      : tr("切换源码模式"));
    }
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
    // 上下标回显：图标切换 + 菜单项勾选（旧 subscript/superscript 单按钮已折叠为下拉）
    if (f.contains("subscript") || f.contains("superscript")) {
        bool sub = f.contains("subscript") ? f["subscript"].toBool() : false;
        bool sup = f.contains("superscript") ? f["superscript"].toBool() : false;
        setSubsupIcon(sub, sup);
        rebuildSubsupMenuChecks(sub, sup);
    }
    TOGGLE_CHECK(m_bulletListAction, "bulletList");
    TOGGLE_CHECK(m_orderedListAction, "orderedList");
    TOGGLE_CHECK(m_taskListAction, "taskList");
    TOGGLE_CHECK(m_blockquoteAction, "blockquote");
    TOGGLE_CHECK(m_codeBlockAction, "codeBlock");
#undef TOGGLE_CHECK
    // 文字对齐回显：切换按钮图标 + 同步菜单项勾选（无对应属性时默认左对齐）
    if (f.contains("textAlign")) {
        QString a = f["textAlign"].toString();
        if (a.isEmpty()) a = "left";
        setAlignIcon(a);
        rebuildAlignMenuChecks(a);
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
    // 导出按钮是 QToolButton widget，单独处理图标主题
    if (m_exportButton) {
        QString iconPath = m_isDark ? ":/icons/dark/pdf.svg" : ":/icons/light/pdf.svg";
        m_exportButton->setIcon(QIcon(iconPath));
    }
    // 对齐/上下标按钮：按记录的当前状态重选主题图标
    if (m_alignButton) setAlignIcon(m_currentAlign);
    if (m_subsupButton) {
        // 从菜单勾选状态恢复激活态，避免另存状态成员
        bool sup = false, sub = false;
        if (QAction *a = m_subsupMenu->findChild<QAction*>("superscriptItem")) sup = a->isChecked();
        if (QAction *a = m_subsupMenu->findChild<QAction*>("subscriptItem")) sub = a->isChecked();
        setSubsupIcon(sub, sup);
    }
}

bool ToolBar::eventFilter(QObject *obj, QEvent *event)
{
    // 下拉按钮统一：点击即弹出菜单（导出/对齐/上下标）
    QToolButton *dropBtn = nullptr;
    if (obj == m_exportButton) dropBtn = m_exportButton;
    else if (obj == m_alignButton) dropBtn = m_alignButton;
    else if (obj == m_subsupButton) dropBtn = m_subsupButton;
    if (dropBtn && event->type() == QEvent::MouseButtonPress) {
        if (dropBtn->menu()) {
            dropBtn->menu()->popup(
                dropBtn->mapToGlobal(QPoint(0, dropBtn->height())));
        }
        return true;
    }
    return QToolBar::eventFilter(obj, event);
}
