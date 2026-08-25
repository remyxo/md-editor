#ifndef MD_EDITOR_MENUBAR_H
#define MD_EDITOR_MENUBAR_H

#include <QMenuBar>

class MenuBar : public QMenuBar {
    Q_OBJECT
public:
    explicit MenuBar(QWidget *parent = nullptr);

    void addRecentFile(const QString &path);

    // 按偏好模式（system/light/dark）勾选主题菜单项
    void setThemeChecked(const QString &mode);

    // 语言切换后重设全部菜单/动作文案（Task 10 调用）
    void retranslateUi();

signals:
    void newFile();
    void openFile();
    void openFolder();
    void openRecentFile(const QString &path);
    void saveFile();
    void saveFileAs();
    void exportPdf();
    void exportDocx();
    void exportHtml();
    void quit();

    void undo();
    void redo();
    void findRequested();
    void replaceRequested();

    void toggleFileTree(bool visible);
    void toggleOutline(bool visible);
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

    // === 文件菜单动作 ===
    QAction *m_newAct, *m_openAct, *m_openFolderAct, *m_saveAct, *m_saveAsAct;
    QMenu *m_exportMenu;
    QAction *m_exportPdfAct, *m_exportDocxAct, *m_exportHtmlAct, *m_quitAct;

    // === 编辑菜单动作 ===
    QAction *m_undoAct, *m_redoAct, *m_findAct, *m_replaceAct;

    // === 视图菜单动作 ===
    QAction *m_fileTreeAct, *m_outlineAct, *m_sourceAct, *m_fullscreenAct;
    QMenu *m_themeMenu;

    // === 插入菜单动作 ===
    QAction *m_linkAct, *m_imageAct, *m_tableAct, *m_mathAct, *m_codeAct,
            *m_codeBlockAct, *m_hrAct, *m_emojiAct, *m_footnoteAct;

    // === 帮助菜单动作 ===
    QAction *m_guideAct, *m_settingsAct, *m_aboutAct;

    QList<QAction *> m_themeActs;  // 主题菜单项（与 system/light/dark 顺序对应）
};

#endif // MD_EDITOR_MENUBAR_H
