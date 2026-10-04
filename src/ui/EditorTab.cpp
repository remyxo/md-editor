#include "EditorTab.h"
#include "../core/JsEscape.h"
#include "../core/LocalImageSchemeHandler.h"
#include "../core/MarkdownHandler.h"
#include "../app/Application.h"
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QUrl>
#include <QDesktopServices>
#include <QColor>
#include <QDebug>
#include <QContextMenuEvent>
#include <QAction>
#include <QTimer>   // 大文档加载后的序列化冷 JIT 预热
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>

// ===== EditorWebPage — 过滤 Mermaid SVG 引擎级报错 =====

class EditorWebPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
protected:
    // 导航白名单：编辑器页只允许 qrc（自身资源）与 localimg（本地图片），
    // about 为引擎内部页。SVG 锚点（mermaid 图内链接）与任何注入产生的顶层
    // 导航（file:/javascript:/远程页）一律拒绝——曾可把整个编辑器 WebView
    // 导航离页（标签页白屏）
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
        Q_UNUSED(type); Q_UNUSED(isMainFrame);
        const QString scheme = url.scheme();
        return scheme == QLatin1String("qrc")
            || scheme == QLatin1String("localimg")
            || scheme == QLatin1String("about");
    }
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

    QAction *undoAct = m_contextMenu->addAction(tr("撤销(&U)"));
    connect(undoAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Undo);
    });

    QAction *redoAct = m_contextMenu->addAction(tr("重做(&R)"));
    connect(redoAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Redo);
    });

    m_contextMenu->addSeparator();

    QAction *cutAct = m_contextMenu->addAction(tr("剪切(&X)"));
    connect(cutAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Cut);
    });

    QAction *copyAct = m_contextMenu->addAction(tr("复制(&C)"));
    connect(copyAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Copy);
    });

    QAction *pasteAct = m_contextMenu->addAction(tr("粘贴(&V)"));
    connect(pasteAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::Paste);
    });

    m_contextMenu->addSeparator();

    QAction *selectAllAct = m_contextMenu->addAction(tr("全选(&A)"));
    connect(selectAllAct, &QAction::triggered, [this]() {
        page()->triggerAction(QWebEnginePage::SelectAll);
    });

    // 选中文字或在已有链接上 → 编辑超链接
    if (m_hasSelection || !m_linkUrl.isEmpty()) {
        m_contextMenu->addSeparator();
        QAction *createLink = m_contextMenu->addAction(
            tr("编辑超链接..."));
        connect(createLink, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi._linkEditor.open();");
        });
    }

    // 链接访问 — 仅光标在链接上时显示
    if (!m_linkUrl.isEmpty()) {
        QAction *visitLink = m_contextMenu->addAction(
            tr("访问 %1").arg(m_linkUrl));
        connect(visitLink, &QAction::triggered, [this]() {
            QDesktopServices::openUrl(QUrl(m_linkUrl));
        });
    }

    // 表格操作子菜单 — 仅光标在表格内时显示
    if (m_inTable) {
        m_contextMenu->addSeparator();
        QMenu *tableMenu = m_contextMenu->addMenu(tr("表格"));
        QAction *addRowBefore = tableMenu->addAction(tr("上方插入行"));
        connect(addRowBefore, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addRowBefore();");
        });
        QAction *addRowAfter = tableMenu->addAction(tr("下方插入行"));
        connect(addRowAfter, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addRowAfter();");
        });
        QAction *delRow = tableMenu->addAction(tr("删除当前行"));
        connect(delRow, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.deleteRow();");
        });
        tableMenu->addSeparator();
        QAction *addColBefore = tableMenu->addAction(tr("左侧插入列"));
        connect(addColBefore, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addColumnBefore();");
        });
        QAction *addColAfter = tableMenu->addAction(tr("右侧插入列"));
        connect(addColAfter, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.addColumnAfter();");
        });
        QAction *delCol = tableMenu->addAction(tr("删除当前列"));
        connect(delCol, &QAction::triggered, [this]() {
            page()->runJavaScript("window.editorApi.deleteColumn();");
        });
        tableMenu->addSeparator();
        QAction *delTable = tableMenu->addAction(tr("删除表格"));
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

    // 加载编辑器页面（附当前主题：首帧即暗色，避免暗色模式下的亮色闪烁）
    const QString theme = Application::instance()->themeManager()->currentTheme();
    // 页面加载完成前 WebView 控件绘制的是 QWebEnginePage 默认白底（TipTap bundle
    // 解析 + JS 初始化期间），暗色下会先白后暗；load 前同步把页面背景设为暗色。
    if (theme == "dark") {
        m_webView->page()->setBackgroundColor(QColor("#1e1e1e"));
    }
    m_webView->load(QUrl("qrc:/web/editor.html?theme=" + theme));
}

