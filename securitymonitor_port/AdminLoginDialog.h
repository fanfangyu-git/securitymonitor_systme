#ifndef ADMINLOGINDIALOG_H
#define ADMINLOGINDIALOG_H

#include <QDialog>

class QLineEdit;
class QPushButton;

// 管理员登录对话框：验证通过后 accept()，主窗口凭此解锁「登记人脸」
// 账号密码存于 SQLite admin 表（加盐 SHA-256，不存明文）
class AdminLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdminLoginDialog(QWidget* parent = nullptr);

    // 静态便捷方法：弹出登录框，返回是否验证通过
    static bool execLogin(QWidget* parent);

private slots:
    void onLoginClicked();
    void onChangePasswordClicked();

private:
    QLineEdit* m_userEdit = nullptr;
    QLineEdit* m_passEdit = nullptr;
    QPushButton* m_loginButton = nullptr;
};

#endif // ADMINLOGINDIALOG_H