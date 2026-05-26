#pragma once

#include <QApplication>

namespace kf {

class MainWindow;

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    ~Application();

    int run();

private:
    MainWindow *main_window_;
};

} // namespace kf
