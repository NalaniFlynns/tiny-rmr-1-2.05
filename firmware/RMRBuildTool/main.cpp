#include <QApplication>
#include <QFontDatabase>
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
    MainWindow w;
    w.show();
    return app.exec();
}