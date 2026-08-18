#ifndef MD_EDITOR_TOOLBAR_H
#define MD_EDITOR_TOOLBAR_H

#include <QToolBar>
#include <QComboBox>

class QLabel;
class QMenu;
class QToolButton;

class ToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);

    void updateFormatState(const QJsonObject &format);
    void updateHistoryState(bool canUndo, bool canRedo);
    void refreshThemeIcons(const QString &theme);
    void setSourceMode(bool on);

    // 语言切换后重设全部 tooltip/菜单文案（Task 10 调用）
    void retranslateUi();

signals:
    void boldClicked();
    void italicClicked();
    void strikethroughClicked();
    void codeClicked();
    void underlineClicked();
    void highlightClicked();
    void subscriptClicked();
    void superscriptClicked();
    void textColorSelected(const QString &color);
    void bgColorSelected(const QString &color);
    void textColorCleared();
    void bgColorCleared();
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
    void footnoteClicked();
    void clearFormatClicked();
    void sourceToggleClicked();
    void pdfExportClicked();
    void docxExportClicked();
    void htmlExportClicked();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUi();
    QLabel *m_headingLabel = nullptr;
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
    QAction *m_textColorAction;
    QAction *m_bgColorAction;
    QMenu *buildColorMenu(bool forText);
    QString m_currentTextColor;   // 回显：当前光标处 colorSpan 的文字色
    QString m_currentBgColor;     // 回显：当前光标处 colorSpan 的背景色
    QToolButton *m_alignButton = nullptr;   // 对齐下拉按钮（左/中/右折叠）
    QToolButton *m_subsupButton = nullptr;  // 上下标下拉按钮（上标/下标折叠）
    QMenu *m_alignMenu = nullptr;
    QMenu *m_subsupMenu = nullptr;
    QAction *m_alignLeftAct = nullptr, *m_alignCenterAct = nullptr, *m_alignRightAct = nullptr;
    QAction *m_superscriptAct = nullptr, *m_subscriptAct = nullptr;
    QString m_currentAlign = QStringLiteral("left");  // 当前对齐状态，主题切换时重选图标
    // 按对齐状态设置按钮图标（light/dark 由 m_isDark 决定）
    void setAlignIcon(const QString &alignment);
    // 按上下标激活状态设置按钮图标与 checked 高亮
    void setSubsupIcon(bool sub, bool sup);
    // 同步菜单项勾选状态
    void rebuildAlignMenuChecks(const QString &alignment);
    void rebuildSubsupMenuChecks(bool sub, bool sup);
    QAction *m_bulletListAction;
    QAction *m_orderedListAction;
    QAction *m_taskListAction;
    QAction *m_blockquoteAction;
    QAction *m_footnoteAction = nullptr;
    QAction *m_clearFormatAction = nullptr;
    QToolButton *m_exportButton = nullptr;
    QMenu *m_exportMenu = nullptr;
    QList<QAction*> m_allActions;
    bool m_isDark;
    QMap<QAction*, QString> m_iconPaths; // action → 亮色图标路径
    void applyIconTheme(const QString &theme);
};

#endif // MD_EDITOR_TOOLBAR_H
