#include "ProxyChecker.h"

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
    std::thread([self, proxy, done, timeoutMs] {
        struct Service { const char *host, *path; };
        const Service services[] = {{"ipinfo.io", "/json"}, {"api.ipify.org", "/?format=json"}};
        QVariantMap r;
        QString lastError;
        for (const Service &svc : services) {
            const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
            QString err;
            const sw_socket_t s = shadowsocks::open(proxy, QString::fromLatin1(svc.host), 80, timeoutMs, false, &err);
            if (s == kInvalidSocket) {
                lastError = err;
                break;   // the server itself is not reachable: the second service will not help
            }
            const QByteArray req = QByteArray("GET ") + svc.path + " HTTP/1.0\r\nHost: " + svc.host
                                   + "\r\nUser-Agent: ShellWarden-SSH-Client\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
            QByteArray resp;
            if (proxytunnel::writeAll(s, req, timeoutMs)) {
                char buf[4096];
                const qint64 until = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
                while (resp.size() < 65536 && QDateTime::currentMSecsSinceEpoch() < until) {
                    const int n = proxytunnel::readSome(s, buf, sizeof buf, 1000);
                    if (n < 0 || (n == 0 && !resp.isEmpty()))
                        break;   // error, or the service closed the connection after its answer
                    resp.append(buf, n);
                }
            }
            proxytunnel::closeSocket(s);
            const int body = int(resp.indexOf("\r\n\r\n"));
            const QJsonObject o = body > 0 ? QJsonDocument::fromJson(resp.mid(body + 4)).object() : QJsonObject();
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
            lastError = (lastError.isEmpty() ? QString() : lastError + QStringLiteral("; ")) + QString::fromLatin1(svc.host) + QStringLiteral(": ")
                        + (resp.isEmpty() ? QObject::tr("no answer through the Shadowsocks server — wrong password or cipher, or the server has no internet access")
                                          : QObject::tr("Unexpected answer from the IP service"));
        }
        if (r.isEmpty()) {
            r.insert(QStringLiteral("ok"), false);
            r.insert(QStringLiteral("error"), lastError);
        }
        r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        if (self)
            QMetaObject::invokeMethod(self.data(), [done, r] { done(r); }, Qt::QueuedConnection);
    }).detach();
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
                           Callback done, int timeoutMs, const QString &earlierErrors)
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

        const QString err = reply->error() == QNetworkReply::NoError
                                ? tr("Unexpected answer from the IP service")
                                : reply->errorString();
        const bool proxyFault = reply->error() == QNetworkReply::ProxyConnectionRefusedError
                                || reply->error() == QNetworkReply::ProxyNotFoundError
                                || reply->error() == QNetworkReply::ProxyAuthenticationRequiredError;
        // name every service that was tried, so a blocked one is easy to spot
        const QString all = proxyFault ? err
                            : (earlierErrors.isEmpty() ? QString() : earlierErrors + QStringLiteral("; "))
                                  + QUrl(QString::fromLatin1(kEndpoints[endpoint])).host() + QStringLiteral(": ") + err;
        if (endpoint + 1 < kEndpointCount && !proxyFault) {
            request(nam, endpoint + 1, startedMs, done, timeoutMs, all);
            return;
        }
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), endpoint > 0 && !proxyFault ? tr("no IP service answered (%1)").arg(all) : all);
        r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        nam->deleteLater();
        done(r);
    });
}
