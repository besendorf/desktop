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

using namespace Qt::StringLiterals;

class TestApplicationStartup : public QObject
{
    Q_OBJECT

    static constexpr auto _startupTimeoutMs = 10000;
    static constexpr auto _startupObservationMs = 3000;
    static constexpr auto _accountSettingsVersion = 13;

    static void startClient(QProcess &client, const QString &configDir, const bool activated, const QStringList &options = {}, const bool waitForTray = false)
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("DBUS_STARTER_ADDRESS");
        environment.remove("DBUS_STARTER_BUS_TYPE");
        if (activated) {
            environment.insert("DBUS_STARTER_ADDRESS", environment.value("DBUS_SESSION_BUS_ADDRESS"));
            environment.insert("DBUS_STARTER_BUS_TYPE", "session");
        }
        environment.insert("XDG_CONFIG_HOME", configDir);
        environment.insert("XDG_CACHE_HOME", configDir + u"/cache"_s);
        environment.insert("XDG_DATA_HOME", configDir + u"/data"_s);
        environment.insert("XDG_RUNTIME_DIR", configDir);
        environment.insert("XDG_CURRENT_DESKTOP", "XFCE");
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        environment.insert("QT_QPA_PLATFORMTHEME", waitForTray ? "" : "appmenu-qt5");
        client.setProcessEnvironment(environment);
        client.setProcessChannelMode(QProcess::MergedChannels);
        client.start(QString::fromUtf8(OWNCLOUD_BIN_PATH "/" APPLICATION_EXECUTABLE),
                     QStringList{u"--confdir"_s, configDir, u"--logfile"_s, u"-"_s, u"--logflush"_s} + options);
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

    void testStartup_unconfiguredActivation_exitsWithoutGui_data()
    {
        QTest::addColumn<bool>("hasPreferences");
        QTest::addColumn<bool>("background");
        QTest::newRow("fresh-install") << false << true;
        QTest::newRow("preferences-only") << true << true;
        QTest::newRow("activation-without-background") << false << false;
    }

    void testStartup_unconfiguredActivation_exitsWithoutGui()
    {
        QFETCH(bool, hasPreferences);
        QFETCH(bool, background);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        if (hasPreferences) {
            QSettings settings(configFile(directory), QSettings::IniFormat);
            settings.setValue(u"General/monoIcons"_s, true);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        QProcess client;
        const auto cleanup = qScopeGuard([&] {
            stopClient(client);
        });
        startClient(client, directory.path(), true, background ? QStringList{u"--background"_s} : QStringList{}, true);
        QVERIFY(client.waitForStarted());
        // This must finish before XFCE's 30-second tray wait, without entering the GUI event loop.
        QVERIFY2(client.waitForFinished(_startupTimeoutMs), client.readAll().constData());
        QCOMPARE(client.exitStatus(), QProcess::NormalExit);
        QCOMPARE(client.exitCode(), 1);
    }

    void testStartup_manualOrConfigured_keepsRunning_data()
    {
        QTest::addColumn<bool>("activated");
        QTest::addColumn<bool>("configured");
        QTest::addColumn<bool>("background");
        QTest::newRow("manual-first-run") << false << false << false;
        QTest::newRow("manual-background-first-run") << false << false << true;
        QTest::newRow("configured-activation") << true << true << true;
    }

    void testStartup_manualOrConfigured_keepsRunning()
    {
        QFETCH(bool, activated);
        QFETCH(bool, configured);
        QFETCH(bool, background);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        if (configured) {
            QSettings settings(configFile(directory), QSettings::IniFormat);
            settings.setValue(u"Accounts/version"_s, _accountSettingsVersion);
            settings.setValue(u"Accounts/test/version"_s, _accountSettingsVersion);
            settings.setValue(u"Accounts/test/url"_s, u"http://127.0.0.1:1"_s);
            settings.setValue(u"Accounts/test/authType"_s, u"webflow"_s);
            settings.setValue(u"Accounts/test/webflow_user"_s, u"alice"_s);
            settings.setValue(u"Accounts/test/dav_user"_s, u"alice"_s);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        QProcess client;
        const auto cleanup = qScopeGuard([&] {
            stopClient(client);
        });
        startClient(client, directory.path(), activated, background ? QStringList{u"--background"_s} : QStringList{});
        QVERIFY(client.waitForStarted());
        QVERIFY2(!client.waitForFinished(_startupObservationMs), client.readAll().constData());
        QCOMPARE(client.state(), QProcess::Running);
    }

    void testStartup_provisioning_isAllowed_data()
    {
        QTest::addColumn<bool>("loginUri");
        QTest::addColumn<bool>("password");
        QTest::newRow("command-line-without-password") << false << false;
        QTest::newRow("command-line-with-password") << false << true;
        QTest::newRow("login-uri") << true << false;
    }

    void testStartup_provisioning_isAllowed()
    {
        QFETCH(bool, loginUri);
        QFETCH(bool, password);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QProcess client;
        const auto cleanup = qScopeGuard([&] {
            stopClient(client);
        });
        const auto serverUrl = u"http://127.0.0.1:%1"_s.arg(server.serverPort());
        auto options = loginUri
            ? QStringList{QString::fromLatin1(APPLICATION_URI_HANDLER_SCHEME "://login/server:") + serverUrl}
            : QStringList{u"--background"_s, u"--userid"_s, u"alice"_s, u"--serverurl"_s, serverUrl, u"--localdirpath"_s, directory.path() + u"/sync"_s};
        if (password) {
            options << u"--apppassword"_s << u"test-password"_s;
        }
        startClient(client, directory.path(), true, options);
        QVERIFY(client.waitForStarted());
        if (!loginUri && !password) {
            QVERIFY2(client.waitForFinished(_startupTimeoutMs), client.readAll().constData());
            QCOMPARE(client.exitStatus(), QProcess::NormalExit);
            QVERIFY2(client.exitCode() == 0, client.readAll().constData());
            QSettings settings(configFile(directory), QSettings::IniFormat);
            settings.beginGroup(u"Accounts"_s);
            const auto accounts = settings.childGroups();
            QCOMPARE(accounts.size(), 1);
            settings.beginGroup(accounts.first());
            QCOMPARE(settings.value(u"url"_s).toString(), serverUrl);
            QCOMPARE(settings.value(u"webflow_user"_s).toString(), u"alice"_s);
            return;
        }
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), _startupTimeoutMs);
        QCOMPARE(client.state(), QProcess::Running);
    }
};

QTEST_GUILESS_MAIN(TestApplicationStartup)
#include "testapplicationstartup.moc"
