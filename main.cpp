#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QTextStream>
#include <QDebug>
#include <QDateTime>
#include <QLoggingCategory>

// 全局 UTF-8 日志：将 Qt 的 qDebug/qInfo/qWarning/... 统一写为 UTF-8 到 stdout/stderr
static void utf8MessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx);
    static QTextStream tsOut(stdout);
    static QTextStream tsErr(stderr);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    static bool inited = [](){ tsOut.setEncoding(QStringConverter::Utf8); tsErr.setEncoding(QStringConverter::Utf8); return true; }();
    Q_UNUSED(inited);
#else
    tsOut.setCodec("UTF-8");
    tsErr.setCodec("UTF-8");
#endif
    const QString prefix = (type==QtDebugMsg?"[D] ": type==QtInfoMsg?"[I] ": type==QtWarningMsg?"[W] ": type==QtCriticalMsg?"[E] ": "[F] ");
    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") + prefix + msg;
    QTextStream &ts = (type>=QtWarningMsg) ? tsErr : tsOut;
    ts << line << '\n';
    ts.flush();
#ifdef _WIN32
    std::wstring ws = line.toStdWString();
    ws.push_back(L'\n');
    OutputDebugStringW(ws.c_str());
#endif
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 安装全局 UTF-8 日志处理器
    qInstallMessageHandler(utf8MessageHandler);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "dldl-lhsj_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return a.exec();
}
