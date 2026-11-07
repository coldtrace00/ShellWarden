#pragma once

#include <QObject>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// Checks GitHub Releases of the configured repository ("owner/repo"),
// downloads the right package for this platform and verifies its SHA-256
// against the SHA256SUMS file published in the same release before running it.
class UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)          // idle|checking|available|uptodate|downloading|ready|error
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    // the part of the release notes in the app's language (see pickNotes)
    Q_PROPERTY(QString localizedNotes READ localizedNotes NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic NOTIFY changed)   // the last check ran by itself at start
    Q_PROPERTY(QString pageUrl READ pageUrl NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString downloadedFile READ downloadedFile NOTIFY changed)
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    QString localizedNotes() const { return pickNotes(m_notes, m_lang); }
    bool automatic() const { return m_automatic; }
    void setLanguage(const QString &code);
    QString pageUrl() const { return m_page; }
    QString error() const { return m_error; }
    double progress() const { return m_progress; }
    QString downloadedFile() const { return m_file; }

    Q_INVOKABLE void check(const QString &repo, bool automatic = false);
    Q_INVOKABLE void download();
    Q_INVOKABLE bool install();         // runs the installer / replaces the AppImage
    Q_INVOKABLE void openPage();

    static int compareVersions(const QString &a, const QString &b);
    // Release notes may hold one section per language:
    //   <!-- lang:en --> … <!-- lang:uk --> … <!-- lang:ru --> …
    // Returns the section for `lang`, else English, else the first one;
    // notes without markers are returned as they are.
    static QString pickNotes(const QString &body, const QString &lang);

signals:
    void changed();
    void quitRequested();

private:
    void setState(const QString &s, const QString &err = {});
    void fetchChecksums();
    void useChecksums(const QByteArray &sums);
    QString pickAsset() const;

    QNetworkAccessManager *m_nam;
    QString m_state = QStringLiteral("idle");
    QString m_latest, m_notes, m_page, m_error, m_file;
    QVariantList m_assets;
    QString m_assetName, m_assetUrl, m_sumsUrl, m_sigUrl, m_expectedSha;
    QString m_signedBy;   // trusted comment of a verified signature
    double m_progress = 0;
    QString m_lang = QStringLiteral("en");
    bool m_automatic = false;
};
