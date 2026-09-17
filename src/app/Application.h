#ifndef MD_EDITOR_APPLICATION_H
#define MD_EDITOR_APPLICATION_H

#include <QApplication>
#include "LanguageManager.h"
#include "ThemeManager.h"

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    static Application *instance();
    ThemeManager *themeManager() const;
    LanguageManager *languageManager() const;

signals:
    // macOS：Finder 双击/「打开方式」的文件经 Apple Event → QEvent::FileOpen 到达
    //（不走 argv——argv 只覆盖 Linux .desktop %f / Windows 场景），
    // 由此信号转发给 MainWindow 按既有路径打开
    void fileOpenRequested(const QString &path);

protected:
    bool event(QEvent *e) override;

private:
    ThemeManager *m_themeManager;
    LanguageManager *m_languageManager;
};

#endif // MD_EDITOR_APPLICATION_H
