#ifndef MD_EDITOR_SEARCHBAR_H
#define MD_EDITOR_SEARCHBAR_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

    void focusFindField();
    void setCount(int total, int current);      // bridge 回传：显示 n/m、禁用态
    void setReplaceMode(bool on);                // Ctrl+H 展开/收起替换行
    void emitParamsNow();                        // 立即触发一次（去抖 flush）
    void emitResumeNow();                        // 立即触发恢复（切 tab 用）
    void retranslateUi();
    bool replaceModeActive() const { return m_replaceRow->isVisible(); }

signals:
    void searchParamsChanged(const QString &text, bool caseSensitive, bool regex, bool wholeWord);
    // 切回标签页恢复搜索：JS 侧参数未变时保留当前匹配位置
    void searchResumeRequested(const QString &text, bool caseSensitive, bool regex, bool wholeWord);
    void findNextRequested();
    void findPrevRequested();
    void replaceRequested(const QString &rep);
    void replaceAllRequested(const QString &rep);
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;   // Enter→next / Shift+Enter→prev

private:
    void scheduleEmitParams();   // 启动 150ms 去抖

    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QPushButton *m_caseBtn;      // Aa 大小写
    QPushButton *m_regexBtn;     // .* 正则
    QPushButton *m_wordBtn;      // W| 全词
    QPushButton *m_nextBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_replaceBtn;
    QPushButton *m_replaceAllBtn;
    QPushButton *m_closeBtn;
    QLabel *m_countLabel;
    QWidget *m_replaceRow;       // 第二行容器（可显隐）
    QTimer *m_debounce;
};

#endif // MD_EDITOR_SEARCHBAR_H
