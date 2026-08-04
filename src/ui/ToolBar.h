#ifndef MD_EDITOR_TOOLBAR_H
#define MD_EDITOR_TOOLBAR_H

#include <QToolBar>
#include <QComboBox>

class ToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

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
    void horizontalRuleClicked();
    void sourceToggleClicked();
    void pdfExportClicked();

private:
    void setupUi();
    QComboBox *m_headingCombo;
};

#endif // MD_EDITOR_TOOLBAR_H
