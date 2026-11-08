#include "ProxyChecker.h"
#include "core/BackgroundThreads.h"
#include "core/HttpsOverSocket.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include "core/Shadowsocks.h"

#include <thread>

namespace {

// Two independent services: if the first is blocked, the second answers.
const char *const kEndpoints[] = {
    "https://ipinfo.io/json",
    "https://api.ipify.org?format=json",
};
constexpr int kEndpointCount = 2;

} // namespace

ProxyChecker::ProxyChecker(QObject *parent) : QObject(parent) {}

// Qt's network classes do not speak Shadowsocks: ask the IP service over plain
// HTTP through our own tunnel (encrypted up to the Shadowsocks server).
void ProxyChecker::checkShadowsocks(const ProxyConfig &proxy, Callback done, int timeoutMs)
{
    QPointer<ProxyChecker> self(this);
    // tracked: on exit the program waits for it (and cuts its socket) before
    // this object goes away
    bgthreads::start([self, proxy, done, timeoutMs] {
        struct Service { const char *host, *path; };
        const Service services[] = {{"ipinfo.io", "/json"}, {"api.ipify.org", "/?format=json"}};
        QVariantMap r;
        QString lastError;
        QVariantList tried;   // {service, error} for every IP service that did not answer
        for (const Service &svc : services) {
            if (bgthreads::stopping())
                return;
            const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
            QString err;
            // HTTPS inside the tunnel: the Shadowsocks server carries the bytes but
            // cannot read or change the answer (country, city, IP)
            const sw_socket_t s = shadowsocks::open(proxy, QString::fromLatin1(svc.host), 443, timeoutMs, false, &err);
            if (s == kInvalidSocket) {
                lastError = err;
                break;   // the server itself is not reachable: the second service will not help
            }
            QString why;   // https::get closes s; it waits in short steps and stops on exit
            const QByteArray bodyJson = https::get(static_cast<long long>(s), QString::fromLatin1(svc.host), svc.path,
                                                   timeoutMs, &why);
            const QJsonObject o = QJsonDocument::fromJson(bodyJson).object();
            const QString ip = o.value(QStringLiteral("ip")).toString();
            if (!ip.isEmpty()) {
                r.insert(QStringLiteral("ok"), true);
                r.insert(QStringLiteral("ip"), ip);
                r.insert(QStringLiteral("country"), o.value(QStringLiteral("country")).toString());
                r.insert(QStringLiteral("city"), o.value(QStringLiteral("city")).toString());
                r.insert(QStringLiteral("org"), o.value(QStringLiteral("org")).toString());
                r.insert(QStringLiteral("latencyMs"), int(QDateTime::currentMSecsSinceEpoch() - t0));
                break;
            }
            tried << QVariantMap{{QStringLiteral("service"), QString::fromLatin1(svc.host)},
                                 {QStringLiteral("error"), why.isEmpty() ? QObject::tr("unexpected answer") : why}};
        }
        if (r.isEmpty()) {
            r.insert(QStringLiteral("ok"), false);
            if (!tried.isEmpty()) {
                r.insert(QStringLiteral("summary"), QObject::tr("No IP service answered through the Shadowsocks server"));
                r.insert(QStringLiteral("hint"), QObject::tr("Wrong password or cipher, or the Shadowsocks server has no internet access."));
                r.insert(QStringLiteral("errors"), tried);
                QStringList parts;
                for (const QVariant &t : tried)
                    parts << t.toMap().value(QStringLiteral("service")).toString() + QStringLiteral(": ") + t.toMap().value(QStringLiteral("error")).toString();
                lastError = r.value(QStringLiteral("summary")).toString() + QStringLiteral(" (") + parts.join(QStringLiteral("; ")) + QStringLiteral(") — ")
                            + r.value(QStringLiteral("hint")).toString();
            }
            r.insert(QStringLiteral("error"), lastError);
        }
        r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        if (!bgthreads::stopping() && self)
            QMetaObject::invokeMethod(self.data(), [done, r] { done(r); }, Qt::QueuedConnection);
    });
}

