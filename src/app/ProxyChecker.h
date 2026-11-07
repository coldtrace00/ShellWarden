#pragma once

#include "core/ProxyTunnel.h"

#include <QObject>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;

// Answers "does this route work, and which public IP does it exit from?".
// The request goes through exactly the given proxy (or explicitly direct,
// ignoring any system proxy), and for SOCKS5 host names are resolved by the
// proxy, not locally.
//
// Result map: ok (bool), ip, country, city, org, latencyMs (int), error.
class ProxyChecker : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QVariantMap &)>;

    explicit ProxyChecker(QObject *parent = nullptr);

    void check(const ProxyConfig &proxy, Callback done, int timeoutMs = 10000);

private:
    void checkShadowsocks(const ProxyConfig &proxy, Callback done, int timeoutMs);
    void request(QNetworkAccessManager *nam, int endpoint, qint64 startedMs, Callback done, int timeoutMs,
                 const QString &earlierErrors = QString());
};
