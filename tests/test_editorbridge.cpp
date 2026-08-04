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
};

QTEST_MAIN(TestEditorBridge)
#include "test_editorbridge.moc"
