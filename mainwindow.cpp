#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QFile>
#include <QPixmap>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QTimer>
#include <QPainter>
#include <QPen>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QVector>
#include <QThread>
#include <QMetaObject>
#include <QStandardPaths>
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
    if (QThread::currentThread() == this->thread()) {
        ui->txtLog->append(line);
    } else {
        QString copy = line;
        QMetaObject::invokeMethod(this, [this, copy]() { ui->txtLog->append(copy); }, Qt::QueuedConnection);
    }
    // 向 Qt Creator Application Output 以 UTF-8 编码输出
    // 需与 IDE 中 Environment->Interface->Text codec for tools = UTF-8 保持一致
    QTextStream ts(stdout);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    ts.setEncoding(QStringConverter::Utf8);
#else
    ts.setCodec("UTF-8");
#endif
    ts << line << '\n';
    ts.flush();

#ifdef _WIN32
    // 同时写入调试器，便于在 DebugView 等工具查看
    std::wstring ws = line.toStdWString();
    ws.push_back(L'\n');
    OutputDebugStringW(ws.c_str());
#endif
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
    lastScreenshot = shot;

    // 模板匹配改为异步，防止鼠标转圈
    appendLog("启动异步模板匹配");
    QImage shotCopyForTmpl = shot.copy();
    auto tmplTask = [this](QImage img) -> TemplateResult {
        double score = 0.0;
        QPoint pt = runTemplateMatch(img, score);
        return TemplateResult{pt, score};
    };
    futureTmpl = QtConcurrent::run(tmplTask, shotCopyForTmpl);
    connect(&watcherTmpl, &QFutureWatcher<TemplateResult>::finished, this, &MainWindow::onTemplateFinished, Qt::UniqueConnection);
    watcherTmpl.setFuture(futureTmpl);

    // 异步执行双语言 OCR，避免卡 UI
    appendLog("启动异步OCR任务[fast] 与 [accuracy]");
    QImage shotCopy = shot.copy();
    auto ocrTask = [this](QImage img, QString lang) -> OcrResult {
        QString text;
        QRect rect = runOcrFindWithLang(img, lang, &text);
        OcrResult r{rect, text, lang};
        return r;
    };
    futureFast = QtConcurrent::run(ocrTask, shotCopy, QStringLiteral("chi_sim_fast"));
    futureAcc  = QtConcurrent::run(ocrTask, shotCopy, QStringLiteral("chi_sim_accuracy"));
    connect(&watcherFast, &QFutureWatcher<OcrResult>::finished, this, &MainWindow::onOcrFinished, Qt::UniqueConnection);
    connect(&watcherAcc,  &QFutureWatcher<OcrResult>::finished, this, &MainWindow::onOcrFinished, Qt::UniqueConnection);
    watcherFast.setFuture(futureFast);
    watcherAcc.setFuture(futureAcc);

    // 截图与模板匹配结果先展示，OCR结果回调里再补充
    showScreenshotWithMarks(shot, QPoint(-1,-1), QRect(), QRect());
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
    if (res.isNull()) {
        QString path = QCoreApplication::applicationDirPath() + "/assets/hshj.png";
        res.load(path);
    }
    if (res.isNull()) appendLog("模板图片仍未找到: :/assets/hshj.png 或 程序目录/assets/hshj.png");
    return res;
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

    // 分离 alpha 作为掩膜
    cv::Mat alpha;
    if (templRGBA.channels() == 4) {
        std::vector<cv::Mat> ch; cv::split(templRGBA, ch); alpha = ch[3];
    }

    cv::Mat src3; cv::cvtColor(srcRGBA, src3, cv::COLOR_RGBA2BGR);

    // 多尺度匹配：从1.0降到0.4
    double bestScore = -1.0; cv::Point bestLoc(0,0); double bestScale = 1.0; cv::Size bestSize;
    for (double scale = 1.0; scale >= 0.4; scale -= 0.1) {
        cv::Mat templScaledRGBA, templ3, mask;
        cv::resize(templRGBA, templScaledRGBA, cv::Size(), scale, scale, cv::INTER_AREA);
        if (templScaledRGBA.cols <= 1 || templScaledRGBA.rows <= 1) continue;
        cv::cvtColor(templScaledRGBA, templ3, cv::COLOR_RGBA2BGR);
        if (!alpha.empty()) {
            cv::Mat alphaScaled; cv::resize(alpha, alphaScaled, cv::Size(), scale, scale, cv::INTER_AREA);
            cv::threshold(alphaScaled, mask, 10, 255, cv::THRESH_BINARY);
        }

        int cols = src3.cols - templ3.cols + 1;
        int rows = src3.rows - templ3.rows + 1;
        if (cols <= 0 || rows <= 0) {
            appendLog(QString("跳过尺度%1：模板大于截图 (%2x%3)>").arg(scale,0,'f',1).arg(templ3.cols).arg(templ3.rows));
            continue;
        }
        cv::Mat result(rows, cols, CV_32FC1);
        if (!mask.empty()) cv::matchTemplate(src3, templ3, result, cv::TM_CCORR_NORMED, mask);
        else cv::matchTemplate(src3, templ3, result, cv::TM_CCOEFF_NORMED);
        double minVal, maxVal; cv::Point minLoc, maxLoc; cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
        appendLog(QString("尺度%1 匹配得分=%2 位置=(%3,%4) 模板(%5x%6)")
                  .arg(scale,0,'f',1).arg(maxVal,0,'f',4).arg(maxLoc.x).arg(maxLoc.y).arg(templ3.cols).arg(templ3.rows));
        if (maxVal > bestScore) { bestScore = maxVal; bestLoc = maxLoc; bestScale = scale; bestSize = templ3.size(); }
    }

    scoreOut = (bestScore < 0 ? 0.0 : bestScore);
    appendLog(QString("模板匹配最佳：score=%1 scale=%2 size=%3x%4")
              .arg(scoreOut,0,'f',4).arg(bestScale,0,'f',2).arg(bestSize.width).arg(bestSize.height));
    if (bestScore < 0) return QPoint(-1,-1);
    return QPoint(bestLoc.x, bestLoc.y);
}

