#include "AdminLoginDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "service/DatabaseManager.h"

// 修改密码小对话框（复用 DatabaseManager::changeAdminPassword）
static bool showChangePasswordDialog(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("修改管理员密码"));
    dlg.setModal(true);

    auto* userEdit = new QLineEdit(DatabaseManager::instance().defaultAdminUsername(), &dlg);
    auto* oldEdit = new QLineEdit(&dlg);
    oldEdit->setEchoMode(QLineEdit::Password);
    auto* newEdit = new QLineEdit(&dlg);
    newEdit->setEchoMode(QLineEdit::Password);
    auto* confirmEdit = new QLineEdit(&dlg);
    confirmEdit->setEchoMode(QLineEdit::Password);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), userEdit);
    form->addRow(QStringLiteral("当前密码"), oldEdit);
    form->addRow(QStringLiteral("新密码"), newEdit);
    form->addRow(QStringLiteral("确认新密码"), confirmEdit);

    auto* okBtn = new QPushButton(QStringLiteral("确定"), &dlg);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), &dlg);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(okBtn);
    btnRow->addWidget(cancelBtn);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addLayout(btnRow);

    QObject::connect(okBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString user = userEdit->text().trimmed();
        const QString oldPwd = oldEdit->text();
        const QString newPwd = newEdit->text();
        if (newPwd != confirmEdit->text()) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 QStringLiteral("两次输入的新密码不一致。"));
            return;
        }
        if (newPwd.isEmpty()) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 QStringLiteral("新密码不能为空。"));
            return;
        }
        if (!DatabaseManager::instance().changeAdminPassword(user, oldPwd, newPwd)) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                 QStringLiteral("修改失败：账号不存在或当前密码错误。"));
            return;
        }
        QMessageBox::information(&dlg, QStringLiteral("成功"),
                                 QStringLiteral("密码已修改，下次登录请使用新密码。"));
        dlg.accept();
    });
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

AdminLoginDialog::AdminLoginDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("管理员验证"));
    setModal(true);
    setMinimumWidth(320);

    m_userEdit = new QLineEdit(DatabaseManager::instance().defaultAdminUsername(), this);
    m_passEdit = new QLineEdit(this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("请输入管理员密码"));

    auto* hint = new QLabel(QStringLiteral("登记人脸需要管理员权限。\n默认账号 admin，初始密码 admin123，首次使用建议修改。"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888888; font-size: 12px;"));

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), m_userEdit);
    form->addRow(QStringLiteral("密码"), m_passEdit);

    m_loginButton = new QPushButton(QStringLiteral("登录"), this);
    m_loginButton->setDefault(true);
    auto* changeBtn = new QPushButton(QStringLiteral("修改密码"), this);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(changeBtn);
    btnRow->addWidget(m_loginButton);
    btnRow->addWidget(cancelBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addLayout(btnRow);

    connect(m_loginButton, &QPushButton::clicked, this, &AdminLoginDialog::onLoginClicked);
    connect(changeBtn, &QPushButton::clicked, this, &AdminLoginDialog::onChangePasswordClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

bool AdminLoginDialog::execLogin(QWidget* parent)
{
    AdminLoginDialog dlg(parent);
    return dlg.exec() == QDialog::Accepted;
}

void AdminLoginDialog::onLoginClicked()
{
    const QString user = m_userEdit->text().trimmed();
    const QString pass = m_passEdit->text();
    if (user.isEmpty() || pass.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请输入账号和密码。"));
        return;
    }
    if (DatabaseManager::instance().verifyAdmin(user, pass)) {
        accept();
    } else {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("账号或密码错误。"));
        m_passEdit->clear();
        m_passEdit->setFocus();
    }
}

void AdminLoginDialog::onChangePasswordClicked()
{
    showChangePasswordDialog(this);
}