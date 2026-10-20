#include "MainWindow.h"
#include <QApplication>
#include <QCoreApplication>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("FBInstantTools");
    QCoreApplication::setApplicationName("FBInstantLocalLauncher");
    MainWindow window;
    window.show();
    return app.exec();
}
