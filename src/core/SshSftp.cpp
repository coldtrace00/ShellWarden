// SFTP part of SshWorker: listing, file operations and chunked transfers that
// run inside the worker's event loop, so the shell stays responsive.
#include "SshWorker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include <fcntl.h>
#include <algorithm>

namespace {
constexpr int kChunk = 64 * 1024;
constexpr int kChunksPerTick = 8;          // ≈ 512 KiB per 25 ms tick
}

QString SshWorker::sftpErrorText() const
{
    if (!m_sftp)
        return tr("SFTP is not available.");
    switch (sftp_get_error(m_sftp)) {
    case SSH_FX_NO_SUCH_FILE: return tr("No such file or folder.");
    case SSH_FX_PERMISSION_DENIED: return tr("Permission denied.");
    case SSH_FX_FILE_ALREADY_EXISTS: return tr("Already exists.");
    case SSH_FX_FAILURE: return tr("The server refused the operation.");
    default: return QString::fromUtf8(ssh_get_error(m_session));
    }
}

bool SshWorker::ensureSftp(QString *error)
{
    if (m_sftp)
        return true;
    if (!m_session || !m_shellOpen) {
        *error = tr("Not connected.");
        return false;
    }
    m_sftp = sftp_new(m_session);
    if (!m_sftp || sftp_init(m_sftp) != SSH_OK) {
        *error = tr("The server does not provide SFTP: %1").arg(QString::fromUtf8(ssh_get_error(m_session)));
        if (m_sftp) {
            sftp_free(m_sftp);
            m_sftp = nullptr;
        }
        return false;
    }
    return true;
}

void SshWorker::sftpList(int reqId, const QString &path)
{
    QString err;
    if (!ensureSftp(&err)) {
        emit sftpListed(reqId, path, {}, err);
        return;
    }
    QByteArray p = (path.isEmpty() ? QStringLiteral(".") : path).toUtf8();
    char *canon = sftp_canonicalize_path(m_sftp, p.constData());
    const QString full = canon ? QString::fromUtf8(canon) : path;
    if (canon)
        ssh_string_free_char(canon);

    const QByteArray fp = full.toUtf8();
    sftp_dir dir = sftp_opendir(m_sftp, fp.constData());
    if (!dir) {
        emit sftpListed(reqId, full, {}, sftpErrorText());
        return;
    }
    QVariantList entries;
    while (sftp_attributes a = sftp_readdir(m_sftp, dir)) {
        const QString name = QString::fromUtf8(a->name);
        if (name != QLatin1String(".") && name != QLatin1String("..")) {
            bool isDir = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
            const bool isLink = a->type == SSH_FILEXFER_TYPE_SYMLINK;
            if (isLink) {
                const QByteArray target = (full + QLatin1Char('/') + name).toUtf8();
                if (sftp_attributes t = sftp_stat(m_sftp, target.constData())) {
                    isDir = t->type == SSH_FILEXFER_TYPE_DIRECTORY;
                    sftp_attributes_free(t);
                }
            }
            entries << QVariantMap{
                {QStringLiteral("name"), name},
                {QStringLiteral("isDir"), isDir},
                {QStringLiteral("isLink"), isLink},
                {QStringLiteral("size"), qint64(a->size)},
                {QStringLiteral("mtime"), QDateTime::fromSecsSinceEpoch(qint64(a->mtime)).toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
                {QStringLiteral("perms"), int(a->permissions & 07777)},
                {QStringLiteral("owner"), QString::fromUtf8(a->owner ? a->owner : "")},
            };
        }
        sftp_attributes_free(a);
    }
    sftp_closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const QVariant &x, const QVariant &y) {
        const QVariantMap a = x.toMap(), b = y.toMap();
        if (a.value(QStringLiteral("isDir")).toBool() != b.value(QStringLiteral("isDir")).toBool())
            return a.value(QStringLiteral("isDir")).toBool();
        return a.value(QStringLiteral("name")).toString().compare(b.value(QStringLiteral("name")).toString(),
                                                                   Qt::CaseInsensitive) < 0;
    });
    emit sftpListed(reqId, full, entries, QString());
}

