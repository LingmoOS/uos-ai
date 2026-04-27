#include "chatbotplatformdialog.h"
#include "wechatqrlogindialog.h"

#include <DTitlebar>
#include <DLabel>
#include <DFontSizeManager>
#include <DPaletteHelper>
#include <DGuiApplicationHelper>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLoggingCategory>
#include <QDesktopServices>
#include <QLineEdit>
#include <QUrl>

using namespace uos_ai;

Q_DECLARE_LOGGING_CATEGORY(logAIGUI)

ChatBotPlatformDialog::ChatBotPlatformDialog(const QString &platformKey, DWidget *parent)
    : DAbstractDialog(parent)
    , m_platformKey(platformKey)
{
    initUI();
    initConnect();
    setModal(true);
}

bool ChatBotPlatformDialog::unbindRequested() const
{
    return m_unbindRequested;
}

void ChatBotPlatformDialog::initUI()
{
    setFixedWidth(440);

    // 标题栏
    DTitlebar *titleBar = new DTitlebar(this);
    titleBar->setMenuVisible(false);
    titleBar->setBackgroundTransparent(true);
    QString title;
    if (m_platformKey == "feishu")
        title = tr("Lark Integration Settings");
    else if (m_platformKey == "dingtalk")
        title = tr("DingTalk Integration Settings");
    else if (m_platformKey == "wechat")
        title = tr("WeChat Integration Settings");
    else
        title = tr("QQ Integration Settings");
    titleBar->setTitle(title);
    DFontSizeManager::instance()->bind(titleBar, DFontSizeManager::T5, QFont::DemiBold);

    QString label1, label2, label3;
    if (m_platformKey == "feishu") {
        label1 = tr("App ID");
        label2 = tr("App Secret");
        m_field2IsPassword = true;
    } else if (m_platformKey == "dingtalk") {
        label1 = tr("Client ID");
        label2 = tr("Client Secret");
        label3 = tr("Card Template ID");
        m_field2IsPassword = true;
    } else if (m_platformKey == "wechat") {
        label1 = tr("Base URL");
        label2 = tr("Bot Token");
        label3 = tr("CDN Base URL");
        m_field2IsPassword = true;
    } else { // qq
        label1 = tr("App ID");
        label2 = tr("App Secret");
        m_field2IsPassword = true;
    }

    // 表单
    m_field1Edit = new DLineEdit(this);
    m_field1Edit->setPlaceholderText(tr("Required"));
    m_field1Edit->setClearButtonEnabled(true);

    m_field2Edit = new DLineEdit(this);
    m_field2Edit->setPlaceholderText(tr("Required"));
    m_field2Edit->setClearButtonEnabled(true);
    if (m_field2IsPassword)
        m_field2Edit->lineEdit()->setEchoMode(QLineEdit::Password);

    QFormLayout *formLayout = new QFormLayout;
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->setSpacing(10);
    formLayout->addRow(new DLabel(label1), m_field1Edit);
    formLayout->addRow(new DLabel(label2), m_field2Edit);

    if (m_platformKey == "dingtalk" || m_platformKey == "wechat") {
        m_field3Edit = new DLineEdit(this);
        m_field3Edit->setPlaceholderText(tr("Optional"));
        m_field3Edit->setClearButtonEnabled(true);
        formLayout->addRow(new DLabel(label3), m_field3Edit);
    }

    if (m_platformKey == "wechat") {
        m_bindingStatusValueLabel = new DLabel(this);
        m_accountIdValueLabel = new DLabel(this);
        m_userIdValueLabel = new DLabel(this);

        m_bindingStatusValueLabel->setWordWrap(true);
        m_accountIdValueLabel->setWordWrap(true);
        m_userIdValueLabel->setWordWrap(true);

        formLayout->addRow(new DLabel(tr("Binding Status")), m_bindingStatusValueLabel);
        formLayout->addRow(new DLabel(tr("Bot Account ID")), m_accountIdValueLabel);
        formLayout->addRow(new DLabel(tr("WeChat User ID")), m_userIdValueLabel);
    }

    // 配置方法链接（居中显示）
    m_helpLabel = new DLabel(this);
    m_helpLabel->setAlignment(Qt::AlignCenter);
    m_helpLabel->setOpenExternalLinks(false);

    QHBoxLayout *helpLayout = new QHBoxLayout;
    helpLayout->setContentsMargins(0, 0, 0, 0);
    helpLayout->setSpacing(8);
    helpLayout->addStretch();
    if (m_platformKey == "wechat") {
        m_scanQrBtn = new DPushButton(tr("Scan QR Code"), this);
        helpLayout->addWidget(m_scanQrBtn);
        m_unbindBtn = new DPushButton(tr("Unbind"), this);
        helpLayout->addWidget(m_unbindBtn);
    }
    helpLayout->addWidget(m_helpLabel);
    helpLayout->addStretch();

    // 按钮区
    m_cancelBtn  = new DPushButton(tr("Cancel"), this);
    m_confirmBtn = new DSuggestButton(tr("Confirm"), this);
    m_confirmBtn->setEnabled(false);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(10);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_confirmBtn);

    // 主布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 0, 20, 20);
    mainLayout->setSpacing(16);
    mainLayout->addWidget(titleBar);
    mainLayout->addLayout(formLayout);
    mainLayout->addLayout(helpLayout);
    mainLayout->addStretch();
    mainLayout->addLayout(btnLayout);
}

