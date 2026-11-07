#include "SyntaxHighlighter.h"

#include <QFileInfo>

namespace {
QTextCharFormat fmt(const char *color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(QColor(QLatin1String(color)));
    if (bold) f.setFontWeight(QFont::DemiBold);
    if (italic) f.setFontItalic(true);
    return f;
}
} // namespace

SyntaxHighlighter::SyntaxHighlighter(QObject *parent) : QSyntaxHighlighter(parent)
{
    // the Midnight palette (default terminal theme), so code looks like the terminal
    m_comment = fmt("#6b7a96", false, true);
    m_keyword = fmt("#bba8f5", true);
    m_string = fmt("#98dbb5");
    m_number = fmt("#ecd08e");
    m_variable = fmt("#7fd4e6");
    m_key = fmt("#8cb8fa");
    m_section = fmt("#ecd08e", true);
    m_builtin = fmt("#8cb8fa");
    m_tag = fmt("#f08ea0");
    m_heading = fmt("#8cb8fa", true);
    m_punct = fmt("#8a97b3");
}

void SyntaxHighlighter::setQuickDocument(QQuickTextDocument *doc)
{
    if (doc == m_quickDoc)
        return;
    m_quickDoc = doc;
    setDocument(doc ? doc->textDocument() : nullptr);
    emit documentChanged();
}

void SyntaxHighlighter::setFileName(const QString &name)
{
    if (name == m_fileName) return;
    m_fileName = name;
    detect();
}

void SyntaxHighlighter::setFirstLine(const QString &line)
{
    if (line == m_firstLine) return;
    m_firstLine = line;
    detect();
}

void SyntaxHighlighter::detect()
{
    const QFileInfo fi(m_fileName);
    const QString name = fi.fileName().toLower();
    const QString ext = fi.suffix().toLower();
    const QString path = m_fileName.toLower();
    const QString shebang = m_firstLine.startsWith(QLatin1String("#!")) ? m_firstLine.toLower() : QString();

    Lang l = Plain;
    if (shebang.contains(QLatin1String("python"))) l = Python;
    else if (shebang.contains(QLatin1String("sh")) || shebang.contains(QLatin1String("bash"))) l = Shell;
    else if (QStringList{"sh", "bash", "zsh", "ksh", "bats"}.contains(ext)
             || QStringList{".bashrc", ".profile", ".bash_profile", ".zshrc", ".bash_aliases", ".bash_logout",
                            "profile", "bashrc", ".envrc"}.contains(name))
        l = Shell;
    else if (ext == QLatin1String("py") || ext == QLatin1String("pyw")) l = Python;
    else if (ext == QLatin1String("yml") || ext == QLatin1String("yaml") || name == QLatin1String(".clang-format")) l = Yaml;
    else if (ext == QLatin1String("json") || ext == QLatin1String("jsonc")) l = Json;
    else if (name == QLatin1String("dockerfile") || name.startsWith(QLatin1String("dockerfile.")) || ext == QLatin1String("dockerfile")
             || name == QLatin1String("containerfile"))
        l = Docker;
    else if (path.contains(QLatin1String("nginx")) || ext == QLatin1String("nginx")) l = Nginx;
    else if (QStringList{"ini", "conf", "cfg", "cnf", "env", "toml", "properties", "service", "timer", "socket",
                         "mount", "target", "path", "network", "netdev", "link", "desktop", "repo", "list"}.contains(ext)
             || QStringList{"sshd_config", "ssh_config", "config", "fstab", "hosts", "crontab", ".env",
                            "sysctl.conf", "jail.local", ".gitconfig", "my.cnf", "php.ini"}.contains(name))
        l = Ini;
    else if (QStringList{"js", "mjs", "cjs", "ts", "tsx", "jsx", "vue"}.contains(ext)) l = Js;
    else if (QStringList{"c", "h", "cc", "cpp", "hpp", "cxx", "go", "rs", "java", "kt", "cs", "php", "swift", "qml", "scala"}.contains(ext)) l = CLike;
    else if (ext == QLatin1String("sql")) l = Sql;
    else if (QStringList{"xml", "html", "htm", "svg", "xhtml", "plist", "xsd", "xsl"}.contains(ext)) l = Xml;
    else if (ext == QLatin1String("md") || ext == QLatin1String("markdown")) l = Markdown;
    else if (m_firstLine.startsWith(QLatin1String("<?xml")) || m_firstLine.startsWith(QLatin1String("<!DOCTYPE"))) l = Xml;
    else if (m_firstLine.trimmed().startsWith(QLatin1Char('{'))) l = Json;
    else if (m_firstLine.startsWith(QLatin1Char('['))) l = Ini;

    static const char *names[] = {"Text", "Bash", "Python", "YAML", "JSON", "Config", "nginx", "Dockerfile",
                                  "JavaScript", "C-like", "SQL", "XML / HTML", "Markdown"};
    const bool changed = l != m_lang || m_languageName.isEmpty();
    m_lang = l;
    m_languageName = QString::fromLatin1(names[l]);
    if (changed) {
        build();
        rehighlight();
    }
    emit languageChanged();
}

