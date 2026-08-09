#ifndef MD_EDITOR_TOOLBAR_H
#define MD_EDITOR_TOOLBAR_H

#include <QToolBar>
#include <QComboBox>

class ToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

    void updateFormatState(const QJsonObject &format);
    void updateHistoryState(bool canUndo, bool canRedo);
    void refreshThemeIcons(const QString &theme);
    void setReadMode(bool readMode);

signals:
    void boldClicked();
    void italicClicked();
    void strikethroughClicked();
    void codeClicked();
    void underlineClicked();
    void highlightClicked();
    void subscriptClicked();
    void superscriptClicked();
    void alignSelected(const QString &alignment);
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
    void mathClicked();
    void mermaidClicked();
    void emojiClicked();
    void sourceToggleClicked();
    void pdfExportClicked();
    void toggleReadModeClicked();

private:
    void setupUi();
    QComboBox *m_headingCombo;
    QAction *m_undoAction;
    QAction *m_redoAction;
    QAction *m_boldAction;
    QAction *m_italicAction;
    QAction *m_strikeAction;
    QAction *m_codeAction;
    QAction *m_codeBlockAction;
    QAction *m_underlineAction;
    QAction *m_highlightAction;
    QAction *m_subscriptAction;
    QAction *m_superscriptAction;
    QAction *m_alignLeftAction;
    QAction *m_alignCenterAction;
    QAction *m_alignRightAction;
    QAction *m_alignJustifyAction;
    QActionGroup *m_alignGroup;
    QAction *m_bulletListAction;
    QAction *m_orderedListAction;
    QAction *m_taskListAction;
    QAction *m_blockquoteAction;
    QAction *m_readModeAction;
    QList<QAction*> m_allActions;
    bool m_isDark;
    QMap<QAction*, QString> m_iconPaths; // action → 亮色图标路径
    void applyIconTheme(const QString &theme);
};

#endif // MD_EDITOR_TOOLBAR_H