void ChatBotPlatformDialog::initConnect()
{
    connect(m_field1Edit, &DLineEdit::textChanged, this, &ChatBotPlatformDialog::updateConfirmEnabled);
    connect(m_field2Edit, &DLineEdit::textChanged, this, &ChatBotPlatformDialog::updateConfirmEnabled);
    connect(m_cancelBtn,  &DPushButton::clicked, this, &ChatBotPlatformDialog::reject);
    connect(m_confirmBtn, &DSuggestButton::clicked, this, &ChatBotPlatformDialog::accept);

    if (m_scanQrBtn) {
        connect(m_scanQrBtn, &DPushButton::clicked,
                this, &ChatBotPlatformDialog::startWeChatQrLogin);
    }
    if (m_unbindBtn) {
        connect(m_unbindBtn, &DPushButton::clicked,
                this, &ChatBotPlatformDialog::clearWeChatBinding);
    }

    connect(m_helpLabel, &DLabel::linkActivated, this, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged,
            this, &ChatBotPlatformDialog::updateHelpLabel);
    updateHelpLabel();
}

void ChatBotPlatformDialog::updateConfirmEnabled()
{
    const bool hasRequiredFields =
        !m_field1Edit->text().trimmed().isEmpty() &&
        !m_field2Edit->text().trimmed().isEmpty();
    m_confirmBtn->setEnabled(m_unbindRequested || hasRequiredFields);
}

void ChatBotPlatformDialog::setConfig(const QJsonObject &cfg)
{
    m_unbindRequested = false;
    if (m_platformKey == "feishu") {
        m_field1Edit->setText(cfg.value("app_id").toString());
        m_field2Edit->setText(cfg.value("app_secret").toString());
    } else if (m_platformKey == "dingtalk") {
        m_field1Edit->setText(cfg.value("client_id").toString());
        m_field2Edit->setText(cfg.value("client_secret").toString());
        if (m_field3Edit)
            m_field3Edit->setText(cfg.value("card_template_id").toString());
    } else if (m_platformKey == "wechat") {
        m_field1Edit->setText(cfg.value("base_url").toString());
        m_field2Edit->setText(cfg.value("bot_token").toString());
        if (m_field3Edit)
            m_field3Edit->setText(cfg.value("cdn_base_url").toString());
        m_accountId = cfg.value("account_id").toString().trimmed();
        m_userId = cfg.value("user_id").toString().trimmed();
        updateWeChatBindingUi();
    } else { // qq
        m_field1Edit->setText(cfg.value("app_id").toString());
        m_field2Edit->setText(cfg.value("token").toString());
    }
    updateConfirmEnabled();
}