void SyntaxHighlighter::add(const QString &pattern, const QTextCharFormat &f, int group,
                            QRegularExpression::PatternOptions opts)
{
    m_rules.append({QRegularExpression(pattern, opts), f, group});
}

void SyntaxHighlighter::addKeywords(const QStringList &words, const QTextCharFormat &f, bool ci)
{
    add(QStringLiteral("\\b(?:%1)\\b").arg(words.join(QLatin1Char('|'))), f, 0,
        ci ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
}

void SyntaxHighlighter::build()
{
    m_rules.clear();
    m_multi.clear();
    const QString dq = QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"?");
    const QString sq = QStringLiteral("'(?:[^'\\\\]|\\\\.)*'?");
    const QString num = QStringLiteral("\\b(?:0x[0-9a-fA-F]+|\\d+(?:\\.\\d+)?)\\b");
    const QString hashComment = QStringLiteral("(?:^|(?<=\\s))#.*$");

    switch (m_lang) {
    case Shell:
        add(num, m_number);
        addKeywords({"if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac", "in",
                     "function", "select", "time", "return", "break", "continue", "local", "export", "readonly",
                     "declare", "typeset", "unset", "shift", "exit", "trap", "set", "source", "eval", "exec"}, m_keyword);
        addKeywords({"echo", "printf", "read", "cd", "test", "true", "false", "install", "mkdir", "cp", "mv", "rm",
                     "cat", "grep", "sed", "awk", "chmod", "chown", "systemctl", "apt", "apt-get", "curl", "wget",
                     "sudo", "tee", "ln", "touch", "tar", "docker", "git"}, m_builtin);
        add(QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(\\)"), m_builtin, 1);          // name() {
        add(QStringLiteral("(?<![\\\\$])\\b[A-Za-z_][A-Za-z0-9_]*(?==)"), m_variable);       // NAME=
        add(dq, m_string);
        add(QStringLiteral("\\$(?:\\{[^}]*\\}|[A-Za-z_][A-Za-z0-9_]*|[0-9#?@*!$-])"), m_variable); // $X ${X} (also inside "…")
        add(sq, m_string);
        add(QStringLiteral("<<-?\\s*['\"]?[A-Za-z_]+['\"]?"), m_keyword);
        add(hashComment, m_comment);
        add(QStringLiteral("^#!.*$"), m_comment);
        break;
    case Python:
        add(num, m_number);
        addKeywords({"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif",
                     "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda",
                     "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "match",
                     "case", "True", "False", "None"}, m_keyword);
        addKeywords({"print", "len", "range", "open", "str", "int", "float", "dict", "list", "set", "tuple", "bool",
                     "isinstance", "super", "self", "enumerate", "zip", "map", "filter", "sorted", "any", "all"}, m_builtin);
        add(QStringLiteral("\\b(?:def|class)\\s+([A-Za-z_][A-Za-z0-9_]*)"), m_variable, 1);
        add(QStringLiteral("^\\s*@[\\w.]+"), m_tag);
        add(QStringLiteral("\\b[rbfuRBFU]{0,2}") + dq, m_string);
        add(QStringLiteral("\\b[rbfuRBFU]{0,2}") + sq, m_string);
        add(hashComment, m_comment);
        m_multi.append({QRegularExpression(QStringLiteral("\"\"\"|'''")), QRegularExpression(QStringLiteral("\"\"\"|'''")), m_string});
        break;
    case Yaml:
        add(num, m_number);
        addKeywords({"true", "false", "yes", "no", "on", "off", "null"}, m_keyword, true);
        add(QStringLiteral("^\\s*-?\\s*([\\w.\\-/\"' ]+?)\\s*:(?=\\s|$)"), m_key, 1);
        add(QStringLiteral("^\\s*-(?=\\s)"), m_punct);
        add(QStringLiteral("[&*][\\w-]+"), m_tag);
        add(QStringLiteral("!!?[\\w/]+"), m_tag);
        add(QStringLiteral("\\{\\{[^}]*\\}\\}"), m_variable);    // Ansible / Helm
        add(QStringLiteral(":\\s+(") + dq + QLatin1Char('|') + sq + QLatin1Char(')'), m_string, 1);
        add(QStringLiteral("^\\s*-\\s+(") + dq + QLatin1Char('|') + sq + QLatin1Char(')'), m_string, 1);
        add(QStringLiteral("^---|^\\.\\.\\."), m_section);
        add(hashComment, m_comment);
        break;
    case Json:
        add(num, m_number);
        addKeywords({"true", "false", "null"}, m_keyword);
        add(dq, m_string);
        add(QStringLiteral("(") + dq + QStringLiteral(")\\s*:"), m_key, 1);
        add(QStringLiteral("//.*$"), m_comment);
        break;
    case Ini:
        add(num, m_number);
        addKeywords({"true", "false", "yes", "no", "on", "off", "enabled", "disabled"}, m_keyword, true);
        add(QStringLiteral("^\\s*\\[[^\\]]*\\]"), m_section);
        add(QStringLiteral("^\\s*([\\w.\\-]+)\\s*(?==|:)"), m_key, 1);                   // key = value
        add(QStringLiteral("^\\s*([A-Z][A-Za-z0-9]+)(?=\\s)"), m_key, 1);               // sshd_config: Port 22
        add(QStringLiteral("\\$\\{?[A-Za-z_][\\w]*\\}?|%[a-zA-Z]"), m_variable);
        add(dq, m_string);
        add(QStringLiteral("^\\s*[#;].*$"), m_comment);
        add(QStringLiteral("(?<=\\s)#.*$"), m_comment);
        break;
    case Nginx:
        add(num, m_number);
        add(QStringLiteral("^\\s*([a-z_]+)(?=[\\s;{])"), m_key, 1);
        addKeywords({"server", "location", "http", "events", "upstream", "if", "map", "stream", "include"}, m_keyword);
        add(QStringLiteral("\\$[\\w]+"), m_variable);
        add(QStringLiteral("\\b\\d+[kKmMgGsShHdD]\\b"), m_number);
        add(dq, m_string);
        add(sq, m_string);
        add(QStringLiteral("[{};]"), m_punct);
        add(hashComment, m_comment);
        break;
    case Docker:
        add(num, m_number);
        add(QStringLiteral("^\\s*(?:FROM|RUN|CMD|LABEL|EXPOSE|ENV|ADD|COPY|ENTRYPOINT|VOLUME|USER|WORKDIR|ARG|ONBUILD|"
                           "STOPSIGNAL|HEALTHCHECK|SHELL|MAINTAINER)\\b"), m_keyword, 0, QRegularExpression::CaseInsensitiveOption);
        add(QStringLiteral("\\bAS\\b"), m_keyword);
        add(QStringLiteral("\\$\\{?[A-Za-z_][\\w]*\\}?"), m_variable);
        add(dq, m_string);
        add(sq, m_string);
        add(hashComment, m_comment);
        break;
    case Js:
    case CLike:
        add(num, m_number);
        if (m_lang == Js)
            addKeywords({"var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "switch",
                         "case", "break", "continue", "new", "delete", "typeof", "instanceof", "in", "of", "class",
                         "extends", "import", "export", "from", "default", "async", "await", "try", "catch", "finally",
                         "throw", "this", "null", "undefined", "true", "false", "interface", "type", "enum", "yield"}, m_keyword);
        else
            addKeywords({"if", "else", "for", "while", "do", "switch", "case", "break", "continue", "return", "goto",
                         "struct", "class", "enum", "union", "typedef", "namespace", "using", "public", "private",
                         "protected", "static", "const", "constexpr", "virtual", "override", "template", "typename",
                         "new", "delete", "try", "catch", "throw", "this", "true", "false", "nullptr", "null", "func",
                         "package", "import", "fn", "let", "mut", "impl", "pub", "use", "mod", "match", "go", "defer",
                         "chan", "select", "interface", "var", "type", "auto", "void", "int", "char", "bool", "float",
                         "double", "long", "unsigned", "string", "function", "echo", "property", "signal"}, m_keyword);
        add(QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\()"), m_builtin, 1);
        add(QStringLiteral("^\\s*#\\s*\\w+"), m_tag);    // #include, #define
        add(dq, m_string);
        add(sq, m_string);
        if (m_lang == Js) add(QStringLiteral("`[^`]*`?"), m_string);
        add(QStringLiteral("//.*$"), m_comment);
        m_multi.append({QRegularExpression(QStringLiteral("/\\*")), QRegularExpression(QStringLiteral("\\*/")), m_comment});
        break;
    case Sql:
        add(num, m_number);
        addKeywords({"select", "from", "where", "and", "or", "not", "insert", "into", "values", "update", "set",
                     "delete", "create", "table", "index", "view", "drop", "alter", "add", "column", "primary", "key",
                     "foreign", "references", "join", "left", "right", "inner", "outer", "on", "group", "by", "order",
                     "having", "limit", "offset", "as", "distinct", "union", "all", "null", "is", "in", "like",
                     "between", "case", "when", "then", "else", "end", "begin", "commit", "rollback", "grant",
                     "revoke", "database", "if", "exists", "default", "unique", "constraint", "returning"}, m_keyword, true);
        add(sq, m_string);
        add(dq, m_key);
        add(QStringLiteral("--.*$"), m_comment);
        m_multi.append({QRegularExpression(QStringLiteral("/\\*")), QRegularExpression(QStringLiteral("\\*/")), m_comment});
        break;
    case Xml:
        add(QStringLiteral("</?([\\w:.\\-]+)"), m_tag, 1);
        add(QStringLiteral("\\s([\\w:.\\-]+)(?==)"), m_key, 1);
        add(dq, m_string);
        add(sq, m_string);
        add(QStringLiteral("&\\w+;"), m_variable);
        add(QStringLiteral("</?|/?>"), m_punct);
        m_multi.append({QRegularExpression(QStringLiteral("<!--")), QRegularExpression(QStringLiteral("-->")), m_comment});
        break;
    case Markdown:
        add(QStringLiteral("^#{1,6}\\s.*$"), m_heading);
        add(QStringLiteral("^\\s*(?:[-*+]|\\d+\\.)\\s"), m_punct);
        add(QStringLiteral("\\*\\*[^*]+\\*\\*|__[^_]+__"), m_keyword);
        add(QStringLiteral("`[^`]+`"), m_string);
        add(QStringLiteral("\\[[^\\]]*\\]\\([^)]*\\)"), m_key);
        add(QStringLiteral("^>.*$"), m_comment);
        m_multi.append({QRegularExpression(QStringLiteral("^\\s*```")), QRegularExpression(QStringLiteral("^\\s*```")), m_string});
        break;
    case Plain:
        add(num, m_number);
        add(dq, m_string);
        add(hashComment, m_comment);
        break;
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &r : std::as_const(m_rules)) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int len = m.capturedLength(r.group);
            if (len > 0)
                setFormat(int(m.capturedStart(r.group)), len, r.fmt);
        }
    }

    // constructs over several lines; block state = index of the open one + 1
    setCurrentBlockState(0);
    int pos = 0;
    int open = previousBlockState() > 0 ? previousBlockState() - 1 : -1;
    while (pos <= text.size()) {
        if (open < 0) {
            int best = -1, bestAt = int(text.size()) + 1, bestLen = 0;
            for (int i = 0; i < m_multi.size(); ++i) {
                const auto m = m_multi.at(i).start.match(text, pos);
                if (m.hasMatch() && m.capturedStart() < bestAt) {
                    best = i; bestAt = int(m.capturedStart()); bestLen = int(m.capturedLength());
                }
            }
            if (best < 0)
                break;
            open = best;
            const auto e = m_multi.at(open).end.match(text, bestAt + bestLen);
            if (e.hasMatch()) {
                setFormat(bestAt, int(e.capturedEnd()) - bestAt, m_multi.at(open).fmt);
                pos = int(e.capturedEnd());
                open = -1;
                continue;
            }
            setFormat(bestAt, int(text.size()) - bestAt, m_multi.at(open).fmt);
            setCurrentBlockState(open + 1);
            break;
        }
        // continuing from the previous line
        const auto e = m_multi.at(open).end.match(text, pos);
        if (!e.hasMatch()) {
            setFormat(0, int(text.size()), m_multi.at(open).fmt);
            setCurrentBlockState(open + 1);
            break;
        }
        setFormat(0, int(e.capturedEnd()), m_multi.at(open).fmt);
        pos = int(e.capturedEnd());
        open = -1;
    }
}
