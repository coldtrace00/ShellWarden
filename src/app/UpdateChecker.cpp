#include "UpdateChecker.h"
#include "core/Paths.h"
#include "core/Minisign.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

void UpdateChecker::setState(const QString &s, const QString &err)
{
    m_state = s;
    m_error = err;
    emit changed();
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    static const QRegularExpression nonNum(QStringLiteral("[^0-9.]"));
    QString x = a, y = b;
    x.remove(nonNum);
    y.remove(nonNum);
    const QStringList pa = x.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList pb = y.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int va = i < pa.size() ? pa.at(i).toInt() : 0;
        const int vb = i < pb.size() ? pb.at(i).toInt() : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

void UpdateChecker::check(const QString &repo)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"));
    if (!valid.match(repo.trimmed()).hasMatch()) {
        setState(QStringLiteral("error"), tr("Set the GitHub repository (owner/name) in the update settings."));
        return;
    }
    setState(QStringLiteral("checking"));
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo.trimmed())));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", QCoreApplication::applicationName().toUtf8() + "-updater");
    req.setTransferTimeout(15000);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            setState(QStringLiteral("error"), code == 404 ? tr("No releases published yet.") : r->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        m_latest = o.value(QStringLiteral("tag_name")).toString();
        m_notes = o.value(QStringLiteral("body")).toString().left(4000);
        m_page = o.value(QStringLiteral("html_url")).toString();
        m_assets = o.value(QStringLiteral("assets")).toArray().toVariantList();
        if (compareVersions(m_latest, QCoreApplication::applicationVersion()) <= 0) {
            setState(QStringLiteral("uptodate"));
            return;
        }
        setState(QStringLiteral("available"));
    });
}

QString UpdateChecker::pickAsset() const
{
    QStringList wanted;
#ifdef Q_OS_WIN
    // installed with "data next to the program" is still updated by the installer
    if (paths::isPortable() && !paths::isInstalled())
        wanted << QStringLiteral("-portable.zip");
    else
        wanted << QStringLiteral("-setup.exe");
#else
    wanted << QStringLiteral(".AppImage");
#endif
    for (const QString &suffix : wanted) {
        for (const QVariant &v : m_assets) {
            const QString name = v.toMap().value(QStringLiteral("name")).toString();
            if (name.endsWith(suffix, Qt::CaseInsensitive))
                return name;
        }
    }
    return {};
}

void UpdateChecker::download()
{
    m_assetName = pickAsset();
    m_assetUrl.clear();
    m_sumsUrl.clear();
    m_sigUrl.clear();
    for (const QVariant &v : m_assets) {
        const QVariantMap a = v.toMap();
        const QString name = a.value(QStringLiteral("name")).toString();
        if (name == m_assetName)
            m_assetUrl = a.value(QStringLiteral("browser_download_url")).toString();
        if (name.compare(QStringLiteral("SHA256SUMS"), Qt::CaseInsensitive) == 0
            || name.compare(QStringLiteral("SHA256SUMS.txt"), Qt::CaseInsensitive) == 0
            || name == m_assetName + QStringLiteral(".sha256"))
            m_sumsUrl = a.value(QStringLiteral("browser_download_url")).toString();
        if (name.compare(QStringLiteral("SHA256SUMS.minisig"), Qt::CaseInsensitive) == 0
            || name.compare(QStringLiteral("SHA256SUMS.txt.minisig"), Qt::CaseInsensitive) == 0)
            m_sigUrl = a.value(QStringLiteral("browser_download_url")).toString();
    }
    if (m_assetUrl.isEmpty()) {
        setState(QStringLiteral("error"), tr("This release has no package for this system. Open the release page."));
        return;
    }
    if (m_sumsUrl.isEmpty()) {
        setState(QStringLiteral("error"), tr("The release has no SHA256SUMS file, so the download cannot be verified."));
        return;
    }
    fetchChecksums();
}

