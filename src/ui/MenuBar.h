#ifndef MD_EDITOR_MENUBAR_H
#define MD_EDITOR_MENUBAR_H

#include <QMenuBar>

class MenuBar : public QMenuBar {
    Q_OBJECT
public:
    explicit MenuBar(QWidget *parent = nullptr);

    void addRecentFile(const QString &path);
    void loadRecentFiles();

    // 按偏好模式（system/light/dark）勾选主题菜单项
    void setThemeChecked(const QString &mode);

signals:
    void newFile();
    void openFile();
    void openFolder();
    void openRecentFile(const QString &path);
    void saveFile();
    void saveFileAs();
    void closeTab();
    void exportPdf();
    void exportDocx();
    void exportHtml();
    void quit();

    void undo();
    void redo();
    void findRequested();
    void replaceRequested();

    void toggleSidebar();
    void toggleOutline();
    void toggleSourceMode();
    void toggleFullscreen();

    void insertLink();
    void insertImage();
    void insertTable();
    void insertMath();
    void insertCode();
    void insertCodeBlock();
    void insertHorizontalRule();
    void insertEmoji();
    void insertFootnote();

    void switchTheme(const QString &name);

    void showAbout();
    void showSettings();
    void showMarkdownGuide();

private:
    void setupUi();
    void updateRecentFilesMenu();

    QMenu *m_fileMenu;
    QMenu *m_recentFilesMenu;
    QMenu *m_editMenu;
    QMenu *m_viewMenu;
    QMenu *m_insertMenu;
    QMenu *m_helpMenu;

    QList<QAction *> m_themeActs;  // 主题菜单项（与 system/light/dark 顺序对应）
};

#endif // MD_EDITOR_MENUBAR_H
