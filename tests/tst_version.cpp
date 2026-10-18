#include <QtTest>

#include "nlhc/Version.h"

class TstVersion : public QObject
{
    Q_OBJECT
private slots:
    void versionIsNotEmpty()
    {
        QVERIFY(!nlhc::versionString().isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstVersion)

#include "tst_version.moc"
