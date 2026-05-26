#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

namespace kf {

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    QString username() const;
    QString password() const;
    QString serverUrl() const;

    void setError(const QString &msg);
    void setLoading(bool loading);

signals:
    void loginRequested(const QString &url, const QString &user, const QString &pass);

private:
    QLineEdit *url_edit_;
    QLineEdit *user_edit_;
    QLineEdit *pass_edit_;
    QPushButton *login_btn_;
    QLabel *error_label_;
};

} // namespace kf
