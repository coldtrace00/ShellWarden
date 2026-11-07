#include "QmlSafety.h"

#include <QAtomicInt>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>

namespace qmlsafety {
namespace {

QAtomicInt g_blocked;

// An address that cannot be loaded from anywhere: unknown scheme, fails at once.
QUrl refused()
{
    g_blocked.fetchAndAddRelaxed(1);
    return QUrl(QStringLiteral("blocked:"));
}

class LocalOnlyNam : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        if (allowed(request.url()))
            return QNetworkAccessManager::createRequest(op, request, data);
        QNetworkRequest blocked(request);
        blocked.setUrl(refused());   // nothing is sent anywhere
        return QNetworkAccessManager::createRequest(op, blocked, data);
    }
};

class LocalOnlyNamFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override { return new LocalOnlyNam(parent); }
};

class LocalOnlyInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    QUrl intercept(const QUrl &url, DataType) override
    {
        if (url.isEmpty() || url.isRelative() || allowed(url))
            return url;
        return refused();
    }
};

// One of each for the whole program: they hold no state and must outlive every engine.
LocalOnlyNamFactory g_namFactory;
LocalOnlyInterceptor g_interceptor;

} // namespace

bool allowed(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("qrc") || scheme == QLatin1String("data"))
        return true;
    if (scheme != QLatin1String("file"))
        return false;
    if (!url.host().isEmpty())                 // file://server/share/x → UNC on Windows
        return false;
    // file:////server/x, file:///\\server\x, file:///%5C%5Cserver… — still UNC
    const QString p = url.path(QUrl::FullyDecoded);
    if (p.startsWith(QLatin1String("//")) || p.startsWith(QLatin1String("/\\")) || p.startsWith(QLatin1Char('\\')))
        return false;
    const QString local = url.toLocalFile();
    if (local.startsWith(QLatin1String("//")) || local.startsWith(QLatin1String("\\\\")))
        return false;
    return true;
}

void install(QQmlEngine *engine)
{
    if (!engine)
        return;
    if (engine->networkAccessManagerFactory() != &g_namFactory)
        engine->setNetworkAccessManagerFactory(&g_namFactory);
    if (!engine->urlInterceptors().contains(&g_interceptor))
        engine->addUrlInterceptor(&g_interceptor);
}

int blockedCount()
{
    return g_blocked.loadRelaxed();
}

} // namespace qmlsafety