void ProxyChecker::check(const ProxyConfig &proxy, Callback done, int timeoutMs)
{
    if (proxy.type == ProxyConfig::Shadowsocks) {
        checkShadowsocks(proxy, std::move(done), timeoutMs);
        return;
    }
    auto *nam = new QNetworkAccessManager(this);
    if (proxy.isSet()) {
        QNetworkProxy p(proxy.type == ProxyConfig::Socks5 ? QNetworkProxy::Socks5Proxy
                                                          : QNetworkProxy::HttpProxy,
                        proxy.host, quint16(proxy.port), proxy.username, proxy.password);
        // Remote DNS + tunnelling; nothing about the target is resolved locally.
        p.setCapabilities(QNetworkProxy::TunnelingCapability | QNetworkProxy::HostNameLookupCapability);
        nam->setProxy(p);
    } else {
        nam->setProxy(QNetworkProxy::NoProxy);
    }
    request(nam, 0, QDateTime::currentMSecsSinceEpoch(), std::move(done), timeoutMs);
}

void ProxyChecker::request(QNetworkAccessManager *nam, int endpoint, qint64 startedMs,
                           Callback done, int timeoutMs, const QVariantList &earlier)
{
    QNetworkRequest req(QUrl(QString::fromLatin1(kEndpoints[endpoint])));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "ShellWarden-SSH-Client");
    req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        const qint64 latency = QDateTime::currentMSecsSinceEpoch() - t0;

        QVariantMap r;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const QString ip = o.value(QStringLiteral("ip")).toString();
            if (!ip.isEmpty()) {
                r.insert(QStringLiteral("ok"), true);
                r.insert(QStringLiteral("ip"), ip);
                r.insert(QStringLiteral("country"), o.value(QStringLiteral("country")).toString());
                r.insert(QStringLiteral("city"), o.value(QStringLiteral("city")).toString());
                r.insert(QStringLiteral("org"), o.value(QStringLiteral("org")).toString());
                r.insert(QStringLiteral("latencyMs"), int(latency));
                r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
                nam->deleteLater();
                done(r);
                return;
            }
        }

        const bool proxyFault = reply->error() == QNetworkReply::ProxyConnectionRefusedError
                                || reply->error() == QNetworkReply::ProxyNotFoundError
                                || reply->error() == QNetworkReply::ProxyAuthenticationRequiredError;
        if (proxyFault) {   // the proxy itself failed: the second service would not help
            r.insert(QStringLiteral("ok"), false);
            r.insert(QStringLiteral("error"), reply->errorString());
            r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
            nam->deleteLater();
            done(r);
            return;
        }
        // short, human words per service; the full text stays in "error" for the log
        QString why;
        switch (reply->error()) {
        case QNetworkReply::NoError: why = tr("unexpected answer"); break;
        case QNetworkReply::HostNotFoundError: why = tr("name not found (DNS)"); break;
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError: why = tr("no answer (timeout)"); break;
        case QNetworkReply::ConnectionRefusedError: why = tr("connection refused"); break;
        case QNetworkReply::RemoteHostClosedError: why = tr("connection closed"); break;
        case QNetworkReply::SslHandshakeFailedError: why = tr("TLS error — something on the network intercepts HTTPS"); break;
        default: why = reply->errorString(); break;
        }
        QVariantList tried = earlier;
        tried << QVariantMap{{QStringLiteral("service"), QUrl(QString::fromLatin1(kEndpoints[endpoint])).host()},
                             {QStringLiteral("error"), why}};
        if (endpoint + 1 < kEndpointCount) {
            request(nam, endpoint + 1, startedMs, done, timeoutMs, tried);
            return;
        }
        const bool viaProxy = nam->proxy().type() != QNetworkProxy::NoProxy;
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("summary"), tr("No IP service answered"));
        r.insert(QStringLiteral("hint"), viaProxy ? tr("The proxy may have no internet access, or it blocks these services.")
                                                  : tr("No internet connection, or your network blocks these services."));
        r.insert(QStringLiteral("errors"), tried);
        QStringList parts;
        for (const QVariant &t : tried)
            parts << t.toMap().value(QStringLiteral("service")).toString() + QStringLiteral(": ") + t.toMap().value(QStringLiteral("error")).toString();
        r.insert(QStringLiteral("error"), r.value(QStringLiteral("summary")).toString() + QStringLiteral(" (") + parts.join(QStringLiteral("; ")) + QStringLiteral(")"));
        r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        nam->deleteLater();
        done(r);
    });
}
