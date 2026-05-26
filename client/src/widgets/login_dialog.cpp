#include "login_dialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>

namespace kf {

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("KungFu - 登录"));
    setFixedSize(380, 220);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    url_edit_ = new QLineEdit("http://127.0.0.1:8080/api/v1");
    user_edit_ = new QLineEdit;
    pass_edit_ = new QLineEdit;
    pass_edit_->setEchoMode(QLineEdit::Password);
    user_edit_->setPlaceholderText(QStringLiteral("admin"));

    form->addRow(QStringLiteral("服务地址:"), url_edit_);
    form->addRow(QStringLiteral("用户名:"), user_edit_);
    form->addRow(QStringLiteral("密码:"), pass_edit_);
    layout->addLayout(form);

    error_label_ = new QLabel;
    error_label_->setStyleSheet("color: red;");
    error_label_->hide();
    layout->addWidget(error_label_);

    login_btn_ = new QPushButton(QStringLiteral("登录"));
    login_btn_->setDefault(true);
    layout->addWidget(login_btn_);

    connect(login_btn_, &QPushButton::clicked, this, [this]() {
        error_label_->hide();
        emit loginRequested(url_edit_->text().trimmed(),
                            user_edit_->text().trimmed(),
                            pass_edit_->text());
    });

    connect(pass_edit_, &QLineEdit::returnPressed, login_btn_, &QPushButton::click);
}

QString LoginDialog::username() const { return user_edit_->text().trimmed(); }
QString LoginDialog::password() const { return pass_edit_->text(); }
QString LoginDialog::serverUrl() const { return url_edit_->text().trimmed(); }

void LoginDialog::setError(const QString &msg) {
    error_label_->setText(msg);
    error_label_->show();
}

void LoginDialog::setLoading(bool loading) {
    login_btn_->setEnabled(!loading);
    login_btn_->setText(loading ? QStringLiteral("登录中...") : QStringLiteral("登录"));
}

} // namespace kf
