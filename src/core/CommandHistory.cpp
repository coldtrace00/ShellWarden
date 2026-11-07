#include "CommandHistory.h"

#include <QRegularExpression>

namespace history {

QString commandFromLine(const QString &line)
{
    // The prompt ends at the first "$ ", "# ", "% " or "> " (user@host:~$ ls -la);
    // prompts of zsh themes end with "❯", "➜" or "»".
    static const QRegularExpression shell(QStringLiteral("^.*?[$#%>] "));
    static const QRegularExpression fancy(QStringLiteral("^.*?[\u276F\u279C\u00BB]\\s+"));
    QRegularExpressionMatch m = shell.match(line);
    if (!m.hasMatch())
        m = fancy.match(line);
    if (!m.hasMatch())
        return {};                  // no prompt: a password or a program asking something
    const QString prompt = line.left(m.capturedEnd()).trimmed();

    // Database shells and other interactive programs are not the system shell:
    // "mysql>", "sqlite>", ">>>", "redis …>", "ftp>", psql "db=#" / "db=>" / "db-#".
    // Their commands often carry passwords (ALTER USER … IDENTIFIED BY '…').
    // A "…>" prompt is a shell only on Windows: "PS C:\\…>" or "C:\\…>".
    if (prompt.endsWith(QLatin1Char('>'))) {
        static const QRegularExpression winShell(QStringLiteral("^(PS .*|[A-Za-z]:\\\\.*)>$"));
        if (!winShell.match(prompt).hasMatch())
            return {};
    }
    if (prompt.endsWith(QLatin1String("=#")) || prompt.endsWith(QLatin1String("-#")) || prompt.endsWith(QLatin1String("(#")))
        return {};

    const QString rest = line.mid(m.capturedEnd());
    if (rest.trimmed().isEmpty() || rest.startsWith(QLatin1Char(' ')))
        return {};                  // " command" = do not remember (like bash's ignorespace)
    const QString cmd = rest.trimmed();
    if (looksSecret(cmd))
        return {};
    return cmd;
}

// Commands that probably contain a password or a token are never remembered:
// "mysql -pS3cret", "export API_TOKEN=…", "curl -u user:pass", "sshpass -p …",
// "username admin password …" (network devices), "… IDENTIFIED BY '…'".
bool looksSecret(const QString &cmd)
{
    static const QRegularExpression secret(
        QStringLiteral("passw|secret|token|api[_-]?key|identified\\s+by|credential|private[_-]?key"
                       "|\\bmysql(dump)?\\b.*\\s-p\\S|\\bsshpass\\b|\\bcurl\\b.*\\s(-u|--user)\\s"
                       "|\\bhtpasswd\\b|://[^/@\\s]+:[^/@\\s]+@"),
        QRegularExpression::CaseInsensitiveOption);
    return secret.match(cmd).hasMatch();
}

} // namespace history