EditorBridge *EditorTab::bridge() const { return m_bridge; }
QWebEngineView *EditorTab::webView() const { return m_webView; }

void EditorTab::loadFile(const QString &path, const QString &content)
{
    Q_UNUSED(path)
    // 立即加载：作废未决的惰性内容（如外部变更重载先于激活发生，防止陈旧覆盖）
    m_pendingContent.clear();
    if (m_pageLoaded) {
        // 新加载作废旧：在途/排队的加载类转换完成后被代次护栏丢弃——快速
        // 连续重载（外部变更风暴）不会把旧内容 setContent 进编辑器
        m_convQueue.bumpLoadEpoch();
        const qint64 contentSize = content.size();
        // MD → HTML（含 body 提取）移至 worker 线程：4k 级文档 toHtml 达
        // 数百毫秒，同步执行期间 GUI 线程冻结（窗口不可拖动、菜单无响应）。
        // worker lambda 只捕获纯数据；IPC 调用与定时器留在 GUI 线程完成回调
        m_convQueue.enqueue(
            [content]() {
                QString fullHtml = MarkdownHandler::toHtml(content);

                // 提取 <body> 内容（QTextDocument::toHtml 返回完整 HTML 文档）
                int bodyStart = fullHtml.indexOf("<body");
                int bodyEnd = fullHtml.indexOf("</body>");
                if (bodyStart >= 0 && bodyEnd > bodyStart) {
                    bodyStart = fullHtml.indexOf('>', bodyStart) + 1;
                    return fullHtml.mid(bodyStart, bodyEnd - bodyStart).trimmed();
                }
                return fullHtml;
            },
            [this, content, contentSize](const QString &html, qint64) {
                // 转义后嵌入模板字面量（\ ` $，单一来源见 core/JsEscape.h）
                QString js = QString("window.editorApi.setContent(`%1`);")
                                 .arg(JsEscape::forTemplateLiteral(html));
                m_webView->page()->runJavaScript(js);
                // 大文档冷 JIT 预热：内容就绪 1.5 秒后空闲跑一次丢弃式
                // getHTML，消除首次切换源码模式 ~1s 的冷 JIT 停顿（编辑器
                // 聚焦时 JS 侧自动推迟重试）
                if (contentSize >= 8192) {
                    QTimer::singleShot(1500, this, [this]() {
                        if (m_pageLoaded && m_webView)
                            m_webView->page()->runJavaScript(
                                QStringLiteral("window.editorApi.warmup()"));
                    });
                }

                // 外部文件变更时若处于源码模式，同步刷新 textarea 内容
                // setContent 仅更新 WYSIWYG 编辑器，源码区 <textarea> 需单独更新。
                // 守卫前置到 C++（2026-09-28 速赢）：常规加载（非源码模式）下
                // 这段全文 MD IPC 纯浪费——载荷翻倍，守卫在 JS 端才判等于白发。
                // 正常路径（页面刚加载）不可能处于源码模式，守卫恒假即零发送。
                if (isSourceMode()) {
                    // 与 setContent 同一模板字面量转义（\ ` $），单一来源 core/JsEscape.h
                    QString mdEscaped = JsEscape::forTemplateLiteral(content);
                    runJsCommand(QString(
                        "(function(){"
                        "var a=window.editorApi;"
                        "if(!a||!a.isSourceMode)return;"
                        "var sv=document.getElementById('source-view');"
                        "if(!sv)return;"
                        "sv.value=`%1`;a._sourceValue=`%1`;"
                        "if(a._srcSearch)a._srcOnSourceEdit();"   // 外部同步不触发 input，手动刷新搜索高亮
                        "})()").arg(mdEscaped));
                }
            },
            /*tiedToLoadEpoch=*/true);
    }
}

