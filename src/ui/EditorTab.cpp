#include "EditorTab.h"
#include "../core/MarkdownHandler.h"
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QUrl>
#include <QDesktopServices>
#include <QDebug>
#include <QContextMenuEvent>
#include <QAction>
#include <QJsonDocument>
#include <QJsonObject>

// ===== EditorWebPage — 过滤 Mermaid SVG 引擎级报错 =====

class EditorWebPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
        const QString &msg, int line, const QString &src) override {
        Q_UNUSED(level); Q_UNUSED(line); Q_UNUSED(src);
        // Mermaid 内部 innerHTML 赋值时产生的 SVG 负 rect 解析报错：
        // editor.js 的 _sanitizeMermaidSvg + _fixNegativeRects 会修正最终 DOM，
        // 此处为浏览器引擎级消息，JS console API 覆写无法拦截，故在 C++ 侧过滤。
        if (msg.contains("A negative value is not valid"))
            return;
        qDebug() << "[JS]" << msg;
    }
};

// ===== EditorWebView — 中文上下文菜单 =====

EditorWebView::EditorWebView(QWidget *parent)
    : QWebEngineView(parent), m_contextMenu(nullptr),
      m_inTable(false), m_hasSelection(false)
{
    setPage(new EditorWebPage(this));
}

void EditorWebView::setInTable(bool inTable) { m_inTable = inTable; }
void EditorWebView::setHasSelection(bool has) { m_hasSelection = has; }
void EditorWebView::setLinkUrl(const QString &url) { m_linkUrl = url; }

void EditorWebView::setupContextMenu()
{
    if (!m_contextMenu) {
        m_contextMenu = new QMenu(this);
    }
    m_contextMenu->clear();

    QAction *undoAct = m_contextMenu->addAction("撤销(&U)");
    connect(undoAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Undo);
    });

    QAction *redoAct = m_contextMenu->addAction("重做(&R)");
    connect(redoAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Redo);
    });

    m_contextMenu->addSeparator();

    QAction *cutAct = m_contextMenu->addAction("剪切(&X)");
    connect(cutAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Cut);
    });

    QAction *copyAct = m_contextMenu->addAction("复制(&C)");
    connect(copyAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Copy);
    });

    QAction *pasteAct = m_contextMenu->addAction("粘贴(&V)");
    connect(pasteAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Paste);
    });

    m_contextMenu->addSeparator();

    QAction *selectAllAct = m_contextMenu->addAction("全选(&A)");
    connect(selectAllAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::SelectAll);
    });

    // 链接访问 — 仅光标在链接上时显示
    if (!m_linkUrl.isEmpty()) {
        m_contextMenu->addSeparator();
        QAction *visitLink = m_contextMenu->addAction(
            QString::fromUtf8("访问 %1").arg(m_linkUrl));
        connect(visitLink, &QAction::triggered, [this]() {
            QDesktopServices::openUrl(QUrl(m_linkUrl));
        });
    }

    // 表格操作子菜单 — 仅光标在表格内时显示
    if (m_inTable) {
        m_contextMenu->addSeparator();
        QMenu *tableMenu = m_contextMenu->addMenu("表格");
        QAction *addRowBefore = tableMenu->addAction("上方插入行");
        connect(addRowBefore, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addRowBefore();");
        });
        QAction *addRowAfter = tableMenu->addAction("下方插入行");
        connect(addRowAfter, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addRowAfter();");
        });
        QAction *delRow = tableMenu->addAction("删除当前行");
        connect(delRow, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.deleteRow();");
        });
        tableMenu->addSeparator();
        QAction *addColBefore = tableMenu->addAction("左侧插入列");
        connect(addColBefore, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addColumnBefore();");
        });
        QAction *addColAfter = tableMenu->addAction("右侧插入列");
        connect(addColAfter, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addColumnAfter();");
        });
        QAction *delCol = tableMenu->addAction("删除当前列");
        connect(delCol, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.deleteColumn();");
        });
        tableMenu->addSeparator();
        QAction *delTable = tableMenu->addAction("删除表格");
        connect(delTable, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.deleteTable();");
        });
    }
}

void EditorWebView::contextMenuEvent(QContextMenuEvent *event)
{
    // 先查询当前链接，再构建菜单（避免上次残留状态）
    page()->runJavaScript(
        "var ed=window.editorApi&&window.editorApi.editor;"
        "var u='';"
        "if(ed&&ed.isActive('link')){"
        "var a=ed.getAttributes('link');if(a&&a.href)u=a.href;}"
        "u;",
        [this, pos = event->globalPos()](const QVariant &result) {
            m_linkUrl = result.toString();
            setupContextMenu();
            m_contextMenu->popup(pos);
        });
}

