#ifndef MD_EDITOR_MAINWINDOW_H
#define MD_EDITOR_MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QCloseEvent>

class MenuBar;
class ToolBar;
class SideBar;
class TabWidget;
class StatusBar;
class SearchBar;
class EditorBridge;
class FileManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
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
    void onToggleSourceMode();
    void onExportPdf();
    void onThemeSwitch(const QString &theme);

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
    void applyEditorFont();
    void applyEditorWidth();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void connectSignals();
    void runEditorCommand(const QString &js);
    EditorBridge *currentBridge() const;

    // 会话恢复
    void saveSession();
    void restoreSession();

    MenuBar *m_menuBar;
    ToolBar *m_toolBar;
    SideBar *m_sideBar;
    TabWidget *m_tabWidget;
    StatusBar *m_statusBar;
    SearchBar *m_searchBar;
    FileManager *m_fileManager;
    QSplitter *m_splitter;
    bool m_readMode;
};

#endif // MD_EDITOR_MAINWINDOW_H
