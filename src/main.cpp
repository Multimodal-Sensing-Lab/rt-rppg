#include "mainwindow.h"
#include <QApplication>
#include <atomic>
#include <csignal>

// Set by SIGINT and SIGTERM; MainWindow::UpdateProcessFrame then shuts the application down as a key press does,
// which stops the camera (restoring its automatic exposure if it was changed) and the network servers.
std::atomic<bool> g_stopRequested{false};

static void requestStop(int)
{
    g_stopRequested = true;
}

int main(int argc, char *argv[])
{

    QApplication a(argc, argv);
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    MainWindow w;
    w.show();

    return a.exec();
}