QJsonObject ChatBotPlatformDialog::config() const
{
    QJsonObject obj;
    if (m_platformKey == "feishu") {
        obj["app_id"]     = m_field1Edit->text().trimmed();
        obj["app_secret"] = m_field2Edit->text().trimmed();
    } else if (m_platformKey == "dingtalk") {
        obj["client_id"]        = m_field1Edit->text().trimmed();
        obj["client_secret"]    = m_field2Edit->text().trimmed();
        obj["card_template_id"] = m_field3Edit ? m_field3Edit->text().trimmed() : QString();
    } else if (m_platformKey == "wechat") {
        obj["base_url"] = m_field1Edit->text().trimmed();
        obj["bot_token"] = m_field2Edit->text().trimmed();
        obj["cdn_base_url"] = m_field3Edit ? m_field3Edit->text().trimmed() : QString();
        obj["account_id"] = m_accountId;
        obj["user_id"] = m_userId;
    } else { // qq
        obj["app_id"] = m_field1Edit->text().trimmed();
        obj["token"]  = m_field2Edit->text().trimmed();
    }
    return obj;
}

void ChatBotPlatformDialog::startWeChatQrLogin()
{
    WeChatQrLoginDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_field1Edit->setText(dlg.baseUrl());
    m_field2Edit->setText(dlg.botToken());
    m_accountId = dlg.accountId();
    m_userId = dlg.userId();
    m_unbindRequested = false;
    updateWeChatBindingUi();
    updateConfirmEnabled();
}

void ChatBotPlatformDialog::clearWeChatBinding()
{
    if (m_platformKey != "wechat")
        return;

    m_field1Edit->clear();
    m_field2Edit->clear();
    if (m_field3Edit)
        m_field3Edit->clear();
    m_accountId.clear();
    m_userId.clear();
    m_unbindRequested = true;
    updateWeChatBindingUi();
    accept();
}

void ChatBotPlatformDialog::updateWeChatBindingUi()
{
    if (m_platformKey != "wechat")
        return;

    const bool isBound = !m_accountId.isEmpty() || !m_userId.isEmpty()
                         || (!m_field1Edit->text().trimmed().isEmpty()
                             && !m_field2Edit->text().trimmed().isEmpty());
    if (m_bindingStatusValueLabel)
        m_bindingStatusValueLabel->setText(isBound ? tr("Bound") : tr("Not Bound"));
    if (m_accountIdValueLabel)
        m_accountIdValueLabel->setText(m_accountId.isEmpty() ? tr("Not available") : m_accountId);
    if (m_userIdValueLabel)
        m_userIdValueLabel->setText(m_userId.isEmpty() ? tr("Not available") : m_userId);
    if (m_unbindBtn)
        m_unbindBtn->setEnabled(isBound);
}

void ChatBotPlatformDialog::updateHelpLabel()
{
    const QColor color = DPaletteHelper::instance()->palette(m_helpLabel).color(DPalette::Normal, DPalette::Highlight);

    QString url;
    if (m_platformKey == "feishu")
        url = "https://bbs.deepin.org/post/296336";
    else if (m_platformKey == "dingtalk")
        url = "https://bbs.deepin.org/post/296337";
    else if (m_platformKey == "wechat")
        url = "https://github.com/Tencent/openclaw-weixin";
    else
        url = "https://bbs.deepin.org/post/296334";

    m_helpLabel->setText(
        QString("<a href=\"%1\" style=\"color:%2; text-decoration: none;\">%3</a>")
        .arg(url, color.name(), tr("Configuration Guide >"))
    );
}
