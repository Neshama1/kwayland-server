/********************************************************************
Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) version 3, or any
later version accepted by the membership of KDE e.V. (or its
successor approved by the membership of KDE e.V.), which shall
act as a proxy defined in Section 6 of version 3 of the license.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
// Qt
#include <QtTest/QtTest>
// KWin
#include "../../src/client/connection_thread.h"
#include "../../src/client/output.h"
#include "../../src/client/registry.h"
#include "../../src/server/display.h"
#include "../../src/server/output_interface.h"
// Wayland
#include <wayland-client-protocol.h>

class TestWaylandOutput : public QObject
{
    Q_OBJECT
public:
    explicit TestWaylandOutput(QObject *parent = nullptr);
private Q_SLOTS:
    void init();
    void cleanup();

    void testRegistry();
    void testModeChanges();
    void testScaleChange();

    void testSubPixel_data();
    void testSubPixel();

    void testTransform_data();
    void testTransform();

private:
    KWayland::Server::Display *m_display;
    KWayland::Server::OutputInterface *m_serverOutput;
    KWayland::Client::ConnectionThread *m_connection;
    QThread *m_thread;
};

static const QString s_socketName = QStringLiteral("kwin-test-wayland-output-0");

TestWaylandOutput::TestWaylandOutput(QObject *parent)
    : QObject(parent)
    , m_display(nullptr)
    , m_serverOutput(nullptr)
    , m_connection(nullptr)
    , m_thread(nullptr)
{
}

void TestWaylandOutput::init()
{
    using namespace KWayland::Server;
    delete m_display;
    m_display = new Display(this);
    m_display->setSocketName(s_socketName);
    m_display->start();
    QVERIFY(m_display->isRunning());

    m_serverOutput = m_display->createOutput(this);
    m_serverOutput->addMode(QSize(800, 600));
    m_serverOutput->addMode(QSize(1024, 768));
    m_serverOutput->addMode(QSize(1280, 1024));
    m_serverOutput->setCurrentMode(QSize(1024, 768));
    m_serverOutput->create();

    // setup connection
    m_connection = new KWayland::Client::ConnectionThread;
    QSignalSpy connectedSpy(m_connection, SIGNAL(connected()));
    m_connection->setSocketName(s_socketName);

    m_thread = new QThread(this);
    m_connection->moveToThread(m_thread);
    m_thread->start();

    m_connection->initConnection();
    QVERIFY(connectedSpy.wait());
}

void TestWaylandOutput::cleanup()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    delete m_connection;
    m_connection = nullptr;

    delete m_serverOutput;
    m_serverOutput = nullptr;

    delete m_display;
    m_display = nullptr;
}

void TestWaylandOutput::testRegistry()
{
    m_serverOutput->setGlobalPosition(QPoint(100, 50));
    m_serverOutput->setPhysicalSize(QSize(200, 100));

    KWayland::Client::Registry registry;
    QSignalSpy announced(&registry, SIGNAL(outputAnnounced(quint32,quint32)));
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();
    wl_display_flush(m_connection->display());
    QVERIFY(announced.wait());

    KWayland::Client::Output output;
    QVERIFY(!output.isValid());
    QCOMPARE(output.geometry(), QRect());
    QCOMPARE(output.globalPosition(), QPoint());
    QCOMPARE(output.manufacturer(), QString());
    QCOMPARE(output.model(), QString());
    QCOMPARE(output.physicalSize(), QSize());
    QCOMPARE(output.pixelSize(), QSize());
    QCOMPARE(output.refreshRate(), 0);
    QCOMPARE(output.scale(), 1);
    QCOMPARE(output.subPixel(), KWayland::Client::Output::SubPixel::Unknown);
    QCOMPARE(output.transform(), KWayland::Client::Output::Transform::Normal);

    QSignalSpy outputChanged(&output, SIGNAL(changed()));
    QVERIFY(outputChanged.isValid());

    output.setup(registry.bindOutput(announced.first().first().value<quint32>(), announced.first().last().value<quint32>()));
    wl_display_flush(m_connection->display());
    QVERIFY(outputChanged.wait());

    QCOMPARE(output.geometry(), QRect(100, 50, 1024, 768));
    QCOMPARE(output.globalPosition(), QPoint(100, 50));
    QCOMPARE(output.manufacturer(), QStringLiteral("org.kde.kwin"));
    QCOMPARE(output.model(), QStringLiteral("none"));
    QCOMPARE(output.physicalSize(), QSize(200, 100));
    QCOMPARE(output.pixelSize(), QSize(1024, 768));
    QCOMPARE(output.refreshRate(), 60000);
    QCOMPARE(output.scale(), 1);
    // for xwayland output it's unknown
    QCOMPARE(output.subPixel(), KWayland::Client::Output::SubPixel::Unknown);
    // for xwayland transform is normal
    QCOMPARE(output.transform(), KWayland::Client::Output::Transform::Normal);
}

void TestWaylandOutput::testModeChanges()
{
    KWayland::Client::Registry registry;
    QSignalSpy announced(&registry, SIGNAL(outputAnnounced(quint32,quint32)));
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();
    wl_display_flush(m_connection->display());
    QVERIFY(announced.wait());

    KWayland::Client::Output output;
    QSignalSpy outputChanged(&output, SIGNAL(changed()));
    QVERIFY(outputChanged.isValid());
    output.setup(registry.bindOutput(announced.first().first().value<quint32>(), announced.first().last().value<quint32>()));
    wl_display_flush(m_connection->display());
    QVERIFY(outputChanged.wait());

    QCOMPARE(output.pixelSize(), QSize(1024, 768));

    // change the current mode
    outputChanged.clear();
    m_serverOutput->setCurrentMode(QSize(800, 600));
    QVERIFY(outputChanged.wait());
    QCOMPARE(output.pixelSize(), QSize(800, 600));

    // change once more
    outputChanged.clear();
    m_serverOutput->setCurrentMode(QSize(1280, 1024));
    QVERIFY(outputChanged.wait());
    QCOMPARE(output.pixelSize(), QSize(1280, 1024));
}

void TestWaylandOutput::testScaleChange()
{
    KWayland::Client::Registry registry;
    QSignalSpy announced(&registry, SIGNAL(outputAnnounced(quint32,quint32)));
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();
    wl_display_flush(m_connection->display());
    QVERIFY(announced.wait());

    KWayland::Client::Output output;
    QSignalSpy outputChanged(&output, SIGNAL(changed()));
    QVERIFY(outputChanged.isValid());
    output.setup(registry.bindOutput(announced.first().first().value<quint32>(), announced.first().last().value<quint32>()));
    wl_display_flush(m_connection->display());
    QVERIFY(outputChanged.wait());
    QCOMPARE(output.scale(), 1);

    // change the scale
    outputChanged.clear();
    m_serverOutput->setScale(2);
    QVERIFY(outputChanged.wait());
    QCOMPARE(output.scale(), 2);

    // change once more
    outputChanged.clear();
    m_serverOutput->setScale(4);
    QVERIFY(outputChanged.wait());
    QCOMPARE(output.scale(), 4);
}

void TestWaylandOutput::testSubPixel_data()
{
    using namespace KWayland::Client;
    using namespace KWayland::Server;
    QTest::addColumn<KWayland::Client::Output::SubPixel>("expected");
    QTest::addColumn<KWayland::Server::OutputInterface::SubPixel>("actual");

    QTest::newRow("none") << Output::SubPixel::None << OutputInterface::SubPixel::None;
    QTest::newRow("horizontal/rgb") << Output::SubPixel::HorizontalRGB << OutputInterface::SubPixel::HorizontalRGB;
    QTest::newRow("horizontal/bgr") << Output::SubPixel::HorizontalBGR << OutputInterface::SubPixel::HorizontalBGR;
    QTest::newRow("vertical/rgb") << Output::SubPixel::VerticalRGB << OutputInterface::SubPixel::VerticalRGB;
    QTest::newRow("vertical/bgr") << Output::SubPixel::VerticalBGR << OutputInterface::SubPixel::VerticalBGR;
}

void TestWaylandOutput::testSubPixel()
{
    using namespace KWayland::Client;
    using namespace KWayland::Server;
    QFETCH(OutputInterface::SubPixel, actual);
    m_serverOutput->setSubPixel(actual);

    KWayland::Client::Registry registry;
    QSignalSpy announced(&registry, SIGNAL(outputAnnounced(quint32,quint32)));
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();
    wl_display_flush(m_connection->display());
    QVERIFY(announced.wait());

    KWayland::Client::Output output;
    QSignalSpy outputChanged(&output, SIGNAL(changed()));
    QVERIFY(outputChanged.isValid());
    output.setup(registry.bindOutput(announced.first().first().value<quint32>(), announced.first().last().value<quint32>()));
    wl_display_flush(m_connection->display());
    if (outputChanged.isEmpty()) {
        QVERIFY(outputChanged.wait());
    }

    QTEST(output.subPixel(), "expected");

    // change back to unknown
    outputChanged.clear();
    m_serverOutput->setSubPixel(OutputInterface::SubPixel::Unknown);
    if (outputChanged.isEmpty()) {
        QVERIFY(outputChanged.wait());
    }
    QCOMPARE(output.subPixel(), Output::SubPixel::Unknown);
}

void TestWaylandOutput::testTransform_data()
{
    using namespace KWayland::Client;
    using namespace KWayland::Server;
    QTest::addColumn<KWayland::Client::Output::Transform>("expected");
    QTest::addColumn<KWayland::Server::OutputInterface::Transform>("actual");

    QTest::newRow("90")          << Output::Transform::Rotated90  << OutputInterface::Transform::Rotated90;
    QTest::newRow("180")         << Output::Transform::Rotated180 << OutputInterface::Transform::Rotated180;
    QTest::newRow("270")         << Output::Transform::Rotated270 << OutputInterface::Transform::Rotated270;
    QTest::newRow("Flipped")     << Output::Transform::Flipped    << OutputInterface::Transform::Flipped;
    QTest::newRow("Flipped 90")  << Output::Transform::Flipped90  << OutputInterface::Transform::Flipped90;
    QTest::newRow("Flipped 180") << Output::Transform::Flipped180 << OutputInterface::Transform::Flipped180;
    QTest::newRow("Flipped 280") << Output::Transform::Flipped270 << OutputInterface::Transform::Flipped270;
}

void TestWaylandOutput::testTransform()
{
    using namespace KWayland::Client;
    using namespace KWayland::Server;
    QFETCH(OutputInterface::Transform, actual);
    m_serverOutput->setTransform(actual);

    KWayland::Client::Registry registry;
    QSignalSpy announced(&registry, SIGNAL(outputAnnounced(quint32,quint32)));
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();
    wl_display_flush(m_connection->display());
    QVERIFY(announced.wait());

    KWayland::Client::Output output;
    QSignalSpy outputChanged(&output, SIGNAL(changed()));
    QVERIFY(outputChanged.isValid());
    output.setup(registry.bindOutput(announced.first().first().value<quint32>(), announced.first().last().value<quint32>()));
    wl_display_flush(m_connection->display());
    if (outputChanged.isEmpty()) {
        QVERIFY(outputChanged.wait());
    }

    QTEST(output.transform(), "expected");

    // change back to normal
    outputChanged.clear();
    m_serverOutput->setTransform(OutputInterface::Transform::Normal);
    if (outputChanged.isEmpty()) {
        QVERIFY(outputChanged.wait());
    }
    QCOMPARE(output.transform(), Output::Transform::Normal);
}

QTEST_MAIN(TestWaylandOutput)
#include "test_wayland_output.moc"