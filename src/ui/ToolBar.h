#ifndef MD_EDITOR_TOOLBAR_H
#define MD_EDITOR_TOOLBAR_H

#include <QToolBar>
#include <QComboBox>

class ToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

    void updateFormatState(const QJsonObject &format);

signals:
    void boldClicked();
    void italicClicked();
    void strikethroughClicked();
    void codeClicked();
    void underlineClicked();
    void highlightClicked();
    void headingSelected(int level);
    void bulletListClicked();
    void orderedListClicked();
    void taskListClicked();
    void blockquoteClicked();
    void linkClicked();
    void imageClicked();
    void tableClicked();
    void codeBlockClicked();
    void horizontalRuleClicked();
    void sourceToggleClicked();
    void pdfExportClicked();

private:
    void setupUi();
    QComboBox *m_headingCombo;
    QAction *m_boldAction;
    QAction *m_italicAction;
    QAction *m_strikeAction;
    QAction *m_codeAction;
    QAction *m_codeBlockAction;
    QAction *m_underlineAction;
    QAction *m_highlightAction;
    QAction *m_bulletListAction;
    QAction *m_orderedListAction;
    QAction *m_taskListAction;
    QAction *m_blockquoteAction;
};

#endif // MD_EDITOR_TOOLBAR_H
