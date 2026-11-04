#include "Widget.h"
#include <QtTest>
#include <QComboBox>
#include <QLineEdit>
#include <QTemporaryDir>

class WidgetTests : public QObject {
    Q_OBJECT
private slots:
    void checkboxAndQuickAdd() {
        QTemporaryDir dir; Widget widget(dir.path(), true, true); widget.show();
        auto *store = widget.store();
        QVERIFY(store->snapshot({QJsonObject{{"id", "l"}, {"title", "List"}}},
            {QJsonObject{{"id", "t"}, {"list", "l"}, {"title", "Task"}, {"status", "needsAction"}}}));
        auto columns = widget.findChildren<Column *>(); QCOMPARE(columns.size(), 2);
        QCOMPARE(columns[0]->count(), 1); QCOMPARE(columns[1]->count(), 0);
        columns[0]->item(0)->setCheckState(Qt::Checked);
        QTRY_COMPARE(columns[1]->count(), 1); QCOMPARE(columns[0]->count(), 0);
        QCOMPARE(store->outbox().size(), 1);
        columns[1]->setFocus(); columns[1]->setCurrentRow(0); QTest::keyClick(columns[1], Qt::Key_Space);
        QTRY_COMPARE(columns[0]->count(), 1);
        auto *input = widget.findChild<QLineEdit *>(); QVERIFY(input); QVERIFY(input->isEnabled());
        input->setText("Quick add"); QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(store->tasks().size(), 2); QCOMPARE(store->outbox().size(), 2); QVERIFY(input->text().isEmpty());
        QVERIFY(!widget.grab().isNull());
    }
    void filterAndDropSignal() {
        QTemporaryDir dir; Widget widget(dir.path(), true, true);
        auto *store = widget.store();
        QVERIFY(store->snapshot({QJsonObject{{"id", "a"}, {"title", "A"}}, QJsonObject{{"id", "b"}, {"title", "B"}}},
            {QJsonObject{{"id", "1"}, {"list", "a"}, {"title", "One"}, {"status", "needsAction"}},
             QJsonObject{{"id", "2"}, {"list", "b"}, {"title", "Two"}, {"status", "completed"}}}));
        auto combos = widget.findChildren<QComboBox *>(); QCOMPARE(combos.size(), 2);
        auto columns = widget.findChildren<Column *>();
        combos[0]->setCurrentIndex(combos[0]->findData("a")); QCOMPARE(columns[0]->count(), 1); QCOMPARE(columns[1]->count(), 0);
        emit columns[1]->dropped("a", "1", true); QCOMPARE(columns[0]->count(), 0); QCOMPARE(columns[1]->count(), 1);
        combos[0]->setCurrentIndex(0); QCOMPARE(columns[1]->count(), 2);
    }
};
QTEST_MAIN(WidgetTests)
#include "WidgetTests.moc"
