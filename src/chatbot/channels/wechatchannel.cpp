#include "wechatchannel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QStringList>
#include <QUuid>
#include <QtGlobal>

Q_LOGGING_CATEGORY(logWX, "uos-ai.chatbot.wechat")

using namespace uos_ai::chatbot;

namespace {
constexpr int kWeixinMessageTypeUser = 1;
constexpr int kWeixinMessageTypeBot = 2;
constexpr int kWeixinMessageStateFinish = 2;
constexpr int kWeixinItemTypeText = 1;
constexpr char kAppId[] = "bot";
}

WeChatChannel::WeChatChannel(QObject *parent)
    : AbstractChannel(parent)
    , m_http(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &WeChatChannel::pollUpdates);
}

void WeChatChannel::start(const QJsonObject &config)
{
    stop();

    m_baseUrl = config.value("base_url").toString().trimmed();
    m_botToken = config.value("bot_token").toString().trimmed();
    m_cdnBaseUrl = config.value("cdn_base_url").toString().trimmed();

    if (m_baseUrl.isEmpty() || m_botToken.isEmpty()) {
        qCWarning(logWX) << "Missing base_url or bot_token";
        emit errorOccurred(tr("WeChat integration requires Base URL and Bot Token."));
        return;
    }

    m_running = true;
    ++m_generation;
    m_updatesBuf.clear();
    m_contextTokens.clear();
    m_longPollingTimeoutMs = kDefaultLongPollingTimeoutMs;

    qCInfo(logWX) << "Starting WeChat channel with base URL" << m_baseUrl;
    scheduleNextPoll();
}

void WeChatChannel::stop()
{
    m_running = false;
    ++m_generation;
    m_pollTimer->stop();
    clearPendingReplies();
    m_updatesBuf.clear();
    m_contextTokens.clear();
}

void WeChatChannel::sendMessage(const QString &to, const QString &content,
                                const QString &conversationType)
{
    if (!m_running) {
        qCWarning(logWX) << "Channel not running";
        return;
    }

    if (conversationType != QStringLiteral("user")) {
        qCWarning(logWX) << "WeChat v1 only supports direct messages";
        return;
    }

    const QString trimmed = content.trimmed();
    if (to.isEmpty() || trimmed.isEmpty())
        return;

    QJsonObject textItem;
    textItem["type"] = kWeixinItemTypeText;
    textItem["text_item"] = QJsonObject{{"text", trimmed}};

    QJsonObject msg;
    msg["from_user_id"] = QString();
    msg["to_user_id"] = to;
    msg["client_id"] = buildClientMessageId();
    msg["message_type"] = kWeixinMessageTypeBot;
    msg["message_state"] = kWeixinMessageStateFinish;
    const QString contextToken = m_contextTokens.value(to);
    if (!contextToken.isEmpty())
        msg["context_token"] = contextToken;
    msg["item_list"] = QJsonArray{textItem};

    const QByteArray body = QJsonDocument(QJsonObject{
        {"msg", msg},
        {"base_info", baseInfo()},
    }).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_http->post(buildRequest(QStringLiteral("ilink/bot/sendmessage"), body), body);
    m_sendReplies.insert(reply);
    const quint64 generation = m_generation;
    armTimeout(reply, kSendTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, to] {
        m_sendReplies.remove(reply);
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        handleSendResponse(reply, to);
        reply->deleteLater();
    });
}

void WeChatChannel::scheduleNextPoll(int delayMs)
{
    if (!m_running || m_pollReply)
        return;
    m_pollTimer->start(qMax(0, delayMs));
}

void WeChatChannel::pollUpdates()
{
    if (!m_running || m_pollReply)
        return;

    const QByteArray body = QJsonDocument(QJsonObject{
        {"get_updates_buf", m_updatesBuf},
        {"base_info", baseInfo()},
    }).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_http->post(buildRequest(QStringLiteral("ilink/bot/getupdates"), body), body);
    m_pollReply = reply;
    const quint64 generation = m_generation;
    armTimeout(reply, qBound(kMinLongPollingTimeoutMs,
                             m_longPollingTimeoutMs + 5000,
                             kMaxLongPollingTimeoutMs + 5000));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (m_pollReply == reply)
            m_pollReply.clear();
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        handlePollResponse(reply);
        reply->deleteLater();
    });
}

void WeChatChannel::handlePollResponse(QNetworkReply *reply)
{
    if (!m_running)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            scheduleNextPoll();
            return;
        }
        qCWarning(logWX) << "getupdates failed:" << reply->errorString();
        scheduleNextPoll(kReconnectIntervalMs);
        return;
    }

    const QByteArray raw = reply->readAll();
    QJsonParseError parseError;
    const QJsonObject obj = QJsonDocument::fromJson(raw, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(logWX) << "Invalid getupdates response:" << parseError.errorString() << raw;
        scheduleNextPoll(kReconnectIntervalMs);
        return;
    }

    const int ret = obj.value("ret").toInt(0);
    if (ret != 0) {
        const int errcode = obj.value("errcode").toInt(0);
        const QString errmsg = obj.value("errmsg").toString();
        qCWarning(logWX) << "getupdates returned error:" << ret << errcode << errmsg;
        if (errcode == -14) {
            emit errorOccurred(errmsg.isEmpty()
                               ? tr("WeChat session expired. Please refresh the Bot Token.")
                               : errmsg);
            stop();
            return;
        }
        scheduleNextPoll(kReconnectIntervalMs);
        return;
    }

    if (obj.contains("get_updates_buf"))
        m_updatesBuf = obj.value("get_updates_buf").toString(m_updatesBuf);
    if (obj.contains("longpolling_timeout_ms")) {
        m_longPollingTimeoutMs = qBound(kMinLongPollingTimeoutMs,
                                        obj.value("longpolling_timeout_ms").toInt(kDefaultLongPollingTimeoutMs),
                                        kMaxLongPollingTimeoutMs);
    }

    const QJsonArray messages = obj.value("msgs").toArray();
    for (const QJsonValue &value : messages) {
        if (value.isObject())
            processMessage(value.toObject());
    }

    scheduleNextPoll();
}

