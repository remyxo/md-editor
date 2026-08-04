#ifndef MD_EDITOR_SEARCHBAR_H
#define MD_EDITOR_SEARCHBAR_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

    void focusFindField();

signals:
    void findRequested(const QString &text, bool caseSensitive);
    void replaceRequested(const QString &find, const QString &replace);
    void replaceAllRequested(const QString &find, const QString &replace);
    void closeRequested();

private:
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QPushButton *m_caseBtn;
};

#endif // MD_EDITOR_SEARCHBAR_H
