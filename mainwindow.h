#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QDateTime>
#include <QDebug>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace cv { class Mat; }

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
#ifdef _WIN32
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
    Ui::MainWindow *ui;

    // 状态
#ifdef _WIN32
    HWND selectedHwnd = nullptr;
    static HHOOK s_mouseHook;
    static MainWindow* s_instance;
#endif
    bool selectingWindow = false;

    // 日志
    void appendLog(const QString &msg);

    // 交互
    void onSelectWindowClicked();
    void onRunHshjClicked();

    // 截图与识别
#ifdef _WIN32
    QImage captureWindow(HWND hwnd);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif
    static cv::Mat qimageToMat(const QImage &img);
    static QImage loadTemplateImage();
    QPoint runTemplateMatch(const QImage &screenshot, double &scoreOut);
    QRect runOcrFind(const QImage &screenshot, QString *recognizedOut = nullptr);
};
#endif // MAINWINDOW_H
