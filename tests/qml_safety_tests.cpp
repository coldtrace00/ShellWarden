// Text from servers must never make the interface load anything from the network
// or from another computer (see src/app/QmlSafety.h). Runs offscreen:
//   QT_QPA_PLATFORM=offscreen ./qml_safety_tests
#include "app/QmlSafety.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>
#include <memory>

static int g_fail = 0;
#define CHECK(cond, name)                                                         \
    do {                                                                          \
        if (cond) std::printf("  ok   %s\n", name);                               \
        else { std::printf("  FAIL %s\n", name); ++g_fail; }                      \
    } while (0)

namespace {

// Counts every connection made to it.
struct Canary
{
    QTcpServer server;
    int hits = 0;
    Canary()
    {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) { ++hits; s->close(); s->deleteLater(); }
        });
    }
    QString url(const char *name) const
    {
        return QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(QLatin1String(name));
    }
};

void spin(int ms)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// Builds a window with the given QML body, shows it, lets loads happen.
std::unique_ptr<QObject> show(QQmlEngine &engine, const QString &body)
{
    const QByteArray qml = QStringLiteral(
        "import QtQuick\nimport QtQuick.Controls\nimport QtQuick.Window\n"
        "Window { width: 400; height: 300; visible: true\n%1\n}\n").arg(body).toUtf8();
    QQmlComponent c(&engine);
    c.setData(qml, QUrl(QStringLiteral("qrc:/test/Window.qml")));
    std::unique_ptr<QObject> o(c.create());
    if (!o)
        std::printf("       QML error: %s\n", qPrintable(c.errorString()));
    spin(600);
    return o;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    std::printf("[allowed urls]\n");
    {
        using qmlsafety::allowed;
        CHECK(allowed(QUrl("qrc:/ShellWarden/qml/Main.qml")), "qrc");
        CHECK(allowed(QUrl("data:image/png;base64,AAAA")), "data:");
        CHECK(allowed(QUrl("file:///home/user/x.png")), "local file (Linux)");
        CHECK(allowed(QUrl("file:///C:/Users/x/y.png")), "local file (Windows)");
        CHECK(allowed(QUrl::fromLocalFile("/tmp/a b.png")), "local file with a space");
        CHECK(!allowed(QUrl("http://example.com/x.png")), "http refused");
        CHECK(!allowed(QUrl("https://example.com/x.png")), "https refused");
        CHECK(!allowed(QUrl("ftp://example.com/x")), "ftp refused");
        CHECK(!allowed(QUrl("file://attacker/share/x.png")), "file://host/… (UNC) refused");
        CHECK(!allowed(QUrl("file://localhost/etc/x.png")), "file://localhost/… refused");
        CHECK(!allowed(QUrl("file:////attacker/share/x.png")), "file:////host/… refused");
        CHECK(!allowed(QUrl("file:///%5C%5Cattacker%5Cshare%5Cx.png")), "file:///\\\\host\\… refused");
        CHECK(!allowed(QUrl("file:///\\\\attacker\\share\\x.png")), "file:///\\\\host (raw) refused");
        CHECK(!allowed(QUrl("smb://attacker/share")), "smb refused");
        CHECK(!allowed(QUrl("blocked:")), "unknown scheme refused");
    }

    // A control run without the protection proves the test can see a load.
    std::printf("[control: unprotected engine]\n");
    {
        Canary canary;
        QQmlEngine engine;
        auto w = show(engine, QStringLiteral(
            "Text { textFormat: Text.AutoText; text: \"<b>x</b><img src='%1'>\" }").arg(canary.url("control.png")));
        CHECK(w && canary.hits > 0, "rich text without protection does load (the test works)");
    }

    QTemporaryDir tmp;
    std::printf("[protected engine]\n");
    {
        Canary canary;
        QQmlEngine engine;
        qmlsafety::install(&engine);
        qmlsafety::install(&engine);   // twice must be harmless
        const int before = qmlsafety::blockedCount();
        auto w = show(engine, QStringLiteral(
            "Text { textFormat: Text.AutoText; text: \"<b>x</b><img src='%1'>\" }\n"
            "Text { y: 40; textFormat: Text.RichText; text: \"<img src='%2'>\" }\n"
            "Text { y: 80; textFormat: Text.StyledText; text: \"<img src='%3'>\" }\n"
            "Image { y: 120; source: '%4' }\n"
            "Image { y: 160; source: 'file://127.0.0.1/share/x.png' }\n"
            "Image { y: 200; source: 'file:////127.0.0.1/share/z.png' }\n")
                .arg(canary.url("auto.png"), canary.url("rich.png"), canary.url("styled.png"), canary.url("image.png")));
        CHECK(w != nullptr, "window created");
        CHECK(canary.hits == 0, qPrintable(QStringLiteral("no network loads from rich text or Image (%1)").arg(canary.hits)));
        CHECK(qmlsafety::blockedCount() - before >= 4,
              qPrintable(QStringLiteral("remote and UNC loads were refused (%1)").arg(qmlsafety::blockedCount() - before)));
        // local files still work
        const QString png = tmp.path() + QStringLiteral("/ok.png");
        {
            QImage img(4, 4, QImage::Format_ARGB32); img.fill(Qt::red); img.save(png);
        }
        auto w2 = show(engine, QStringLiteral("Image { objectName: 'local'; source: '%1' }")
                                   .arg(QUrl::fromLocalFile(png).toString()));
        QObject *local = w2 ? w2->findChild<QObject *>(QStringLiteral("local")) : nullptr;
        CHECK(local && local->property("status").toInt() == 1, "local images still load");
    }

    // A known LIMIT, kept here so it is not forgotten: an <img src='file://server/…'>
    // inside RichText or StyledText is read by Qt as a local file without asking
    // the URL interceptor (StyledText passes only its base URL to it, RichText
    // gives local files to QTextDocument directly — also in current Qt). On
    // Windows that path is a UNC share. Only plain text protects there: every text
    // in qml/ is plain (tests/check_qml_text.py) and so is the shared tooltip.
    // This check expects the load to HAPPEN; if a future Qt starts to block it, it
    // fails and the documentation can say more. Not run on Windows, where it
    // would really open an SMB connection.
#ifndef _WIN32
    std::printf("[known limit: rich text reads file://server/… past the interceptor]\n");
    {
        static QStringList warnings;
        warnings.clear();
        const QtMessageHandler prev = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) {
            warnings << m;
        });
        QQmlEngine engine;
        qmlsafety::install(&engine);
        auto w = show(engine, QStringLiteral(
            "Text { textFormat: Text.RichText; text: \"<img src='file://127.0.0.1/share/rich-unc.png'>\" }\n"
            "Text { y: 40; textFormat: Text.StyledText; text: \"<img src='file://127.0.0.1/share/styled-unc.png'>\" }\n"));
        qInstallMessageHandler(prev);
        const QString all = warnings.join(QLatin1Char('\n'));
        CHECK(w && all.contains(QLatin1String("rich-unc.png")) && all.contains(QLatin1String("styled-unc.png")),
              "still true: RichText and StyledText try to open it (so plain text everywhere is required)");
    }
#endif

    std::printf("[tooltips]\n");
    {
        Canary canary;
        QQmlEngine engine;
        qmlsafety::install(&engine);
        const QString tipFile = QStringLiteral(SW_QML_DIR "/components/PlainToolTips.qml");
        auto w = show(engine, QStringLiteral(
            "Loader { id: fix; source: '%1' }\n"
            "Rectangle { id: target; width: 50; height: 50; color: 'red'\n"
            "  ToolTip.visible: true; ToolTip.delay: 0\n"
            "  ToolTip.text: \"The HTTP proxy refused: <b>x</b><img src='%2'>\" }\n"
            "property int fmt: target.ToolTip.toolTip.contentItem.textFormat\n")
                .arg(QUrl::fromLocalFile(tipFile).toString(), canary.url("tip.png")));
        CHECK(w != nullptr, "window with a tooltip created");
        CHECK(w && w->property("fmt").toInt() == 0, qPrintable(QStringLiteral("shared tooltip shows plain text (format %1)")
                                                                .arg(w ? w->property("fmt").toInt() : -1)));
        CHECK(canary.hits == 0, "tooltip text loads nothing");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
