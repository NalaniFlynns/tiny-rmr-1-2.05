#include <QApplication>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // 【关键】强行接管 Windows 底部任务栏和所有窗口的角标！
    a.setWindowIcon(QIcon(":/logo.ico"));

    MainWindow w;
    w.show();
    return a.exec();
}