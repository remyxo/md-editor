#include "EditorTab.h"
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QUrl>

EditorTab::EditorTab(int tabId, QWidget *parent)
    : QWidget(parent)
    , m_tabId(tabId)
    , m_webView(new QWebEngineView(this))
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
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

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
        QString escaped = content;
        escaped.replace("\\", "\\\\");
        escaped.replace("`", "\\`");
        escaped.replace("$", "\\$");
        QString js = QString("window.editorApi.setContent(`%1`);").arg(escaped);
        runJsCommand(js);
    }
}

void EditorTab::setEditorTheme(const QString &theme)
{
    if (m_pageLoaded) {
        runJsCommand(QString("window.editorApi.setTheme('%1');").arg(theme));
    }
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
    if (ok) {
        // 页面加载完成后初始化 QWebChannel JS 端
        runJsCommand(R"(
            if (typeof QWebChannel !== 'undefined') {
                new QWebChannel(qt.webChannelTransport, function(channel) {
                    window.editorApi.bridge = channel.objects.bridge;
                });
            }
        )");
    }
    emit pageLoadFinished(ok);
}
