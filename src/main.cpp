#include "app/AppController.h"
#include "core/Paths.h"
#include "core/ProxyTunnel.h"
#include "core/SshWorker.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include "core/DepVersions.h"
#include "app/QmlSafety.h"
#include "core/BackgroundThreads.h"
#include <QQmlContext>
#include <QQuickStyle>

#include <libssh/libssh.h>
#include <sodium.h>
#include <cstdio>
#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {
// Everything Qt reports while starting; shown if the window cannot be created
// (a GUI program on Windows has no console, so it would just vanish).
QStringList g_startupLog;
QtMessageHandler g_prevHandler = nullptr;

void collect(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type != QtDebugMsg && g_startupLog.size() < 200)
        g_startupLog << msg;
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

void reportStartupFailure()
{
    const QString file = paths::logsDir() + QStringLiteral("/startup-error.log");
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(g_startupLog.join(QLatin1Char('\n')).toUtf8());
    const QString text = QStringLiteral("ShellWarden could not start its window.\n\n%1\n\nDetails: %2")
                             .arg(g_startupLog.mid(0, 8).join(QLatin1Char('\n')), QDir::toNativeSeparators(file));
#ifdef Q_OS_WIN
    MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(text.utf16()), L"ShellWarden", MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "%s\n", qPrintable(text));
#endif
}
} // namespace

int main(int argc, char *argv[])
{
    g_prevHandler = qInstallMessageHandler(collect);
    QGuiApplication app(argc, argv);
    // No organisation: data goes to %APPDATA%\<AppName> (not <Org>\<AppName>).
    QGuiApplication::setOrganizationName(QString());
    QGuiApplication::setApplicationName(QStringLiteral(APP_NAME_STRING));
    QGuiApplication::setApplicationDisplayName(QStringLiteral(APP_NAME_STRING));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/icon.png")));
    QGuiApplication::setDesktopFileName(QStringLiteral("shellwarden"));   // Linux: matches the .desktop file
    paths::init();
    qRegisterMetaType<ConnectParams>();
    qRegisterMetaType<ForwardSpec>();

    if (sodium_init() < 0)
        return 1;
    ssh_init();
    // a swapped, older libssh / OpenSSL DLL must not be used silently
    if (const QString problem = depversions::runtimeProblem(); !problem.isEmpty()) {
        g_startupLog << problem;
        reportStartupFailure();
        return 1;
    }
    proxytunnel::initSockets();

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    int rc = 0;
    {
        AppController controller;
        controller.applyLanguage(controller.settings().value(QStringLiteral("language")).toString());
        QQmlApplicationEngine engine;
        // no network and no other computers for anything QML loads (QmlSafety.h)
        qmlsafety::install(&engine);
        controller.setEngine(&engine);
        engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                         [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        // The QML module is compiled into the exe under qrc:/ShellWarden. The build
        // folder also has it on disk (found through the qt.conf Qt writes there),
        // but an installed / portable copy only has the embedded one: without this
        // import path it would quit at once without a word.
        engine.addImportPath(QStringLiteral("qrc:/"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        engine.loadFromModule("ShellWarden", "Main");
#else
        engine.load(QUrl(QStringLiteral("qrc:/ShellWarden/qml/Main.qml")));
#endif
        if (engine.rootObjects().isEmpty()) {
            reportStartupFailure();
            return 1;
        }
        qInstallMessageHandler(g_prevHandler);   // started: stop collecting
        g_startupLog.clear();
        // Background threads (Shadowsocks relays, agent bridge, IP checks) stop and
        // are waited for while everything they talk to still exists.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { bgthreads::shutdown(3000); });
        rc = app.exec();
    }
    bgthreads::shutdown(2000);   // relays of sessions closed just now

    ssh_finalize();
    return rc;
}
