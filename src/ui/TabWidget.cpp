#include "TabWidget.h"
#include "EditorTab.h"
#include "../core/EditorBridge.h"
#include <QTabBar>
#include <QStyle>
#include <QMessageBox>
#include <QAbstractButton>
#include <QFileInfo>

TabWidget::TabWidget(QWidget *parent)
    : QTabWidget(parent), m_nextTabId(1)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    tabBar()->setDrawBase(false);
    tabBar()->setStyleSheet("QTabBar { border: none; background: transparent; }"
                            "QTabBar::tab { border: none; }");
    setStyleSheet("QTabWidget::pane { border: none; }");
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &TabWidget::onCurrentChanged);
    // 双击标签页关闭（复用 onTabCloseRequested 的脏检查/保存流程）
    connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, &TabWidget::onTabCloseRequested);
}

EditorTab *TabWidget::openNewTab()
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);
    m_tabs.insert(tabId, tab);
    int index = addTab(tab, generateUntitledName());
    m_untitledTabs.insert(tab);   // 先取名再登记，保证首个未命名编号从 1 开始
    setCurrentIndex(index);
    connect(tab->bridge(), &EditorBridge::dirtyStateChanged, this,
            [this, tab](bool dirty, int) {
                int idx = indexOf(tab);
                if (idx >= 0) setTabDirty(idx, dirty);
            });
    emit editorBridgeCreated(tab->bridge());
    return tab;
}

EditorTab *TabWidget::openFileTab(const QString &path, const QString &content, bool lazy)
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);
    m_tabs.insert(tabId, tab);
    QFileInfo fi(path);
    int index = addTab(tab, fi.fileName());
    setCurrentIndex(index);
    setTabToolTip(index, path);
    if (lazy) {
        // 惰性加载：页面就绪且本 tab 已是当前标签页时才加载登记的待载内容
        // （内容随后由调用方 setPendingContent 登记；激活切换在 onCurrentChanged flush）
        connect(tab, &EditorTab::pageLoadFinished, this,
                [this, tab](bool ok) {
                    if (ok && indexOf(tab) == currentIndex())
                        tab->flushPendingContent();
                });
    } else {
        connect(tab, &EditorTab::pageLoadFinished, this,
                [tab, content](bool ok) { if (ok) tab->loadFile("", content); });
    }
    connect(tab->bridge(), &EditorBridge::dirtyStateChanged, this,
            [this, tab](bool dirty, int) {
                int idx = indexOf(tab);
                if (idx >= 0) setTabDirty(idx, dirty);
            });
    emit editorBridgeCreated(tab->bridge());
    return tab;
}

EditorTab *TabWidget::openHelpTab(const QString &title, const QString &mdContent)
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);
    m_tabs.insert(tabId, tab);
    int index = addTab(tab, title);
    setCurrentIndex(index);
    // 不设 tooltip（非文件）
    connect(tab, &EditorTab::pageLoadFinished, this,
            [tab, mdContent](bool ok) { if (ok) tab->loadFile("", mdContent); });
    // 不连接 dirtyStateChanged（帮助页始终只读）
    emit editorBridgeCreated(tab->bridge());
    return tab;
}

EditorTab *TabWidget::currentEditorTab() const
{
    return qobject_cast<EditorTab *>(currentWidget());
}

EditorTab *TabWidget::tabAt(int index) const
{
    return qobject_cast<EditorTab *>(widget(index));
}

void TabWidget::setTabDirty(int index, bool dirty)
{
    QString title = tabText(index);
    if (dirty && !title.startsWith("● "))
        setTabText(index, "● " + title);
    else if (!dirty && title.startsWith("● "))
        setTabText(index, title.mid(2));
}

void TabWidget::markTabSaved(int index, const QString &filePath)
{
    m_untitledTabs.remove(tabAt(index));   // 保存后不再是未命名 tab
    QFileInfo fi(filePath);
    setTabText(index, fi.fileName());
    setTabToolTip(index, filePath);
}

void TabWidget::onTabCloseRequested(int index)
{
    EditorTab *tab = tabAt(index);
    if (!tab) return;
    bool isDirty = tabText(index).startsWith("● ");
    if (isDirty) {
        QMessageBox box(QMessageBox::Question, tr("未保存的更改"),
            tr("文件 '%1' 有未保存的更改。\n是否保存？").arg(tabText(index)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.button(QMessageBox::Save)->setText(tr("保存"));
        box.button(QMessageBox::Discard)->setText(tr("不保存并关闭"));
        box.button(QMessageBox::Cancel)->setText(tr("取消"));
        auto r = box.exec();
        if (r == QMessageBox::Cancel) return;
        if (r == QMessageBox::Save) {
            // 记 tab 指针而非下标：保存窗口期（异步 JS 往返期间 UI 可交互）
            // 用户拖动标签或开关其他 tab 会使下标漂移，按陈旧下标会关错页
            m_pendingCloseTab = tab;
            emit tabSaveRequested(index);
            return; // 保存由 MainWindow 异步处理，完成后调用 completePendingTabClose()
        }
    }
    emit tabClosed(index);
    m_tabs.remove(tab->bridge()->tabId());
    m_untitledTabs.remove(tab);
    removeTab(index);
    tab->deleteLater();
}

void TabWidget::completePendingTabClose()
{
    EditorTab *tab = m_pendingCloseTab;
    m_pendingCloseTab = nullptr;
    if (!tab)
        return;
    const int idx = indexOf(tab);
    if (idx < 0)
        return;   // 保存期间该 tab 已被其他路径关闭
    emit tabClosed(idx);
    m_tabs.remove(tab->bridge()->tabId());
    m_untitledTabs.remove(tab);
    removeTab(idx);
    tab->deleteLater();
}

void TabWidget::onCurrentChanged(int index)
{
    if (index >= 0) {
        EditorTab *tab = tabAt(index);
        if (tab) {
            // 惰性加载 flush：切换到尚未加载内容的标签页时在此加载
            tab->flushPendingContent();
            emit currentTabChanged(tab->bridge()->tabId());
        }
    }
}

QString TabWidget::generateUntitledName()
{
    return tr("未命名-%1").arg(m_untitledTabs.size() + 1);
}

// 语言切换后重命名未命名 tab；保留「● 」脏标记前缀
void TabWidget::retranslateUi()
{
    int n = 1;
    for (int i = 0; i < count(); ++i) {
        EditorTab *tab = tabAt(i);
        if (m_untitledTabs.contains(tab)) {
            QString t = tr("未命名-%1").arg(n++);
            if (tabText(i).startsWith("● "))
                t = "● " + t;
            setTabText(i, t);
        }
    }
}
