#pragma once

#include <QPointer>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// Colours code in the SFTP file editor. The language is guessed from the file
// name and the first line (#!/bin/bash …): shell, Python, YAML, JSON, INI /
// .conf / systemd / sshd_config, nginx, Dockerfile, JavaScript/TypeScript,
// C-like (C, C++, Go, Rust, Java, PHP), SQL, XML/HTML, Markdown; anything else
// still gets comments, strings and numbers. Plain regular expressions, line by
// line, so even big configs stay fast.
//
//   SyntaxHighlighter { document: editor.textDocument; fileName: root.path; firstLine: … }
class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ quickDocument WRITE setQuickDocument NOTIFY documentChanged)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY languageChanged)
    Q_PROPERTY(QString firstLine READ firstLine WRITE setFirstLine NOTIFY languageChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)   // shown in the status bar
public:
    explicit SyntaxHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const { return m_quickDoc; }
    void setQuickDocument(QQuickTextDocument *doc);
    QString fileName() const { return m_fileName; }
    void setFileName(const QString &name);
    QString firstLine() const { return m_firstLine; }
    void setFirstLine(const QString &line);
    QString language() const { return m_languageName; }

signals:
    void documentChanged();
    void languageChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    enum Lang { Plain, Shell, Python, Yaml, Json, Ini, Nginx, Docker, Js, CLike, Sql, Xml, Markdown };
    struct Rule {
        QRegularExpression re;
        QTextCharFormat fmt;
        int group = 0;
    };
    struct Multi {   // a construct that can span lines: /* */, """ """, <!-- -->
        QRegularExpression start, end;
        QTextCharFormat fmt;
    };
    void detect();
    void build();
    void addKeywords(const QStringList &words, const QTextCharFormat &fmt, bool caseInsensitive = false);
    void add(const QString &pattern, const QTextCharFormat &fmt, int group = 0,
             QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption);

    QPointer<QQuickTextDocument> m_quickDoc;
    QString m_fileName, m_firstLine, m_languageName;
    Lang m_lang = Plain;
    QVector<Rule> m_rules;       // in order: later rules paint over earlier ones
    QVector<Multi> m_multi;
    QTextCharFormat m_comment, m_keyword, m_string, m_number, m_variable, m_key, m_section,
        m_builtin, m_tag, m_heading, m_punct;
};
