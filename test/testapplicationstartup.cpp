/*
 * SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"
#include "theme.h"

#include <QDir>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

class TestApplicationStartup : public QObject
{
    Q_OBJECT

    static void startClient(QProcess &client, const QString &configDir, const bool activated,
        const QStringList &options = {}, const bool waitForTray = false)
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("DBUS_STARTER_ADDRESS");
        environment.remove("DBUS_STARTER_BUS_TYPE");
        if (activated) {
            environment.insert("DBUS_STARTER_ADDRESS", environment.value("DBUS_SESSION_BUS_ADDRESS"));
            environment.insert("DBUS_STARTER_BUS_TYPE", "session");
        }
        environment.insert("XDG_CONFIG_HOME", configDir);
        environment.insert("XDG_CACHE_HOME", configDir + QStringLiteral("/cache"));
        environment.insert("XDG_DATA_HOME", configDir + QStringLiteral("/data"));
        environment.insert("XDG_RUNTIME_DIR", configDir);
        environment.insert("XDG_CURRENT_DESKTOP", "XFCE");
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        environment.insert("QT_QPA_PLATFORMTHEME", waitForTray ? "" : "appmenu-qt5");
        client.setProcessEnvironment(environment);
        client.setProcessChannelMode(QProcess::MergedChannels);
        client.start(QStringLiteral(OWNCLOUD_BIN_PATH "/" APPLICATION_EXECUTABLE),
            QStringList{QStringLiteral("--confdir"), configDir,
                QStringLiteral("--logfile"), QStringLiteral("-"), QStringLiteral("--logflush")} + options);
    }

    static void stopClient(QProcess &client)
    {
        if (client.state() != QProcess::NotRunning) {
            client.kill();
            client.waitForFinished();
        }
    }

    static QString configFile(const QTemporaryDir &directory)
    {
        return QDir(directory.path()).filePath(OCC::Theme::instance()->configFileName());
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(!qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"), "Run with dbus-run-session");
    }

    void unconfiguredActivation_data()
    {
        QTest::addColumn<bool>("hasPreferences");
        QTest::addColumn<bool>("background");
        QTest::newRow("fresh-install") << false << true;
        QTest::newRow("preferences-only") << true << true;
        QTest::newRow("activation-without-background") << false << false;
    }

    void unconfiguredActivation()
    {
        QFETCH(bool, hasPreferences);
        QFETCH(bool, background);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        if (hasPreferences) {
            QSettings settings(configFile(directory), QSettings::IniFormat);
            settings.setValue(QStringLiteral("General/monoIcons"), true);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        QProcess client;
        const auto cleanup = qScopeGuard([&] { stopClient(client); });
        startClient(client, directory.path(), true,
            background ? QStringList{QStringLiteral("--background")} : QStringList{}, true);
        QVERIFY(client.waitForStarted());
        // This must finish before XFCE's 30-second tray wait, without entering the GUI event loop.
        QVERIFY2(client.waitForFinished(10000), client.readAll().constData());
        QCOMPARE(client.exitStatus(), QProcess::NormalExit);
        QCOMPARE(client.exitCode(), 1);
    }

    void normalStartup_data()
    {
        QTest::addColumn<bool>("activated");
        QTest::addColumn<bool>("configured");
        QTest::addColumn<bool>("background");
        QTest::newRow("manual-first-run") << false << false << false;
        QTest::newRow("manual-background-first-run") << false << false << true;
        QTest::newRow("configured-activation") << true << true << true;
    }

    void normalStartup()
    {
        QFETCH(bool, activated);
        QFETCH(bool, configured);
        QFETCH(bool, background);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        if (configured) {
            QSettings settings(configFile(directory), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Accounts/version"), 13);
            settings.setValue(QStringLiteral("Accounts/test/version"), 13);
            settings.setValue(QStringLiteral("Accounts/test/url"), QStringLiteral("http://127.0.0.1:1"));
            settings.setValue(QStringLiteral("Accounts/test/authType"), QStringLiteral("webflow"));
            settings.setValue(QStringLiteral("Accounts/test/webflow_user"), QStringLiteral("alice"));
            settings.setValue(QStringLiteral("Accounts/test/dav_user"), QStringLiteral("alice"));
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        QProcess client;
        const auto cleanup = qScopeGuard([&] { stopClient(client); });
        startClient(client, directory.path(), activated,
            background ? QStringList{QStringLiteral("--background")} : QStringList{});
        QVERIFY(client.waitForStarted());
        QVERIFY2(!client.waitForFinished(3000), client.readAll().constData());
        QCOMPARE(client.state(), QProcess::Running);
    }

    void activationAllowsProvisioning_data()
    {
        QTest::addColumn<bool>("loginUri");
        QTest::addColumn<bool>("password");
        QTest::newRow("command-line-without-password") << false << false;
        QTest::newRow("command-line-with-password") << false << true;
        QTest::newRow("login-uri") << true << false;
    }

    void activationAllowsProvisioning()
    {
        QFETCH(bool, loginUri);
        QFETCH(bool, password);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QProcess client;
        const auto cleanup = qScopeGuard([&] { stopClient(client); });
        const auto serverUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        auto options = loginUri
            ? QStringList{QStringLiteral(APPLICATION_URI_HANDLER_SCHEME "://login/server:") + serverUrl}
            : QStringList{QStringLiteral("--background"), QStringLiteral("--userid"), QStringLiteral("alice"),
                QStringLiteral("--serverurl"), serverUrl, QStringLiteral("--localdirpath"),
                directory.path() + QStringLiteral("/sync")};
        if (password) {
            options << QStringLiteral("--apppassword") << QStringLiteral("test-password");
        }
        startClient(client, directory.path(), true, options);
        QVERIFY(client.waitForStarted());
        if (!loginUri && !password) {
            QVERIFY2(client.waitForFinished(10000), client.readAll().constData());
            QCOMPARE(client.exitStatus(), QProcess::NormalExit);
            QVERIFY2(client.exitCode() == 0, client.readAll().constData());
            QSettings settings(configFile(directory), QSettings::IniFormat);
            settings.beginGroup(QStringLiteral("Accounts"));
            const auto accounts = settings.childGroups();
            QCOMPARE(accounts.size(), 1);
            settings.beginGroup(accounts.first());
            QCOMPARE(settings.value(QStringLiteral("url")).toString(), serverUrl);
            QCOMPARE(settings.value(QStringLiteral("webflow_user")).toString(), QStringLiteral("alice"));
            return;
        }
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 10000);
        QCOMPARE(client.state(), QProcess::Running);
    }
};

QTEST_GUILESS_MAIN(TestApplicationStartup)
#include "testapplicationstartup.moc"
