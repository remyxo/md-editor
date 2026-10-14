#ifndef MD_EDITOR_MAINWINDOW_H
#define MD_EDITOR_MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QCloseEvent>
#include <functional>
#include <utility>

class MenuBar;
class ToolBar;
class SideBar;
class QShortcut;
class TabWidget;
class EditorTab;
class StatusBar;
class SearchBar;
class EditorBridge;
class FileManager;
class SystemThemeWatcher;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &initialFile = QString(), QWidget *parent = nullptr);
    ~MainWindow();

    // ── selftest 专用入口（--selftest，无头自测试；见 app/SelfTestRunner.cpp）──
    EditorTab *selfTestCurrentTab();
    void selfTestToggleSourceMode();

private slots:
    void onEditorBridgeCreated(EditorBridge *bridge);
    void onTabChanged(int tabId);

    // 工具栏响应
    void onBoldClicked();
    void onItalicClicked();
    void onHeadingSelected(int level);
    void onInsertTable();
    void onInsertLink();
    void onInsertImage();
    void onInsertEmoji();
    void onToggleSourceMode();
    void onExportPdf();
    void onExportDocx();
    void onExportHtml();
    void onThemeSwitch(const QString &theme);

    // 语言切换
    void onLanguageSwitch(const QString &lang);

    // 文件操作
    void onOpenFolder();
    void onFileDoubleClicked(const QString &path);
    void onNewFile();
    void onSaveFile();
    void onSaveFileAs();

    // 格式同步
    void onFormatChanged(const QJsonObject &format, int tabId);

    // 设置
    void onShowSettings();
    void onShowMarkdownGuide();
    void applyEditorFont();
    void applyEditorWidth();
    void applyEditorSettings();
    void applyCustomCss();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupUi();
    void connectSignals();
    void runEditorCommand(const QString &js);
    void showSearchBar(bool withReplace);   // 显示搜索栏（withReplace: 展开替换行）
    void closeSearchBar();                  // 关闭搜索栏：隐藏 + 清高亮（stopSearch 单一收口）
    void refreshEncodingLabel();   // 读取当前标签页编码 → 状态栏
    void onImageSaveRequested(EditorTab *tab, const QString &requestId,
                              const QString &base64, const QString &ext,
                              const QString &suggestedName);

    // 保存共享核：异步取内容（源码模式取 source-view）→ 写盘 → markClean/
    // markTabSaved → cb(true/false)。onSaveFile/onSaveFileAs 与关闭时的逐 tab
    // 保存队列共用（旧 closeEvent 无回调地调 onSaveFile 后立即退出，异步回调
    // 随窗口销毁不再执行——点了「保存」实际未落盘）
    void writeTabToFile(EditorTab *tab, const QString &path, bool setPathAfter,
                        std::function<void(bool)> cb);
    // 关闭时的异步保存队列驱动与中止
    void processCloseSaveQueue();
    void abortCloseSaves();

    // 全量刷新各组件界面文本（语言切换后调用）
    void retranslateUi();

    // 会话恢复
    void saveSession();
    void restoreSession();
    // 惰性恢复标签激活读盘（W4-2）：EditorTab::pendingPathReadRequested 的落地端
    void loadPendingRestoredTab(EditorTab *tab, const QString &path);

    // ≥2MB 大文件软警告（手动打开与会话恢复共用）：true = 继续打开，false = 用户拒绝
    bool confirmLargeFile(const QString &path);

    MenuBar *m_menuBar;
    ToolBar *m_toolBar;
    SideBar *m_sideBar;
    QShortcut *m_sidebarShortcut = nullptr;
    std::pair<bool, bool> m_lastPaneCombo{true, true};  // Ctrl+B 收起前的面板组合
    TabWidget *m_tabWidget;
    StatusBar *m_statusBar;
    SearchBar *m_searchBar;
    FileManager *m_fileManager;
    // 会话恢复期间静默 fileReadError 弹窗（读失败条目直接跳过，避免启动弹窗轰炸）
    bool m_suppressReadErrors = false;
    SystemThemeWatcher *m_systemThemeWatcher = nullptr;
    QString m_themeMode = QStringLiteral("light");  // 偏好模式，非实际主题
    QSplitter *m_splitter;
    bool m_modalDialogOpen = false;  // 当前标签页是否有模态编辑面板打开（Esc 优先级判断）
    bool m_sourceModeToggling = false;   // 异步切换进行中（防重入）
    bool m_closeSavePending = false;     // 关闭前的异步保存队列进行中（防重入 close）
    QString m_lastWriteError;            // 最近一次 fileWriteError 的错误详情（失败弹窗用）
    QList<EditorTab *> m_closeSaveQueue; // 待落盘 tab 队列（逐个异步保存）
};

#endif // MD_EDITOR_MAINWINDOW_H
