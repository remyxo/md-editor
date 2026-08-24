#ifndef MD_EDITOR_MAINWINDOW_H
#define MD_EDITOR_MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QCloseEvent>
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
    void refreshEncodingLabel();   // 读取当前标签页编码 → 状态栏
    void onImageSaveRequested(EditorTab *tab, const QString &requestId,
                              const QString &base64, const QString &ext,
                              const QString &suggestedName);
    EditorBridge *currentBridge() const;

    // 当前主题偏好模式（system/light/dark）
    QString currentThemeMode() const;

    // 全量刷新各组件界面文本（语言切换后调用）
    void retranslateUi();

    // 会话恢复
    void saveSession();
    void restoreSession();

    MenuBar *m_menuBar;
    ToolBar *m_toolBar;
    SideBar *m_sideBar;
    QShortcut *m_sidebarShortcut = nullptr;
    std::pair<bool, bool> m_lastPaneCombo{true, true};  // Ctrl+B 收起前的面板组合
    TabWidget *m_tabWidget;
    StatusBar *m_statusBar;
    SearchBar *m_searchBar;
    FileManager *m_fileManager;
    SystemThemeWatcher *m_systemThemeWatcher = nullptr;
    QString m_themeMode = QStringLiteral("light");  // 偏好模式，非实际主题
    QSplitter *m_splitter;
    bool m_modalDialogOpen = false;  // 当前标签页是否有模态编辑面板打开（Esc 优先级判断）
    bool m_sourceModeToggling = false;   // 异步切换进行中（防重入）
};

#endif // MD_EDITOR_MAINWINDOW_H