void WeChatChannel::handleSendResponse(QNetworkReply *reply, const QString &to)
{
    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(logWX) << "sendmessage failed:" << reply->errorString();
        emit errorOccurred(tr("WeChat message send failed: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray raw = reply->readAll().trimmed();
    if (raw.isEmpty())
        return;

    QJsonParseError parseError;
    const QJsonObject obj = QJsonDocument::fromJson(raw, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(logWX) << "Unexpected sendmessage response:" << raw;
        return;
    }

    const int ret = obj.value("ret").toInt(0);
    if (ret == 0)
        return;

    const QString errmsg = obj.value("errmsg").toString();
    qCWarning(logWX) << "sendmessage returned error:" << ret << errmsg << "to" << to;
    emit errorOccurred(errmsg.isEmpty()
                       ? tr("WeChat message send failed.")
                       : errmsg);
}

void WeChatChannel::processMessage(const QJsonObject &message)
{
    const int messageType = message.value("message_type").toInt(0);
    if (messageType == kWeixinMessageTypeBot || messageType != kWeixinMessageTypeUser)
        return;

    const QString fromUserId = message.value("from_user_id").toString().trimmed();
    if (fromUserId.isEmpty())
        return;

    const QString text = extractText(message.value("item_list").toArray());
    if (text.isEmpty())
        return;

    const QString contextToken = message.value("context_token").toString();
    if (!contextToken.isEmpty())
        m_contextTokens.insert(fromUserId, contextToken);

    QString messageId = message.value("message_id").toVariant().toString();
    if (messageId.isEmpty()) {
        const qint64 numericId = static_cast<qint64>(message.value("message_id").toDouble(0));
        if (numericId > 0)
            messageId = QString::number(numericId);
    }

    const qint64 createdAtMs = static_cast<qint64>(message.value("create_time_ms").toDouble(0));
    const qint64 timestamp = createdAtMs > 0
                             ? createdAtMs / 1000
                             : QDateTime::currentSecsSinceEpoch();

    QJsonObject payload;
    payload["platform"] = platformName();
    payload["message_id"] = messageId;
    payload["sender"] = QJsonObject{
        {"id", fromUserId},
        {"name", fromUserId},
        {"type", "user"},
    };
    payload["conversation"] = QJsonObject{
        {"id", fromUserId},
        {"type", "user"},
    };
    payload["content"] = QJsonObject{
        {"type", "text"},
        {"text", text},
    };
    payload["timestamp"] = timestamp;

    emit messageReceived(payload);
}

QString WeChatChannel::extractText(const QJsonArray &items) const
{
    QStringList fragments;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (item.value("type").toInt(0) != kWeixinItemTypeText)
            continue;
        const QString text = item.value("text_item").toObject().value("text").toString().trimmed();
        if (!text.isEmpty())
            fragments.append(text);
    }
    return fragments.join('\n').trimmed();
}

QUrl WeChatChannel::buildUrl(const QString &endpoint) const
{
    QString base = m_baseUrl;
    if (!base.endsWith('/'))
        base += '/';
    return QUrl(base + endpoint);
}

QNetworkRequest WeChatChannel::buildRequest(const QString &endpoint, const QByteArray &body) const
{
    QNetworkRequest request(buildUrl(endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("AuthorizationType", "ilink_bot_token");
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_botToken.toUtf8());
    request.setRawHeader("X-WECHAT-UIN", randomWechatUin());
    request.setRawHeader("iLink-App-Id", kAppId);
    request.setRawHeader("iLink-App-ClientVersion", buildClientVersion());
    request.setHeader(QNetworkRequest::ContentLengthHeader, body.size());
    return request;
}

QJsonObject WeChatChannel::baseInfo() const
{
    const QString version = QCoreApplication::applicationVersion().isEmpty()
                            ? QStringLiteral("unknown")
                            : QCoreApplication::applicationVersion();
    return QJsonObject{{"channel_version", version}};
}

QString WeChatChannel::buildClientMessageId() const
{
    return QStringLiteral("openclaw-weixin_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QByteArray WeChatChannel::randomWechatUin() const
{
    const quint32 value = QRandomGenerator::global()->generate();
    return QByteArray::number(value).toBase64();
}

QByteArray WeChatChannel::buildClientVersion() const
{
    const QStringList parts = QCoreApplication::applicationVersion().split('.');
    const int major = parts.value(0).toInt();
    const int minor = parts.value(1).toInt();
    const int patch = parts.value(2).toInt();
    const quint32 value = ((major & 0xff) << 16) | ((minor & 0xff) << 8) | (patch & 0xff);
    return QByteArray::number(value);
}

void WeChatChannel::armTimeout(QNetworkReply *reply, int timeoutMs) const
{
    QTimer::singleShot(timeoutMs, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });
}

void WeChatChannel::clearPendingReplies()
{
    if (QNetworkReply *reply = m_pollReply.data()) {
        m_pollReply.clear();
        reply->abort();
        reply->deleteLater();
    }

    const auto sendReplies = m_sendReplies;
    m_sendReplies.clear();
    for (QNetworkReply *reply : sendReplies) {
        if (!reply)
            continue;
        reply->abort();
        reply->deleteLater();
    }
}
