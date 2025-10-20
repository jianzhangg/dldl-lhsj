#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QFile>
#include <QPixmap>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QTimer>
#include <QCoreApplication>
#include <QVector>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    appendLog("程序启动");

    connect(ui->btnSelectWindow, &QPushButton::clicked, this, &MainWindow::onSelectWindowClicked);
    connect(ui->btnRunHshj, &QPushButton::clicked, this, &MainWindow::onRunHshjClicked);
#ifdef _WIN32
    s_instance = this;
#endif
}

MainWindow::~MainWindow()
{
#ifdef _WIN32
    if (s_mouseHook) { UnhookWindowsHookEx(s_mouseHook); s_mouseHook = nullptr; }
#endif
    delete ui;
}

void MainWindow::appendLog(const QString &msg)
{
    QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") + msg;
    ui->txtLog->append(line);
    qDebug() << line;
}

void MainWindow::onSelectWindowClicked()
{
    appendLog("进入窗口选择模式：请在目标窗口上点击鼠标左键");
    selectingWindow = true;
#ifdef _WIN32
    // 安装低级鼠标钩子，捕捉任意窗口上的左键
    if (!s_mouseHook) {
        s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
        if (!s_mouseHook) appendLog("安装鼠标钩子失败");
        else appendLog("已安装鼠标钩子");
    }
#endif
}

void MainWindow::onRunHshjClicked()
{
#ifdef _WIN32
    if (!selectedHwnd) {
        appendLog("未选中窗口，无法执行");
        return;
    }
    appendLog("开始截图并执行识别...");
    QImage shot = captureWindow(selectedHwnd);
    if (shot.isNull()) {
        appendLog("截图失败");
        return;
    }
    appendLog(QString("截图完成，尺寸 %1x%2").arg(shot.width()).arg(shot.height()));

    double tmplScore = 0.0;
    QPoint matchPt = runTemplateMatch(shot, tmplScore);
    appendLog(QString("模板匹配坐标: (%1,%2) 评分:%3").arg(matchPt.x()).arg(matchPt.y()).arg(tmplScore, 0, 'f', 4));
    ui->lblMatchResult->setText(QString("模板匹配坐标: (%1,%2) 评分:%3").arg(matchPt.x()).arg(matchPt.y()).arg(tmplScore, 0, 'f', 4));

    QString ocrText;
    QRect ocrRect = runOcrFind(shot, &ocrText);
    if (ocrRect.isValid()) {
        appendLog(QString("OCR坐标: x=%1 y=%2 w=%3 h=%4 文字=%5")
                  .arg(ocrRect.x()).arg(ocrRect.y()).arg(ocrRect.width()).arg(ocrRect.height()).arg(ocrText));
        ui->lblOcrResult->setText(QString("OCR坐标: (%1,%2,%3,%4) 文字:%5").arg(ocrRect.x()).arg(ocrRect.y()).arg(ocrRect.width()).arg(ocrRect.height()).arg(ocrText));
    } else {
        appendLog("OCR未找到‘魂兽幻境’");
        ui->lblOcrResult->setText("OCR坐标: 未找到");
    }
#else
    appendLog("当前平台未实现");
#endif
}

#ifdef _WIN32
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG*>(message);
        if (selectingWindow && msg->message == WM_LBUTTONDOWN) {
            POINT pt; GetCursorPos(&pt);
            HWND hwnd = WindowFromPoint(pt);
            if (hwnd) {
                selectedHwnd = hwnd;
                wchar_t title[256] = {0};
                GetWindowTextW(hwnd, title, 255);
                appendLog(QString("选中窗口: 0x%1 标题:%2")
                          .arg((qulonglong)hwnd, 0, 16)
                          .arg(QString::fromWCharArray(title)));
                ui->lblHandleInfo->setText(QString("句柄: 0x%1").arg((qulonglong)hwnd, 0, 16));
                ui->btnRunHshj->setEnabled(true);
                selectingWindow = false;
                if (s_mouseHook) { UnhookWindowsHookEx(s_mouseHook); s_mouseHook = nullptr; appendLog("已卸载鼠标钩子"); }
                return true;
            }
        }
    }
    return false;
}

