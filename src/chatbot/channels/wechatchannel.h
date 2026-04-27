#pragma once

#include "abstractchannel.h"

#include <QHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QString>

class QNetworkReply;

namespace uos_ai {
namespace chatbot {

class WeChatChannel : public AbstractChannel
{
    Q_OBJECT

public:
    explicit WeChatChannel(QObject *parent = nullptr);

    void    start(const QJsonObject &config) override;
    void    stop() override;
    void    sendMessage(const QString &to, const QString &content,
                        const QString &conversationType) override;
    bool    isRunning() const override { return m_running; }
    QString platformName() const override { return QStringLiteral("wechat"); }

private:
    void scheduleNextPoll(int delayMs = 0);
    void pollUpdates();
    void handlePollResponse(QNetworkReply *reply);
    void handleSendResponse(QNetworkReply *reply, const QString &to);
    void processMessage(const QJsonObject &message);
    QString extractText(const QJsonArray &items) const;

    QUrl            buildUrl(const QString &endpoint) const;
    QNetworkRequest buildRequest(const QString &endpoint, const QByteArray &body) const;
    QJsonObject     baseInfo() const;
    QString         buildClientMessageId() const;
    QByteArray      randomWechatUin() const;
    QByteArray      buildClientVersion() const;
    void            armTimeout(QNetworkReply *reply, int timeoutMs) const;
    void            clearPendingReplies();

    QString m_baseUrl;
    QString m_botToken;
    QString m_cdnBaseUrl;
    QString m_updatesBuf;
    bool    m_running = false;
    int     m_longPollingTimeoutMs = 35000;
    quint64 m_generation = 0;

    QNetworkAccessManager *m_http = nullptr;
    QTimer                *m_pollTimer = nullptr;
    QPointer<QNetworkReply> m_pollReply;
    QSet<QNetworkReply *>   m_sendReplies;
    QHash<QString, QString> m_contextTokens;

    static constexpr int kReconnectIntervalMs = 5000;
    static constexpr int kDefaultLongPollingTimeoutMs = 35000;
    static constexpr int kMinLongPollingTimeoutMs = 1000;
    static constexpr int kMaxLongPollingTimeoutMs = 120000;
    static constexpr int kSendTimeoutMs = 15000;
};

} // namespace chatbot
} // namespace uos_ai
