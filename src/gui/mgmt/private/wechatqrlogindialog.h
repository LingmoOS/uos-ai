#ifndef WECHATQRLOGINDIALOG_H
#define WECHATQRLOGINDIALOG_H

#include <DAbstractDialog>
#include <DLabel>
#include <DPushButton>

DWIDGET_USE_NAMESPACE

class QLabel;

namespace uos_ai {
namespace chatbot {
class WeChatQrLoginClient;
}

class WeChatQrLoginDialog : public DAbstractDialog
{
    Q_OBJECT

public:
    explicit WeChatQrLoginDialog(DWidget *parent = nullptr);

    QString baseUrl() const { return m_baseUrl; }
    QString botToken() const { return m_botToken; }
    QString accountId() const { return m_accountId; }
    QString userId() const { return m_userId; }

private:
    void initUI();
    void initConnect();
    void updateLinkLabel(const QString &url);
    void loadQrImage(const QString &url);
    bool trySetQrPixmapFromContent(const QString &content);

private:
    chatbot::WeChatQrLoginClient *m_client = nullptr;

    QLabel      *m_qrImageLabel = nullptr;
    DLabel      *m_statusLabel = nullptr;
    DLabel      *m_linkLabel = nullptr;
    DPushButton *m_cancelBtn = nullptr;

    QString m_qrUrl;
    QString m_baseUrl;
    QString m_botToken;
    QString m_accountId;
    QString m_userId;
};

} // namespace uos_ai

#endif // WECHATQRLOGINDIALOG_H
