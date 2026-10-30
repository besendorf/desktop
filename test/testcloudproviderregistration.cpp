/*
 * SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "account.h"
#include "accountmanager.h"
#include "cloudproviderconfig.h"
#include "cloudproviders/cloudprovidermanager.h"
#include "configfile.h"
#include "syncenginetestutils.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace OCC;

class TestCloudProviderRegistration : public QObject
{
    Q_OBJECT

    QTemporaryDir _config;

    static bool isRegistered()
    {
        return QDBusConnection::sessionBus().interface()->isServiceRegistered(
            QStringLiteral(LIBCLOUDPROVIDERS_DBUS_BUS_NAME)).value();
    }

    static void addAccount(const QString &user)
    {
        auto account = Account::create();
        account->setUrl(QUrl(QStringLiteral("http://127.0.0.1:1")));
        account->setDavUser(user);
        account->setCredentials(new FakeCredentials(new FakeQNAM({})));
        AccountManager::instance()->addAccount(account);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(_config.isValid());
        QVERIFY(ConfigFile::setConfDir(_config.path()));
        QVERIFY(QDBusConnection::sessionBus().isConnected());
    }

    void cleanup()
    {
        AccountManager::instance()->shutdown();
    }

    void registrationWaitsForFirstAccount()
    {
        auto provider = std::make_unique<CloudProviderManager>();
        QTest::qWait(100);
        QVERIFY(!isRegistered());

        addAccount(QStringLiteral("alice"));
        QTRY_VERIFY(isRegistered());

        addAccount(QStringLiteral("bob"));
        QVERIFY(isRegistered());

        provider.reset();
        QTRY_VERIFY(!isRegistered());
    }

    void existingAccountRegistersProvider()
    {
        addAccount(QStringLiteral("alice"));
        auto provider = std::make_unique<CloudProviderManager>();
        QTRY_VERIFY(isRegistered());

        provider.reset();
        QTRY_VERIFY(!isRegistered());
    }
};

QTEST_GUILESS_MAIN(TestCloudProviderRegistration)
#include "testcloudproviderregistration.moc"