void UpdateChecker::fetchChecksums()
{
    setState(QStringLiteral("downloading"));
    QNetworkRequest req{QUrl(m_sumsUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"), r->errorString());
            return;
        }
        const QByteArray sums = r->readAll();
        const QString pubKey = QStringLiteral(APP_UPDATE_PUBKEY);
        if (pubKey.isEmpty()) {   // a build without a release key (forks, own builds): checksum only
            useChecksums(sums);
            return;
        }
        if (m_sigUrl.isEmpty()) {
            setState(QStringLiteral("error"), tr("This release is not signed (no SHA256SUMS.minisig). Update not installed."));
            return;
        }
        QNetworkRequest sreq{QUrl(m_sigUrl)};
        sreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *sr = m_nam->get(sreq);
        connect(sr, &QNetworkReply::finished, this, [this, sr, sums, pubKey] {
            sr->deleteLater();
            if (sr->error() != QNetworkReply::NoError) {
                setState(QStringLiteral("error"), sr->errorString());
                return;
            }
            QString err;
            if (!minisign::verify(sums, sr->readAll(), pubKey, &err, &m_signedBy)) {
                setState(QStringLiteral("error"), tr("Signature check failed: %1 Update not installed.").arg(err));
                return;
            }
            useChecksums(sums);
        });
    });
}

void UpdateChecker::useChecksums(const QByteArray &sums)
{
    {
        m_expectedSha.clear();
        for (const QByteArray &line : sums.split('\n')) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 2 && QString::fromUtf8(parts.last()).remove(QLatin1Char('*')) == m_assetName)
                m_expectedSha = QString::fromLatin1(parts.first()).toLower();
            else if (parts.size() == 1 && parts.first().size() == 64)
                m_expectedSha = QString::fromLatin1(parts.first()).toLower();
        }
        if (m_expectedSha.size() != 64) {
            setState(QStringLiteral("error"), tr("No checksum for %1 in SHA256SUMS.").arg(m_assetName));
            return;
        }

        const QString dir = paths::updatesDir();
        QDir().mkpath(dir);
        m_file = dir + QLatin1Char('/') + m_assetName;
        auto *out = new QFile(m_file, this);
        if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setState(QStringLiteral("error"), out->errorString());
            delete out;
            return;
        }
        QNetworkRequest req{QUrl(m_assetUrl)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *dl = m_nam->get(req);
        auto *hash = new QCryptographicHash(QCryptographicHash::Sha256);
        connect(dl, &QNetworkReply::readyRead, this, [dl, out, hash] {
            const QByteArray chunk = dl->readAll();
            hash->addData(chunk);
            out->write(chunk);
        });
        connect(dl, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            m_progress = total > 0 ? double(got) / double(total) : 0;
            emit changed();
        });
        connect(dl, &QNetworkReply::finished, this, [this, dl, out, hash] {
            dl->deleteLater();
            const QByteArray rest = dl->readAll();
            hash->addData(rest);
            out->write(rest);
            out->close();
            out->deleteLater();
            const QString got = QString::fromLatin1(hash->result().toHex());
            delete hash;
            if (dl->error() != QNetworkReply::NoError) {
                QFile::remove(m_file);
                setState(QStringLiteral("error"), dl->errorString());
                return;
            }
            if (got != m_expectedSha) {
                QFile::remove(m_file);
                setState(QStringLiteral("error"), tr("Checksum mismatch — the download was deleted."));
                return;
            }
            m_progress = 1;
            setState(QStringLiteral("ready"));
        });
    }
}

bool UpdateChecker::install()
{
    if (m_state != QLatin1String("ready") || m_file.isEmpty())
        return false;
#ifdef Q_OS_WIN
    if (m_file.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        if (QProcess::startDetached(m_file, {QStringLiteral("/SILENT"), QStringLiteral("/CLOSEAPPLICATIONS"),
                                             QStringLiteral("/RESTARTAPPLICATIONS")})) {
            emit quitRequested();
            return true;
        }
        setState(QStringLiteral("error"), tr("Could not start the installer."));
        return false;
    }
    // Portable: show the verified zip; the user extracts it over the folder.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_file).absolutePath()));
    return true;
#else
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && m_file.endsWith(QStringLiteral(".AppImage"))) {
        QFile::remove(appImage + QStringLiteral(".old"));
        QFile::rename(appImage, appImage + QStringLiteral(".old"));
        if (QFile::copy(m_file, appImage)) {
            QFile(appImage).setPermissions(QFile(appImage).permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
            QProcess::startDetached(appImage, {});
            emit quitRequested();
            return true;
        }
        QFile::rename(appImage + QStringLiteral(".old"), appImage);
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_file).absolutePath()));
    return true;
#endif
}

void UpdateChecker::openPage()
{
    if (!m_page.isEmpty())
        QDesktopServices::openUrl(QUrl(m_page));
}
