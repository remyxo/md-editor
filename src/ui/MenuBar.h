#ifndef MD_EDITOR_MENUBAR_H
#define MD_EDITOR_MENUBAR_H

#include <QMenuBar>

class MenuBar : public QMenuBar {
    Q_OBJECT
public:
    explicit MenuBar(QWidget *parent = nullptr);

signals:
    void newFile();
    void openFile();
    void openFolder();
    void saveFile();
    void saveFileAs();
    void closeTab();
    void exportPdf();
    void quit();

    void undo();
    void redo();

    void toggleSidebar();
    void toggleOutline();
    void toggleSourceMode();
    void toggleFullscreen();

    void insertLink();
    void insertImage();
    void insertTable();
    void insertHorizontalRule();

    void switchTheme(const QString &name);

    void showAbout();
    void showSettings();

private:
    void setupUi();
    QMenu *m_fileMenu;
    QMenu *m_editMenu;
    QMenu *m_viewMenu;
    QMenu *m_insertMenu;
    QMenu *m_formatMenu;
    QMenu *m_helpMenu;
};

#endif // MD_EDITOR_MENUBAR_H
