#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include "mainwindow.h"
#include "encryptworker.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("RMRBuildTool");
    app.setOrganizationName("RMR");
    app.setApplicationVersion("1.0.0");
    QApplication::setStyle("Fusion");
    qRegisterMetaType<EncryptJob>("EncryptJob");

    /* 可选: 命令行第一个位置参数为工程目录, 启动即选中 */
    QString initialDir;
    if (argc > 1) initialDir = QString::fromLocal8Bit(argv[1]);

    MainWindow w(initialDir);
    w.show();
    return app.exec();
}