void SshWorker::sftpMkdir(int reqId, const QString &path)
{
    QString err;
    if (!ensureSftp(&err)) { emit sftpDone(reqId, err); return; }
    const QByteArray p = path.toUtf8();
    emit sftpDone(reqId, sftp_mkdir(m_sftp, p.constData(), 0755) == SSH_OK ? QString() : sftpErrorText());
}

void SshWorker::sftpRemove(int reqId, const QString &path, bool isDir)
{
    QString err;
    if (!ensureSftp(&err)) { emit sftpDone(reqId, err); return; }
    const QByteArray p = path.toUtf8();
    const int rc = isDir ? sftp_rmdir(m_sftp, p.constData()) : sftp_unlink(m_sftp, p.constData());
    emit sftpDone(reqId, rc == SSH_OK ? QString() : sftpErrorText());
}

void SshWorker::sftpRename(int reqId, const QString &from, const QString &to)
{
    QString err;
    if (!ensureSftp(&err)) { emit sftpDone(reqId, err); return; }
    const QByteArray a = from.toUtf8(), b = to.toUtf8();
    emit sftpDone(reqId, sftp_rename(m_sftp, a.constData(), b.constData()) == SSH_OK ? QString() : sftpErrorText());
}

void SshWorker::sftpDownload(int transferId, const QString &remote, const QString &local)
{
    Transfer t;
    t.id = transferId;
    t.upload = false;
    t.remote = remote;
    t.local = local;
    m_transfers.append(t);
    emit sftpProgress(transferId, 0, 0, TransferRunning, QString());
}

void SshWorker::sftpUpload(int transferId, const QString &local, const QString &remote)
{
    Transfer t;
    t.id = transferId;
    t.upload = true;
    t.remote = remote;
    t.local = local;
    t.total = QFileInfo(local).size();
    m_transfers.append(t);
    emit sftpProgress(transferId, 0, t.total, TransferRunning, QString());
}

void SshWorker::sftpCancel(int transferId)
{
    for (int i = 0; i < m_transfers.size(); ++i) {
        Transfer &t = m_transfers[i];
        if (t.id != transferId)
            continue;
        if (t.remoteFile)
            sftp_close(t.remoteFile);
        if (t.localFile) {
            const QString name = t.localFile->fileName();
            delete t.localFile;
            if (!t.upload)
                QFile::remove(name);
        }
        emit sftpProgress(t.id, t.done, t.total, TransferCancelled, QString());
        m_transfers.removeAt(i);
        return;
    }
}

