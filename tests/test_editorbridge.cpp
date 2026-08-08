#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include "../src/core/EditorBridge.h"

class TestEditorBridge : public QObject {
    Q_OBJECT
private slots:
    void testCursorChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::cursorPositionChanged);

        bridge.cursorChanged(10, 5);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toInt(), 10);  // line
        QCOMPARE(spy[0][1].toInt(), 5);   // col
        QCOMPARE(spy[0][2].toInt(), 1);   // tabId
    }

    void testContentChanged() {
        EditorBridge bridge(2);
        QSignalSpy spy(&bridge, &EditorBridge::dirtyStateChanged);

        bridge.contentChanged(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toBool(), true);
        QCOMPARE(spy[0][1].toInt(), 2);
    }

    void testHeadingsChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::headingsUpdated);

        QString json = R"([{"level":1,"text":"标题一","pos":0},{"level":2,"text":"小节","pos":10}])";
        bridge.headingsChanged(json);

        QCOMPARE(spy.count(), 1);
        QJsonArray arr = spy[0][0].value<QJsonArray>();
        QCOMPARE(arr.size(), 2);
        QCOMPARE(arr[0].toObject()["text"].toString(), QString("标题一"));
    }

    void testInvalidJson() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::headingsUpdated);

        bridge.headingsChanged("not valid json");

        QCOMPARE(spy.count(), 0);  // 无效 JSON 不触发信号
    }

    void testHistoryChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::historyStateChanged);

        bridge.historyChanged(true, false);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toBool(), true);   // canUndo
        QCOMPARE(spy[0][1].toBool(), false);  // canRedo
        QCOMPARE(spy[0][2].toInt(), 1);       // tabId

        bridge.historyChanged(false, true);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy[1][0].toBool(), false);
        QCOMPARE(spy[1][1].toBool(), true);
    }

    void testWordCountChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::wordCountUpdated);

        bridge.wordCountChanged(100, 20);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toInt(), 100);  // chars
        QCOMPARE(spy[0][1].toInt(), 20);   // words
        QCOMPARE(spy[0][2].toInt(), 1);    // tabId
    }

    void testFormatChanged() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::formatStateChanged);

        bridge.formatChanged("{\"bold\":true,\"italic\":false,\"heading\":2}");

        QCOMPARE(spy.count(), 1);
        QJsonObject fmt = spy[0][0].value<QJsonObject>();
        QCOMPARE(fmt["bold"].toBool(), true);
        QCOMPARE(fmt["italic"].toBool(), false);
        QCOMPARE(fmt["heading"].toInt(), 2);
    }

    void testFormatChangedInvalidJson() {
        EditorBridge bridge(1);
        QSignalSpy spy(&bridge, &EditorBridge::formatStateChanged);

        bridge.formatChanged("not valid json");

        QCOMPARE(spy.count(), 0);  // 无效 JSON 不触发信号
    }
};

QTEST_MAIN(TestEditorBridge)
#include "test_editorbridge.moc"
