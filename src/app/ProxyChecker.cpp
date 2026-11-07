#include "ProxyChecker.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

// Two independent services: if the first is blocked, the second answers.
const char *const kEndpoints[] = {
    "https://ipinfo.io/json",
    "https://api.ipify.org?format=json",
};
constexpr int kEndpointCount = 2;

} // namespace

ProxyChecker::ProxyChecker(QObject *parent) : QObject(parent) {}

void ProxyChecker::check(const ProxyConfig &proxy, Callback done, int timeoutMs)
{
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
                           Callback done, int timeoutMs)
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
        if (endpoint + 1 < kEndpointCount
            && reply->error() != QNetworkReply::ProxyConnectionRefusedError
            && reply->error() != QNetworkReply::ProxyNotFoundError
            && reply->error() != QNetworkReply::ProxyAuthenticationRequiredError) {
            request(nam, endpoint + 1, startedMs, done, timeoutMs);
            return;
        }
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), err);
        r.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        nam->deleteLater();
        done(r);
    });
}