void EditorTab::enqueueConversion(ConversionQueue::ConvFn conv, ConversionQueue::Done done,
                                  bool tiedToLoadEpoch)
{
    m_convQueue.enqueue(std::move(conv), std::move(done), tiedToLoadEpoch);
}

EditorTab::~EditorTab()
{
    // 撤销 localimg:// 目录围栏注册（引用计数，同目录其他标签页不受影响）。
    // 曾只增不减：长期会话中授权面随打开过的目录单调增长
    if (!m_allowedDir.isEmpty())
        LocalImageSchemeHandler::removeAllowedDir(m_allowedDir);
}

void EditorTab::setFilePath(const QString &path)
{
    m_webView->setProperty("filePath", path);
    // localimg:// 目录围栏：该文档目录内的本地图片才可被编辑器加载。
    // 目录变更时撤销旧注册（引用计数，同目录多标签不受影响）；同路径
    // 重复调用不再叠加引用
    const QString dir = QFileInfo(path).absolutePath();
    if (dir != m_allowedDir) {
        if (!m_allowedDir.isEmpty())
            LocalImageSchemeHandler::removeAllowedDir(m_allowedDir);
        if (!dir.isEmpty()) {
            LocalImageSchemeHandler::addAllowedDir(dir);
            m_allowedDir = dir;
        } else {
            m_allowedDir.clear();
        }
    }
    pushDocDir();   // 页面已就绪则立即推送；否则由 onPageLoadFinished 补推
}

void EditorTab::setPendingContent(const QString &md)
{
    m_pendingContent = md;
}

// 加载未决的惰性内容（仅当页面就绪；由 TabWidget 在本 tab 成为当前标签页时调用）
bool EditorTab::flushPendingContent()
{
    if (m_pendingContent.isNull() || !m_pageLoaded)
        return false;
    QString content = m_pendingContent;
    m_pendingContent.clear();
    loadFile(QString(), content);
    return true;
}

void EditorTab::setEncoding(const QString &encoding)
{
    m_webView->setProperty("encoding", encoding);
}

QString EditorTab::encoding() const
{
    return m_webView->property("encoding").toString();
}

void EditorTab::pushDocDir()
{
    QString path = m_webView->property("filePath").toString();
    if (path.isEmpty() || !m_pageLoaded) return;
    QString dir = QFileInfo(path).absolutePath();
    // 旧内联版只转义 \ 与 '；收敛到 JsEscape::jsSingleQuoted 后额外覆盖
    // \n/\r——目录路径含换行时旧版产生裸换行的非法 JS（setDocDir 静默
    // 失败），无换行路径输出逐字节不变
    runJsCommand(QString("window.editorApi.setDocDir('%1');")
                     .arg(JsEscape::jsSingleQuoted(dir)));
}

void EditorTab::setEditorTheme(const QString &theme)
{
    // 页面背景色跟随主题，避免切换主题后新建/重载页面仍闪白
    m_webView->page()->setBackgroundColor(
        theme == "dark" ? QColor("#1e1e1e") : QColor("#ffffff"));
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setTheme('%1');").arg(theme));
    }
}

void EditorTab::setEditorLanguage(const QString &lang)
{
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setLanguage('%1');").arg(lang));
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

void EditorTab::applyCustomCss(const QString &css)
{
    if (m_pageLoaded) {
        // 单引号字符串安全化（\ ' \n \r，单一来源见 core/JsEscape.h）
        runJsCommand(QString("window.editorApi.applyCustomCss('%1');")
                         .arg(JsEscape::jsSingleQuoted(css)));
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

void EditorTab::setSourceMode(bool on)
{
    m_sourceMode = on;
}

bool EditorTab::isSourceMode() const
{
    return m_sourceMode;
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
    // 新标签页加载完成：补推文档目录（先于 TabWidget 的 setContent，图片首渲即用对目录）
    if (ok) pushDocDir();
    emit pageLoadFinished(ok);
}
