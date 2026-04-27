#ifndef CHATBOTPLATFORMDIALOG_H
#define CHATBOTPLATFORMDIALOG_H

#include "uosai_global.h"

#include <DAbstractDialog>
#include <DLineEdit>
#include <DLabel>
#include <DPushButton>
#include <DSuggestButton>

#include <QJsonObject>

DWIDGET_USE_NAMESPACE

namespace uos_ai {

/**
 * @brief ChatBotPlatformDialog - 编辑单个 IM 平台的接入凭据
 *
 * 根据 platformKey（feishu / dingtalk / qq / wechat）显示不同标题和字段：
 *   - feishu:   App ID + App Secret
 *   - dingtalk: Client ID + Client Secret
 *   - qq:       App ID + Token
 *   - wechat:   Base URL + Bot Token + CDN Base URL(optional)
 */
class ChatBotPlatformDialog : public DAbstractDialog
{
    Q_OBJECT

public:
    explicit ChatBotPlatformDialog(const QString &platformKey, DWidget *parent = nullptr);

    void setConfig(const QJsonObject &cfg);
    QJsonObject config() const;
    bool unbindRequested() const;

private:
    void initUI();
    void initConnect();
    void updateConfirmEnabled();
    void updateHelpLabel();
    void startWeChatQrLogin();
    void clearWeChatBinding();
    void updateWeChatBindingUi();

private:
    QString m_platformKey;

    DLineEdit     *m_field1Edit = nullptr;
    DLineEdit     *m_field2Edit = nullptr;
    DLineEdit     *m_field3Edit = nullptr; // dingtalk: card_template_id / wechat: cdn_base_url
    bool           m_field2IsPassword = true;
    bool           m_unbindRequested = false;

    DLabel         *m_bindingStatusValueLabel = nullptr;
    DLabel         *m_accountIdValueLabel = nullptr;
    DLabel         *m_userIdValueLabel = nullptr;
    QString         m_accountId;
    QString         m_userId;

    DPushButton    *m_scanQrBtn  = nullptr;
    DPushButton    *m_unbindBtn  = nullptr;
    DLabel         *m_helpLabel  = nullptr;
    DPushButton    *m_cancelBtn  = nullptr;
    DSuggestButton *m_confirmBtn = nullptr;
};

} // namespace uos_ai

#endif // CHATBOTPLATFORMDIALOG_H
