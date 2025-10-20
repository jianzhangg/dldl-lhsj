/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.9.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *topLayout;
    QPushButton *btnSelectWindow;
    QLabel *lblHandleInfo;
    QPushButton *btnRunHshj;
    QHBoxLayout *resultLayout;
    QLabel *lblMatchResult;
    QLabel *lblOcrResult;
    QLabel *lblScreenshot;
    QTextEdit *txtLog;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(800, 600);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName("verticalLayout");
        topLayout = new QHBoxLayout();
        topLayout->setObjectName("topLayout");
        btnSelectWindow = new QPushButton(centralwidget);
        btnSelectWindow->setObjectName("btnSelectWindow");

        topLayout->addWidget(btnSelectWindow);

        lblHandleInfo = new QLabel(centralwidget);
        lblHandleInfo->setObjectName("lblHandleInfo");

        topLayout->addWidget(lblHandleInfo);

        btnRunHshj = new QPushButton(centralwidget);
        btnRunHshj->setObjectName("btnRunHshj");
        btnRunHshj->setEnabled(false);

        topLayout->addWidget(btnRunHshj);


        verticalLayout->addLayout(topLayout);

        resultLayout = new QHBoxLayout();
        resultLayout->setObjectName("resultLayout");
        lblMatchResult = new QLabel(centralwidget);
        lblMatchResult->setObjectName("lblMatchResult");

        resultLayout->addWidget(lblMatchResult);

        lblOcrResult = new QLabel(centralwidget);
        lblOcrResult->setObjectName("lblOcrResult");

        resultLayout->addWidget(lblOcrResult);


        verticalLayout->addLayout(resultLayout);

        lblScreenshot = new QLabel(centralwidget);
        lblScreenshot->setObjectName("lblScreenshot");
        lblScreenshot->setMinimumSize(QSize(360, 140));
        lblScreenshot->setMaximumSize(QSize(16777215, 300));
        lblScreenshot->setFrameShape(QFrame::StyledPanel);
        lblScreenshot->setScaledContents(true);

        verticalLayout->addWidget(lblScreenshot);

        txtLog = new QTextEdit(centralwidget);
        txtLog->setObjectName("txtLog");
        txtLog->setReadOnly(true);

        verticalLayout->addWidget(txtLog);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 800, 22));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        btnSelectWindow->setText(QCoreApplication::translate("MainWindow", "\350\216\267\345\217\226\347\252\227\345\217\243\345\217\245\346\237\204(\351\274\240\346\240\207\347\202\271\345\207\273)", nullptr));
        lblHandleInfo->setText(QCoreApplication::translate("MainWindow", "\346\234\252\351\200\211\346\213\251\347\252\227\345\217\243", nullptr));
        btnRunHshj->setText(QCoreApplication::translate("MainWindow", "\351\255\202\345\205\275\345\271\273\345\242\203\345\211\257\346\234\254", nullptr));
        lblMatchResult->setText(QCoreApplication::translate("MainWindow", "\346\250\241\346\235\277\345\214\271\351\205\215\345\235\220\346\240\207: -", nullptr));
        lblOcrResult->setText(QCoreApplication::translate("MainWindow", "OCR\345\235\220\346\240\207: -", nullptr));
        lblScreenshot->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
