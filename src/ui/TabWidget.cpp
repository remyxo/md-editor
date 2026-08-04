#include "TabWidget.h"
#include "EditorTab.h"
#include "../core/EditorBridge.h"
#include <QTabBar>
#include <QMessageBox>
#include <QFileInfo>

TabWidget::TabWidget(QWidget *parent)
    : QTabWidget(parent), m_nextTabId(1)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &TabWidget::onCurrentChanged);
}

EditorTab *TabWidget::openNewTab()
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);
    m_tabs.insert(tabId, tab);
    int index = addTab(tab, generateUntitledName());
    setCurrentIndex(index);
    connect(tab->bridge(), &EditorBridge::dirtyStateChanged, this,
            [this, tab](bool dirty, int) {
                int idx = indexOf(tab);
                if (idx >= 0) setTabDirty(idx, dirty);
            });
    emit editorBridgeCreated(tab->bridge());
    return tab;
}

EditorTab *TabWidget::openFileTab(const QString &path, const QString &content)
{
    int tabId = m_nextTabId++;
    EditorTab *tab = new EditorTab(tabId, this);
    m_tabs.insert(tabId, tab);
    QFileInfo fi(path);
    int index = addTab(tab, fi.fileName());
    setCurrentIndex(index);
    setTabToolTip(index, path);
    connect(tab, &EditorTab::pageLoadFinished, this,
            [tab, content](bool ok) { if (ok) tab->loadFile("", content); });
    connect(tab->bridge(), &EditorBridge::dirtyStateChanged, this,
            [this, tab](bool dirty, int) {
                int idx = indexOf(tab);
                if (idx >= 0) setTabDirty(idx, dirty);
            });
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

void TabWidget::setTabTitle(int index, const QString &title) { setTabText(index, title); }

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
        auto r = QMessageBox::question(this, "未保存的更改",
            QString("文件 '%1' 有未保存的更改。\n是否保存？").arg(tabText(index)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return;
        if (r == QMessageBox::Save) {
            emit tabSaveRequested(index);
            return; // 保存由 MainWindow 异步处理，本次不关闭
        }
    }
    m_tabs.remove(tab->bridge()->tabId());
    removeTab(index);
    tab->deleteLater();
}

void TabWidget::onCurrentChanged(int index)
{
    if (index >= 0) {
        EditorTab *tab = tabAt(index);
        if (tab) emit currentTabChanged(tab->bridge()->tabId());
    }
}

QString TabWidget::generateUntitledName()
{
    return QString("未命名-%1").arg(m_nextTabId);
}
