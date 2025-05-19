#include <QApplication>
#include "MainWindow.h"
#include <spdlog/spdlog.h>


int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    // a.setStyleSheet(R"(*{border: 1px solid green;})");
    MainWindow w{};
    w.show();
    // spdlog::set_level(spdlog::level::warn);


    return QApplication::exec();
}
