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
using namespace Qt::StringLiterals;

class TestCloudProviderRegistration : public QObject
{
    Q_OBJECT

    QTemporaryDir _config;

    static bool isRegistered()
    {
        return QDBusConnection::sessionBus().interface()->isServiceRegistered(QString::fromLatin1(LIBCLOUDPROVIDERS_DBUS_BUS_NAME)).value();
    }

    static void addAccount(const QString &user)
    {
        auto account = Account::create();
        account->setUrl(QUrl(u"http://127.0.0.1:1"_s));
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

    void testRegistration_noAccounts_registersImmediately()
    {
        auto provider = std::make_unique<CloudProviderManager>();
        QTRY_VERIFY(isRegistered());

        addAccount(u"alice"_s);
        QTRY_VERIFY(isRegistered());

        addAccount(u"bob"_s);
        QVERIFY(isRegistered());

        provider.reset();
        QTRY_VERIFY(!isRegistered());
    }

    void testRegistration_existingAccount_registersImmediately()
    {
        addAccount(u"alice"_s);
        auto provider = std::make_unique<CloudProviderManager>();
        QTRY_VERIFY(isRegistered());

        provider.reset();
        QTRY_VERIFY(!isRegistered());
    }
};

QTEST_GUILESS_MAIN(TestCloudProviderRegistration)
#include "testcloudproviderregistration.moc"
