#ifndef REGISTERFACEDIALOG_H
#define REGISTERFACEDIALOG_H

#include <QDialog>
#include <QVector>

#include "core/PreviewChannel.h"

class QLineEdit;
class QListWidget;

// 点名登记对话框：列出所有通道当前检测到的人脸候选（含多帧平均特征），
// 用户选择「登记哪一路的哪一张脸」，并输入姓名。
// 用法：传入候选列表 -> exec() 返回 Accepted 后取 personName()/chosenCandidate()。
class RegisterFaceDialog : public QDialog
{
    Q_OBJECT

public:
    struct Candidate
    {
        int channel = 0;      // 通道号（0 起）
        FaceCandidate face;   // 人脸信息（feature 为可登记的平均特征）
    };

    explicit RegisterFaceDialog(const QVector<Candidate>& candidates,
                                QWidget* parent = nullptr);

    QString personName() const;         // 输入框中的姓名
    const Candidate& chosenCandidate() const; // 用户选中的候选（未选择时为空候选）

private slots:
    void onAccept();

private:
    QListWidget* m_list = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QVector<Candidate> m_candidates;
    Candidate m_chosen;
};

#endif // REGISTERFACEDIALOG_H