QRect MainWindow::runOcrFind(const QImage &screenshot, QString *recognizedOut)
{
    auto testTessdata = [&](const QString &dir) -> bool {
        return QFileInfo(QDir(dir).filePath("chi_sim.traineddata")).exists();
    };
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << qEnvironmentVariable("TESSDATA_PREFIX");
    candidates << QDir(appDir).filePath("tessdata");
    candidates << QDir(appDir).filePath("share/tessdata");
    candidates << QDir(appDir).filePath("share/tesseract/tessdata");
    // vcpkg 默认安装位置（构建目录旁）
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath("../default/vcpkg_installed/x64-windows/share/tesseract/tessdata");
    // 常见系统环境变量
    candidates << QDir(qEnvironmentVariable("VCPKG_ROOT")).filePath("installed/x64-windows/share/tesseract/tessdata");
    QString chosen;
    for (const QString &c : candidates) { if (!c.isEmpty() && testTessdata(c)) { chosen = c; break; } }
    if (!chosen.isEmpty()) {
        appendLog(QString("检测到tessdata目录: %1").arg(chosen));
        qputenv("TESSDATA_PREFIX", chosen.toUtf8());
    } else {
        appendLog("未检测到chi_sim.traineddata，请确认放置在程序目录tessdata/或设置TESSDATA_PREFIX");
    }

    tesseract::TessBaseAPI api;
    // 明确指定路径，优先中文
    const char *datapath = chosen.isEmpty() ? nullptr : chosen.toUtf8().constData();
    if (api.Init(datapath, "chi_sim")) {
        appendLog("Tesseract初始化失败(chi_sim)，尝试英文");
        if (api.Init(datapath, "eng")) {
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

QRect MainWindow::runOcrFindWithLang(const QImage &screenshot, const QString &langCode, QString *recognizedOut)
{
    appendLog(QString("[OCR] 进入 runOcrFindWithLang, lang=%1").arg(langCode));

    auto testTessdataForLang = [&](const QString &dir, const QString &lang) -> QString {
        if (dir.isEmpty()) return QString();
        // 优先匹配同名变体文件
        QString variantFile = QDir(dir).filePath(lang + ".traineddata");
        if (QFileInfo(variantFile).exists()) return variantFile;
        // 兼容标准文件名
        if (lang.startsWith("chi_sim")) {
            QString stdFile = QDir(dir).filePath("chi_sim.traineddata");
            if (QFileInfo(stdFile).exists()) return stdFile;
        }
        return QString();
    };

    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << qEnvironmentVariable("TESSDATA_PREFIX");
    candidates << QDir(appDir).filePath("tessdata");
    candidates << QDir(appDir).filePath("../tessdata");
    candidates << QDir(appDir).filePath("../../tessdata");
    candidates << QDir(appDir).filePath("share/tessdata");
    candidates << QDir(appDir).filePath("share/tesseract/tessdata");
    // vcpkg 默认安装位置（构建目录旁）
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath("../default/vcpkg_installed/x64-windows/share/tesseract/tessdata");
    // 常见系统环境变量
    candidates << QDir(qEnvironmentVariable("VCPKG_ROOT")).filePath("installed/x64-windows/share/tesseract/tessdata");
    QString foundFile;
    QString chosen;
    for (const QString &c : candidates) {
        QString f = testTessdataForLang(c, langCode);
        appendLog(QString("[OCR] 探测目录: %1 => %2").arg(c, f.isEmpty()?"未找到":f));
        if (!f.isEmpty()) { chosen = c; foundFile = f; break; }
    }
    if (chosen.isEmpty()) {
        appendLog(QString("未检测到%1(.traineddata)，请将其放在程序目录tessdata/或设置TESSDATA_PREFIX").arg(langCode));
        return QRect();
    }

    // 构造 datapath 与 lang 参数
    QString datapath = chosen;
    QString langParam = QStringLiteral("chi_sim");
    bool needAlias = false;
    if (QFileInfo(foundFile).fileName() == QStringLiteral("chi_sim.traineddata")) {
        // 使用标准命名
        needAlias = false;
    } else {
        needAlias = true;
    }

    if (needAlias) {
        // 将变体文件复制到临时别名目录，命名为 chi_sim.traineddata
        QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString sub = langCode.contains("accuracy") ? "dldl-lhsj-tess-acc" : "dldl-lhsj-tess-fast";
        QString aliasDir = QDir(base).filePath(sub);
        QDir().mkpath(aliasDir);
        QString aliasFile = QDir(aliasDir).filePath("chi_sim.traineddata");
        bool needCopy = true;
        if (QFileInfo::exists(aliasFile)) {
            if (QFileInfo(aliasFile).size() == QFileInfo(foundFile).size()) needCopy = false;
            else QFile::remove(aliasFile);
        }
        if (needCopy) {
            appendLog(QString("[OCR] 拷贝模型到别名目录: %1 -> %2").arg(foundFile, aliasFile));
            if (!QFile::copy(foundFile, aliasFile)) {
                appendLog("[OCR] 拷贝失败，无法创建别名文件 chi_sim.traineddata");
                return QRect();
            }
        }
        datapath = aliasDir;
    }
    appendLog(QString("[OCR] 使用datapath=%1, lang=%2, 源文件=%3").arg(datapath, langParam, foundFile));
    qputenv("TESSDATA_PREFIX", datapath.toUtf8());

    tesseract::TessBaseAPI api;
    const QByteArray dpUtf8 = datapath.toUtf8();
    if (api.Init(dpUtf8.constData(), langParam.toUtf8().constData())) {
        appendLog(QString("Tesseract初始化失败(%1)" ).arg(langCode));
        return QRect();
    }
    api.SetPageSegMode(tesseract::PSM_AUTO);
    api.SetVariable("user_defined_dpi", "96");

    QImage gray = screenshot.convertToFormat(QImage::Format_Grayscale8);
    Pix *pix = pixCreate(gray.width(), gray.height(), 8);
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
    appendLog(QString("[%1] OCR全文:%2").arg(langCode, text.left(80)));

    api.Recognize(0);
    QRect found;
    // 第一轮：词级
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
                if (w.contains("魂兽幻境")) {
                    found = QRect(QPoint(x1, y1), QPoint(x2, y2));
                    break;
                }
                if (w.contains("魂") || w.contains("兽") || w.contains("幻") || w.contains("境")) {
                    appendLog(QString("[%1] 词:'%2' conf=%3 box=(%4,%5,%6,%7)")
                              .arg(langCode).arg(w).arg(conf, 0, 'f', 1).arg(x1).arg(y1).arg(x2).arg(y2));
                }
            } while (ri->Next(level));
        }
    }
    // 第二轮：字级连续匹配
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
                const QChar target[4] = { QChar(u'魂'), QChar(u'兽'), QChar(u'幻'), QChar(u'境') };
                if (s.contains(target[stage])) {
                    QRect r(QPoint(x1, y1), QPoint(x2, y2));
                    accum = stage == 0 ? r : accum.united(r);
                    stage++;
                    if (stage == 4) { found = accum; break; }
                } else {
                    if (s.contains(QChar(u'魂'))) { stage = 1; accum = QRect(QPoint(x1, y1), QPoint(x2, y2)); }
                    else { stage = 0; accum = QRect(); }
                }
                if (s.contains("魂") || s.contains("兽") || s.contains("幻") || s.contains("境")) {
                    appendLog(QString("[%1] 字:'%2' conf=%3 box=(%4,%5,%6,%7) stage=%8")
                              .arg(langCode).arg(s).arg(conf, 0, 'f', 1).arg(x1).arg(y1).arg(x2).arg(y2).arg(stage));
                }
            } while (ri2->Next(level2));
        }
    }

    if (outText) delete [] outText;
    api.End();
    pixDestroy(&pix);
    return found;
}

