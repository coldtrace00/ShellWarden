#pragma once

#include <QString>

// Command history: what was typed at a shell prompt, read back from the
// terminal line when Enter is pressed.
namespace history {

// "user@host:~$ ls -la" -> "ls -la". Empty when the line has no shell prompt,
// the command starts with a space (not remembered, like bash's ignorespace),
// the prompt belongs to a database shell or other program (mysql>, psql, >>>),
// or the command looks like it contains a password or token.
QString commandFromLine(const QString &line);

// "mysql -pS3cret", "export API_TOKEN=…", "curl -u user:pass", URLs with a password…
bool looksSecret(const QString &cmd);

} // namespace history
