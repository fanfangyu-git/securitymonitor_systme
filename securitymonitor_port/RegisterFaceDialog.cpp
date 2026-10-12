#include "RegisterFaceDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

RegisterFaceDialog::RegisterFaceDialog(const QVector<Candidate>& candidates,
                                       QWidget* parent)
    : QDialog(parent)
    , m_candidates(candidates)
{
    setWindowTitle(QStringLiteral("选择要登记的人脸"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        QStringLiteral("画面中检测到以下人脸，选择要登记的一张，并输入姓名：\n"
                       "（绿色=已识别人员，红色=陌生人；特征为最近几帧平均，更稳定）"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    for (int i = 0; i < m_candidates.size(); ++i) {
        const Candidate& c = m_candidates[i];
        QString identity;
        if (c.face.known) {
            identity = QStringLiteral("已识别: %1（相似度 %2）")
                           .arg(c.face.name)
                           .arg(c.face.score, 0, 'f', 2);
        } else {
            identity = QStringLiteral("陌生人");
        }
        const QString text = QStringLiteral("第 %1 路 · 人脸 %2（(%3,%4) %5×%6）→ %7")
                                 .arg(c.channel + 1)
                                 .arg(i + 1)
                                 .arg(c.face.x)
                                 .arg(c.face.y)
                                 .arg(c.face.w)
                                 .arg(c.face.h)
                                 .arg(identity);
        m_list->addItem(text);
        m_list->item(i)->setData(Qt::UserRole, i);
    }
    m_list->setCurrentRow(0);
    layout->addWidget(m_list, /*stretch=*/1);

    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(QStringLiteral("姓名："), this));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("输入要登记的人员姓名"));
    nameRow->addWidget(m_nameEdit, /*stretch=*/1);
    layout->addLayout(nameRow);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("登记"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &RegisterFaceDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &RegisterFaceDialog::reject);
    layout->addWidget(buttons);

    m_nameEdit->setFocus();
}

QString RegisterFaceDialog::personName() const
{
    return m_nameEdit->text().trimmed();
}

const RegisterFaceDialog::Candidate& RegisterFaceDialog::chosenCandidate() const
{
    return m_chosen;
}

void RegisterFaceDialog::onAccept()
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先选择要登记的人脸。"));
        return;
    }
    if (personName().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请输入要登记的人员姓名。"));
        return;
    }
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_candidates.size()) return;
    m_chosen = m_candidates[idx];
    accept();
}