// ===== EditorTab =====

EditorTab::EditorTab(int tabId, QWidget *parent)
    : QWidget(parent)
    , m_tabId(tabId)
    , m_webView(new EditorWebView(this))
    , m_webChannel(new QWebChannel(this))
    , m_bridge(new EditorBridge(tabId, this))
    , m_pageLoaded(false)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_webView);

    // 允许加载本地资源
    QWebEngineSettings *settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    setupWebChannel();

    connect(m_webView, &QWebEngineView::loadFinished,
            this, &EditorTab::onPageLoadFinished);

    // 加载编辑器页面
    m_webView->load(QUrl("qrc:/web/editor.html"));
}

EditorBridge *EditorTab::bridge() const { return m_bridge; }
QWebEngineView *EditorTab::webView() const { return m_webView; }

void EditorTab::loadFile(const QString &path, const QString &content)
{
    Q_UNUSED(path)
    if (m_pageLoaded) {
        // MD → HTML 转换
        QString fullHtml = MarkdownHandler::toHtml(content);

        // 提取 <body> 内容（QTextDocument::toHtml 返回完整 HTML 文档）
        int bodyStart = fullHtml.indexOf("<body");
        int bodyEnd = fullHtml.indexOf("</body>");
        QString html;
        if (bodyStart >= 0 && bodyEnd > bodyStart) {
            bodyStart = fullHtml.indexOf('>', bodyStart) + 1;
            html = fullHtml.mid(bodyStart, bodyEnd - bodyStart).trimmed();
        } else {
            html = fullHtml;
        }

        QString escaped = html;
        escaped.replace("\\", "\\\\");
        escaped.replace("`", "\\`");
        escaped.replace("$", "\\$");
        QString js = QString("window.editorApi.setContent(`%1`);").arg(escaped);
        runJsCommand(js);

        // 外部文件变更时若处于源码模式，同步刷新 textarea 内容
        // setContent 仅更新 WYSIWYG 编辑器，源码区 <textarea> 需单独更新
        QString mdEscaped = content;
        mdEscaped.replace("\\", "\\\\").replace("`", "\\`");
        mdEscaped.replace("$", "\\$");
        runJsCommand(QString(
            "(function(){"
            "var a=window.editorApi;"
            "if(!a||!a.isSourceMode)return;"
            "var sv=document.getElementById('source-view');"
            "if(!sv)return;"
            "sv.value=`%1`;a._sourceValue=`%1`;"
            "})()").arg(mdEscaped));
    }
}

void EditorTab::setEditorTheme(const QString &theme)
{
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setTheme('%1');").arg(theme));
    }
}

void EditorTab::setEditorFont(const QString &family, int size)
{
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setFont('%1', %2);").arg(family).arg(size));
    }
}

void EditorTab::setEditorWidth(int width)
{
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setWidth(%1);").arg(width));
    }
}

void EditorTab::setEditorSettings(const QJsonObject &settings)
{
    if (m_pageLoaded) {
        QJsonDocument doc(settings);
        QString json = doc.toJson(QJsonDocument::Compact);
        runJsCommand(QString("window.editorApi.applySettings(%1);").arg(json));
    }
}

void EditorTab::setInTable(bool inTable)
{
    m_webView->setInTable(inTable);
}

void EditorTab::setHasSelection(bool has)
{
    m_webView->setHasSelection(has);
}

void EditorTab::setPermanentReadOnly(bool permanent)
{
    m_permanentReadOnly = permanent;
}

bool EditorTab::isPermanentReadOnly() const
{
    return m_permanentReadOnly;
}

void EditorTab::setLinkUrl(const QString &url)
{
    m_webView->setLinkUrl(url);
}

void EditorTab::runJsCommand(const QString &js)
{
    m_webView->page()->runJavaScript(js);
}

void EditorTab::setupWebChannel()
{
    m_webChannel->registerObject("bridge", m_bridge);
    m_webView->page()->setWebChannel(m_webChannel);
}

void EditorTab::onPageLoadFinished(bool ok)
{
    m_pageLoaded = ok;
    // QWebChannel JS 端初始化已由 editor.js 中的 DOMContentLoaded 处理
    // setupWebChannel() 在 load() 之前调用，确保 qt.webChannelTransport 可用
    emit pageLoadFinished(ok);
}