void SshWorker::pumpTransfers()
{
    if (m_transfers.isEmpty())
        return;
    Transfer &t = m_transfers.first();   // one at a time keeps it simple and fair to the shell

    auto finish = [this](int state, const QString &err) {
        Transfer done = m_transfers.takeFirst();
        if (done.remoteFile)
            sftp_close(done.remoteFile);
        QString partName;
        if (done.localFile) {
            partName = done.localFile->fileName();
            done.localFile->close();
            delete done.localFile;
        }
        if (!done.upload && !partName.isEmpty()) {
            if (state == TransferDone) {
                QFile::remove(done.local);
                if (!QFile::rename(partName, done.local)) {
                    QFile::remove(partName);
                    state = TransferFailed;
                    emit sftpProgress(done.id, done.done, done.total, TransferFailed, tr("Cannot write %1").arg(done.local));
                    return;
                }
            } else {
                QFile::remove(partName);
            }
        }
        emit sftpProgress(done.id, done.done, done.total, state, err);
    };

    if (!t.started) {
        t.started = true;
        QString err;
        if (!ensureSftp(&err)) { finish(TransferFailed, err); return; }
        const QByteArray rp = t.remote.toUtf8();
        if (t.upload) {
            t.localFile = new QFile(t.local);
            if (!t.localFile->open(QIODevice::ReadOnly)) { finish(TransferFailed, t.localFile->errorString()); return; }
            t.remoteFile = sftp_open(m_sftp, rp.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (!t.remoteFile) { finish(TransferFailed, sftpErrorText()); return; }
        } else {
            t.remoteFile = sftp_open(m_sftp, rp.constData(), O_RDONLY, 0);
            if (!t.remoteFile) { finish(TransferFailed, sftpErrorText()); return; }
            if (sftp_attributes a = sftp_fstat(t.remoteFile)) {
                t.total = qint64(a->size);
                sftp_attributes_free(a);
            }
            QDir().mkpath(QFileInfo(t.local).absolutePath());
            t.localFile = new QFile(t.local + QStringLiteral(".part"));
            if (!t.localFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) { finish(TransferFailed, t.localFile->errorString()); return; }
        }
    }

    char buf[kChunk];
    for (int i = 0; i < kChunksPerTick; ++i) {
        if (t.upload) {
            const qint64 n = t.localFile->read(buf, kChunk);
            if (n < 0) { finish(TransferFailed, t.localFile->errorString()); return; }
            if (n == 0) { finish(TransferDone, QString()); return; }
            if (sftp_write(t.remoteFile, buf, size_t(n)) != n) { finish(TransferFailed, sftpErrorText()); return; }
            t.done += n;
        } else {
            const ssize_t n = sftp_read(t.remoteFile, buf, kChunk);
            if (n < 0) { finish(TransferFailed, sftpErrorText()); return; }
            if (n == 0) { finish(TransferDone, QString()); return; }
            if (t.localFile->write(buf, qint64(n)) != qint64(n)) { finish(TransferFailed, t.localFile->errorString()); return; }
            t.done += n;
        }
    }
    const qint64 now = m_clock.elapsed();
    if (now - t.lastReportMs > 150) {
        t.lastReportMs = now;
        emit sftpProgress(t.id, t.done, t.total, TransferRunning, QString());
    }
}

void SshWorker::sftpReadFile(int reqId, const QString &path)
{
    QString err;
    if (!ensureSftp(&err)) { emit sftpFileRead(reqId, path, {}, err); return; }
    const QByteArray p = path.toUtf8();
    sftp_attributes a = sftp_stat(m_sftp, p.constData());
    if (!a) { emit sftpFileRead(reqId, path, {}, sftpErrorText()); return; }
    const quint64 size = a->size;
    const bool isDir = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
    sftp_attributes_free(a);
    if (isDir) { emit sftpFileRead(reqId, path, {}, tr("This is a folder.")); return; }
    if (size > 5ull * 1024 * 1024) { emit sftpFileRead(reqId, path, {}, tr("The file is larger than 5 MB: download it instead.")); return; }
    sftp_file f = sftp_open(m_sftp, p.constData(), O_RDONLY, 0);
    if (!f) { emit sftpFileRead(reqId, path, {}, sftpErrorText()); return; }
    QByteArray data;
    char buf[32768];
    for (;;) {
        const ssize_t n = sftp_read(f, buf, sizeof buf);
        if (n < 0) { sftp_close(f); emit sftpFileRead(reqId, path, {}, sftpErrorText()); return; }
        if (n == 0) break;
        data.append(buf, int(n));
    }
    sftp_close(f);
    emit sftpFileRead(reqId, path, data, QString());
}

void SshWorker::sftpWriteFile(int reqId, const QString &path, const QByteArray &data)
{
    QString err;
    if (!ensureSftp(&err)) { emit sftpDone(reqId, err); return; }
    const QByteArray p = path.toUtf8();
    // keep the file's permissions: write over it in place
    int mode = 0644;
    if (sftp_attributes a = sftp_stat(m_sftp, p.constData())) {
        mode = int(a->permissions & 07777);
        sftp_attributes_free(a);
    }
    sftp_file f = sftp_open(m_sftp, p.constData(), O_WRONLY | O_CREAT | O_TRUNC, mode_t(mode));
    if (!f) { emit sftpDone(reqId, sftpErrorText()); return; }
    qsizetype off = 0;
    while (off < data.size()) {
        const ssize_t n = sftp_write(f, data.constData() + off, size_t(std::min<qsizetype>(32768, data.size() - off)));
        if (n <= 0) { sftp_close(f); emit sftpDone(reqId, sftpErrorText()); return; }
        off += n;
    }
    sftp_close(f);
    emit sftpDone(reqId, QString());
}