QImage MainWindow::captureWindow(HWND hwnd)
{
    RECT rc; if (!GetWindowRect(hwnd, &rc)) return QImage();
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    appendLog(QString("准备捕获窗口区域: x=%1 y=%2 w=%3 h=%4").arg(rc.left).arg(rc.top).arg(width).arg(height));

    HDC hdcWindow = GetWindowDC(hwnd);
    if (!hdcWindow) { appendLog("GetWindowDC失败"); return QImage(); }
    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hbm = CreateCompatibleBitmap(hdcWindow, width, height);
    SelectObject(hdcMem, hbm);

    BOOL ok = BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY | CAPTUREBLT);
    if (!ok) appendLog("BitBlt失败");

    BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QVector<uchar> buffer(width * height * 4);
    GetDIBits(hdcMem, hbm, 0, height, buffer.data(), &bmi, DIB_RGB_COLORS);

    QImage img(buffer.data(), width, height, QImage::Format_ARGB32);
    QImage copy = img.copy();

    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);

    appendLog("窗口截图完成");
    return copy;
}
#endif

#ifdef _WIN32
HHOOK MainWindow::s_mouseHook = nullptr;
MainWindow* MainWindow::s_instance = nullptr;

LRESULT CALLBACK MainWindow::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && s_instance && s_instance->selectingWindow) {
        if (wParam == WM_LBUTTONDOWN) {
            MSLLHOOKSTRUCT *hook = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            POINT pt = hook->pt;
            HWND hwnd = WindowFromPoint(pt);
            if (hwnd) {
                s_instance->selectedHwnd = hwnd;
                wchar_t title[256] = {0};
                GetWindowTextW(hwnd, title, 255);
                s_instance->appendLog(QString("钩子选中窗口: 0x%1 标题:%2")
                    .arg((qulonglong)hwnd, 0, 16)
                    .arg(QString::fromWCharArray(title)));
                s_instance->ui->lblHandleInfo->setText(QString("句柄: 0x%1").arg((qulonglong)hwnd, 0, 16));
                s_instance->ui->btnRunHshj->setEnabled(true);
                s_instance->selectingWindow = false;
                if (s_mouseHook) { UnhookWindowsHookEx(s_mouseHook); s_mouseHook = nullptr; s_instance->appendLog("已卸载鼠标钩子"); }
                return 1; // 拦截此次点击
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif

cv::Mat MainWindow::qimageToMat(const QImage &img)
{
    QImage swapped = img.convertToFormat(QImage::Format_RGBA8888);
    return cv::Mat(swapped.height(), swapped.width(), CV_8UC4,
                   const_cast<uchar*>(swapped.bits()), swapped.bytesPerLine()).clone();
}

QImage MainWindow::loadTemplateImage()
{
    // 优先从资源加载，其次从可执行目录的 assets
    QImage res(":/assets/hshj.png");
    if (!res.isNull()) return res;
    QString path = QCoreApplication::applicationDirPath() + "/assets/hshj.png";
    return QImage(path);
}

QPoint MainWindow::runTemplateMatch(const QImage &screenshot, double &scoreOut)
{
    QImage tmplImg = loadTemplateImage();
    if (tmplImg.isNull()) {
        appendLog("模板图片加载失败: assets/hshj.png");
        scoreOut = 0.0;
        return QPoint(-1, -1);
    }
    appendLog(QString("模板图片尺寸 %1x%2").arg(tmplImg.width()).arg(tmplImg.height()));

    cv::Mat srcRGBA = qimageToMat(screenshot);
    cv::Mat templRGBA = qimageToMat(tmplImg);

    // 分离 alpha 作为掩膜（透明度匹配），并转为二值掩膜
    cv::Mat alpha, mask;
    if (templRGBA.channels() == 4) {
        std::vector<cv::Mat> ch;
        cv::split(templRGBA, ch);
        alpha = ch[3];
        cv::threshold(alpha, mask, 10, 255, cv::THRESH_BINARY);
    }

    // 统一转为 BGR 三通道参与匹配
    cv::Mat src3, templ3;
    cv::cvtColor(srcRGBA, src3, cv::COLOR_RGBA2BGR);
    cv::cvtColor(templRGBA, templ3, cv::COLOR_RGBA2BGR);

    int resultCols = src3.cols - templ3.cols + 1;
    int resultRows = src3.rows - templ3.rows + 1;
    if (resultCols <= 0 || resultRows <= 0) {
        scoreOut = 0.0;
        return QPoint(-1, -1);
    }
    cv::Mat result(resultRows, resultCols, CV_32FC1);

    if (!mask.empty()) {
        cv::matchTemplate(src3, templ3, result, cv::TM_CCORR_NORMED, mask);
    } else {
        cv::matchTemplate(src3, templ3, result, cv::TM_CCOEFF_NORMED);
    }

    double minVal, maxVal; cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    scoreOut = maxVal;
    appendLog(QString("模板匹配得分 max=%1").arg(maxVal, 0, 'f', 4));
    return QPoint(maxLoc.x, maxLoc.y);
}

QRect MainWindow::runOcrFind(const QImage &screenshot, QString *recognizedOut)
{
    tesseract::TessBaseAPI api;
    // 使用中文简体，若未安装会回落英文
    if (api.Init(nullptr, "chi_sim")) {
        appendLog("Tesseract初始化失败，尝试英文");
        if (api.Init(nullptr, "eng")) {
            appendLog("Tesseract英文也失败");
            return QRect();
        }
    }
    api.SetPageSegMode(tesseract::PSM_AUTO);
    api.SetVariable("user_defined_dpi", "96");

    QImage gray = screenshot.convertToFormat(QImage::Format_Grayscale8);
    Pix *pix = pixCreate(gray.width(), gray.height(), 8);
    // 将 grayscale 填充到 Pix 数据
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            pixSetPixel(pix, x, y, line[x]);
        }
    }
    api.SetImage(pix);
    char *outText = api.GetUTF8Text();
    QString text = QString::fromUtf8(outText ? outText : "").simplified();
    if (recognizedOut) *recognizedOut = text;
    appendLog(QString("OCR全文:%1").arg(text.left(80)));

    // 获取每个块，寻找包含“魂兽幻境”的区域
    api.Recognize(0);
    QRect found;
    // 第一轮：以词为单位查找
    {
        tesseract::ResultIterator *ri = api.GetIterator();
        tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
        if (ri) {
            do {
                const char *word = ri->GetUTF8Text(level);
                float conf = ri->Confidence(level);
                int x1, y1, x2, y2;
                ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                QString w = QString::fromUtf8(word ? word : "");
                if (word) delete [] word;
                if (w.contains("魂") || w.contains("兽") || w.contains("幻") || w.contains("境")) {
                    appendLog(QString("OCR片段(词): '%1' conf=%2 box=(%3,%4,%5,%6)")
                              .arg(w).arg(conf, 0, 'f', 1).arg(x1).arg(y1).arg(x2).arg(y2));
                }
                if (w.contains("魂兽幻境")) {
                    found = QRect(QPoint(x1, y1), QPoint(x2, y2));
                    break;
                }
            } while (ri->Next(level));
        }
    }

    // 第二轮：以字符为单位，寻找连续的“魂”“兽”“幻”“境”
    if (!found.isValid()) {
        tesseract::ResultIterator *ri2 = api.GetIterator();
        tesseract::PageIteratorLevel level2 = tesseract::RIL_SYMBOL;
        int stage = 0; // 0->魂,1->兽,2->幻,3->境
        QRect accum;
        if (ri2) {
            do {
                const char *sym = ri2->GetUTF8Text(level2);
                float conf = ri2->Confidence(level2);
                int x1, y1, x2, y2;
                ri2->BoundingBox(level2, &x1, &y1, &x2, &y2);
                QString s = QString::fromUtf8(sym ? sym : "").trimmed();
                if (sym) delete [] sym;
                if (s.isEmpty()) continue;
                if (s.contains("魂") || s.contains("兽") || s.contains("幻") || s.contains("境")) {
                    appendLog(QString("OCR片段(字): '%1' conf=%2 box=(%3,%4,%5,%6) stage=%7")
                              .arg(s).arg(conf, 0, 'f', 1).arg(x1).arg(y1).arg(x2).arg(y2).arg(stage));
                }
                const QChar target[4] = { QChar(u'魂'), QChar(u'兽'), QChar(u'幻'), QChar(u'境') };
                if (s.contains(target[stage])) {
                    QRect r(QPoint(x1, y1), QPoint(x2, y2));
                    accum = stage == 0 ? r : accum.united(r);
                    stage++;
                    if (stage == 4) { found = accum; break; }
                } else {
                    // 允许重新开始匹配
                    if (s.contains(QChar(u'魂'))) { stage = 1; accum = QRect(QPoint(x1, y1), QPoint(x2, y2)); }
                    else { stage = 0; accum = QRect(); }
                }
            } while (ri2->Next(level2));
        }
    }
    if (outText) delete [] outText;
    api.End();
    pixDestroy(&pix);
    return found;
}
