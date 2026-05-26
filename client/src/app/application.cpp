#include "application.h"
#include "widgets/main_window.h"
#include "widgets/login_dialog.h"

namespace kf {

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
    , main_window_(nullptr)
{
    setApplicationName("KungFu");
    setApplicationVersion("1.0.0");
    setOrganizationName("KungFu");

    setStyle("Fusion");
}

Application::~Application() {
    delete main_window_;
}

int Application::run() {
    main_window_ = new MainWindow;
    main_window_->show();
    main_window_->showLogin();
    return exec();
}

} // namespace kf