void MainWindow::showScreenshotWithMarks(const QImage &shot, const QPoint &tmplPt, const QRect &ocrRectFast, const QRect &ocrRectAcc)
{
    QImage canvas = shot.convertToFormat(QImage::Format_RGBA8888);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);

    // 绘制模板匹配点
    if (tmplPt.x() >= 0 && tmplPt.y() >= 0) {
        p.setPen(QPen(Qt::green, 3));
        p.drawEllipse(QPoint(tmplPt.x(), tmplPt.y()), 10, 10);
    }

    // 绘制OCR矩形（fast 红色, accuracy 蓝色）
    if (ocrRectFast.isValid()) {
        p.setPen(QPen(Qt::red, 3));
        p.drawRect(ocrRectFast);
    }
    if (ocrRectAcc.isValid()) {
        p.setPen(QPen(Qt::blue, 3));
        p.drawRect(ocrRectAcc);
    }
    p.end();

    // 固定展示宽度最大300，等比例缩放
    int targetW = 300;
    int w = canvas.width();
    int h = canvas.height();
    if (w > targetW) {
        int targetH = (int)((double)targetW / w * h);
        QImage scaled = canvas.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->lblScreenshot->setPixmap(QPixmap::fromImage(scaled));
    } else {
        ui->lblScreenshot->setPixmap(QPixmap::fromImage(canvas));
    }
    appendLog("已在UI展示截图与标记");
}

