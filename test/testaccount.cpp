#include <QTest>

#include "account.h"


QSharedPointer<OCC::Account> createAccount()
{
    auto account = OCC::Account::create();
    return account;
}

class TestAccount : public QObject
{
    Q_OBJECT

private slots:
    void testSample()
    {
        createAccount();
        Q_ASSERT(true);
    }
};

QTEST_GUILESS_MAIN(TestAccount)
#include "testaccount.moc"
