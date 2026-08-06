#include "EditorBridge.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

EditorBridge::EditorBridge(int tabId, QObject *parent)
    : QObject(parent)
    , m_tabId(tabId)
{
}

int EditorBridge::tabId() const
{
    return m_tabId;
}

void EditorBridge::cursorChanged(int line, int col)
{
    emit cursorPositionChanged(line, col, m_tabId);
}

void EditorBridge::contentChanged(bool dirty)
{
    emit dirtyStateChanged(dirty, m_tabId);
}

void EditorBridge::headingsChanged(const QString &json)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && doc.isArray()) {
        emit headingsUpdated(doc.array(), m_tabId);
    }
}

void EditorBridge::wordCountChanged(int chars, int words)
{
    emit wordCountUpdated(chars, words, m_tabId);
}

void EditorBridge::formatChanged(const QString &json)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        emit formatStateChanged(doc.object(), m_tabId);
    }
}