void MainWindow::onOcrFinished()
{
    // 当任一 OCR 完成时，汇总两个任务的结果并刷新 UI
    bool fastReady = watcherFast.isFinished();
    bool accReady  = watcherAcc.isFinished();
    if (!fastReady && !accReady) return;

    QString label;
    QRect rectFast, rectAcc;
    QString textFast, textAcc;

    if (fastReady) {
        OcrResult r = futureFast.result();
        rectFast = r.rect; textFast = r.text;
        if (r.rect.isValid()) appendLog(QString("[fast] OCR坐标: (%1,%2,%3,%4) 文本:%5")
            .arg(r.rect.x()).arg(r.rect.y()).arg(r.rect.width()).arg(r.rect.height()).arg(r.text));
        else appendLog("[fast] OCR未找到‘魂兽幻境’");
    }
    if (accReady) {
        OcrResult r = futureAcc.result();
        rectAcc = r.rect; textAcc = r.text;
        if (r.rect.isValid()) appendLog(QString("[accuracy] OCR坐标: (%1,%2,%3,%4) 文本:%5")
            .arg(r.rect.x()).arg(r.rect.y()).arg(r.rect.width()).arg(r.rect.height()).arg(r.text));
        else appendLog("[accuracy] OCR未找到‘魂兽幻境’");
    }

    label += rectFast.isValid() ? QString("[fast] (%1,%2,%3,%4) 文本:%5\n").arg(rectFast.x()).arg(rectFast.y()).arg(rectFast.width()).arg(rectFast.height()).arg(textFast)
                                : QString("[fast] 未找到\n");
    label += rectAcc.isValid()  ? QString("[accuracy] (%1,%2,%3,%4) 文本:%5").arg(rectAcc.x()).arg(rectAcc.y()).arg(rectAcc.width()).arg(rectAcc.height()).arg(textAcc)
                                : QString("[accuracy] 未找到");
    if (QThread::currentThread() == this->thread()) {
        ui->lblOcrResult->setText(label);
    } else {
        QString copy = label;
        QMetaObject::invokeMethod(this, [this, copy]() { ui->lblOcrResult->setText(copy); }, Qt::QueuedConnection);
    }

    // 使用缓存的截图重绘 OCR 标注，并缓存 OCR 框供模板回调使用
    if (!lastScreenshot.isNull()) {
        lastRectFast = rectFast;
        lastRectAcc  = rectAcc;
        showScreenshotWithMarks(lastScreenshot, lastTemplatePt, rectFast, rectAcc);
    }
}

void MainWindow::onTemplateFinished()
{
    TemplateResult r = futureTmpl.result();
    lastTemplatePt = r.pt;
    lastTemplateScore = r.score;
    appendLog(QString("模板匹配坐标: (%1,%2) 评分:%3")
              .arg(r.pt.x()).arg(r.pt.y()).arg(r.score, 0, 'f', 4));
    QString label = QString("模板匹配坐标: (%1,%2) 评分:%3").arg(r.pt.x()).arg(r.pt.y()).arg(r.score, 0, 'f', 4);
    if (QThread::currentThread() == this->thread()) {
        ui->lblMatchResult->setText(label);
    } else {
        QString copy = label;
        QMetaObject::invokeMethod(this, [this, copy]() { ui->lblMatchResult->setText(copy); }, Qt::QueuedConnection);
    }
    if (!lastScreenshot.isNull()) {
        showScreenshotWithMarks(lastScreenshot, lastTemplatePt, lastRectFast, lastRectAcc);
    }
}
