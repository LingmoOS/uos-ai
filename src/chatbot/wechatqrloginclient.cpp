#include "wechatqrloginclient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(logWeChatQr, "uos-ai.chatbot.wechat.qr")

namespace uos_ai {
namespace chatbot {

namespace {
constexpr char kFixedBaseUrl[] = "https://ilinkai.weixin.qq.com";
constexpr char kAppId[] = "bot";
}

WeChatQrLoginClient::WeChatQrLoginClient(QObject *parent)
    : QObject(parent)
    , m_http(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
    , m_totalTimeoutTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &WeChatQrLoginClient::requestStatus);

    m_totalTimeoutTimer->setSingleShot(true);
    connect(m_totalTimeoutTimer, &QTimer::timeout, this, [this] {
        finishWithError(tr("WeChat QR login timed out. Please try again."));
    });
}

void WeChatQrLoginClient::startLogin(const QString &botType)
{
    cancel();

    m_running = true;
    ++m_generation;
    m_botType = botType.trimmed().isEmpty() ? QStringLiteral("3") : botType.trimmed();
    m_currentPollBaseUrl = QString::fromLatin1(kFixedBaseUrl);
    m_refreshCount = 0;
    m_qrCode.clear();
    m_qrCodeImageUrl.clear();
    m_totalTimeoutTimer->start(kTotalTimeoutMs);

    emit statusChanged(tr("Requesting WeChat QR code..."));
    requestQrCode();
}

void WeChatQrLoginClient::cancel()
{
    if (!m_running)
        return;

    ++m_generation;
    resetState();
}

void WeChatQrLoginClient::requestQrCode()
{
    if (!m_running || m_reply)
        return;

    QUrl url(QString::fromLatin1(kFixedBaseUrl));
    url.setPath(QStringLiteral("/ilink/bot/get_bot_qrcode"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("bot_type"), m_botType);
    url.setQuery(query);

    QNetworkReply *reply = m_http->get(buildGetRequest(url));
    m_reply = reply;
    const quint64 generation = m_generation;
    armTimeout(reply, kQrFetchTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (m_reply == reply)
            m_reply.clear();
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        handleQrCodeReply(reply);
        reply->deleteLater();
    });
}

void WeChatQrLoginClient::requestStatus()
{
    if (!m_running || m_reply || m_qrCode.isEmpty())
        return;

    QUrl url(m_currentPollBaseUrl);
    url.setPath(QStringLiteral("/ilink/bot/get_qrcode_status"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("qrcode"), m_qrCode);
    url.setQuery(query);

    QNetworkReply *reply = m_http->get(buildGetRequest(url));
    m_reply = reply;
    const quint64 generation = m_generation;
    armTimeout(reply, kStatusTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (m_reply == reply)
            m_reply.clear();
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        handleStatusReply(reply);
        reply->deleteLater();
    });
}

void WeChatQrLoginClient::scheduleNextPoll(int delayMs)
{
    if (!m_running || m_reply)
        return;
    m_pollTimer->start(qMax(0, delayMs));
}

void WeChatQrLoginClient::handleQrCodeReply(QNetworkReply *reply)
{
    if (!m_running)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        finishWithError(tr("Failed to request WeChat QR code: %1").arg(reply->errorString()));
        return;
    }

    QJsonParseError parseError;
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        finishWithError(tr("Invalid WeChat QR response."));
        return;
    }

    m_qrCode = obj.value(QStringLiteral("qrcode")).toString().trimmed();
    m_qrCodeImageUrl = obj.value(QStringLiteral("qrcode_img_content")).toString().trimmed();
    if (m_qrCode.isEmpty() || m_qrCodeImageUrl.isEmpty()) {
        finishWithError(tr("WeChat QR code response is incomplete."));
        return;
    }

    emit qrCodeReady(m_qrCodeImageUrl);
    emit statusChanged(tr("Scan the QR code in WeChat, then confirm on your phone."));
    scheduleNextPoll();
}

void WeChatQrLoginClient::handleStatusReply(QNetworkReply *reply)
{
    if (!m_running)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            scheduleNextPoll();
            return;
        }
        qCWarning(logWeChatQr) << "QR status polling failed:" << reply->errorString();
        scheduleNextPoll();
        return;
    }

    QJsonParseError parseError;
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(logWeChatQr) << "Invalid QR status response:" << parseError.errorString();
        scheduleNextPoll();
        return;
    }

    const QString status = obj.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("wait")) {
        emit statusChanged(tr("Waiting for QR code scan..."));
        scheduleNextPoll();
        return;
    }

    if (status == QLatin1String("scaned")) {
        emit statusChanged(tr("QR code scanned. Confirm the login in WeChat."));
        scheduleNextPoll();
        return;
    }

    if (status == QLatin1String("scaned_but_redirect")) {
        const QString redirectHost = obj.value(QStringLiteral("redirect_host")).toString().trimmed();
        if (!redirectHost.isEmpty())
            m_currentPollBaseUrl = QStringLiteral("https://%1").arg(redirectHost);
        emit statusChanged(tr("Redirecting login confirmation..."));
        scheduleNextPoll();
        return;
    }

    if (status == QLatin1String("expired")) {
        ++m_refreshCount;
        if (m_refreshCount > kMaxRefreshCount) {
            finishWithError(tr("WeChat QR code expired too many times. Please try again."));
            return;
        }
        emit statusChanged(tr("QR code expired. Refreshing a new one..."));
        requestQrCode();
        return;
    }

    if (status == QLatin1String("confirmed")) {
        const QString baseUrl = obj.value(QStringLiteral("baseurl")).toString().trimmed();
        const QString botToken = obj.value(QStringLiteral("bot_token")).toString().trimmed();
        const QString accountId = obj.value(QStringLiteral("ilink_bot_id")).toString().trimmed();
        const QString userId = obj.value(QStringLiteral("ilink_user_id")).toString().trimmed();
        if (baseUrl.isEmpty() || botToken.isEmpty()) {
            finishWithError(tr("WeChat login succeeded but returned incomplete credentials."));
            return;
        }
        finishWithSuccess(baseUrl, botToken, accountId, userId);
        return;
    }

    qCWarning(logWeChatQr) << "Unexpected QR status:" << status;
    scheduleNextPoll();
}

void WeChatQrLoginClient::finishWithError(const QString &message)
{
    if (!m_running)
        return;

    const QString finalMessage = message.trimmed().isEmpty()
                                 ? tr("WeChat QR login failed.")
                                 : message;
    ++m_generation;
    resetState();
    emit loginFailed(finalMessage);
}

void WeChatQrLoginClient::finishWithSuccess(const QString &baseUrl, const QString &botToken,
                                            const QString &accountId, const QString &userId)
{
    if (!m_running)
        return;

    ++m_generation;
    resetState();
    emit statusChanged(tr("WeChat connected successfully."));
    emit loginSucceeded(baseUrl, botToken, accountId, userId);
}

void WeChatQrLoginClient::resetState()
{
    m_running = false;
    m_pollTimer->stop();
    m_totalTimeoutTimer->stop();
    m_botType.clear();
    m_qrCode.clear();
    m_qrCodeImageUrl.clear();
    m_currentPollBaseUrl.clear();
    m_refreshCount = 0;

    if (QNetworkReply *reply = m_reply.data()) {
        m_reply.clear();
        reply->abort();
        reply->deleteLater();
    }
}

QNetworkRequest WeChatQrLoginClient::buildGetRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("iLink-App-Id", kAppId);
    request.setRawHeader("iLink-App-ClientVersion", buildClientVersion());
    request.setRawHeader("X-WECHAT-UIN", QByteArray::number(QRandomGenerator::global()->generate()).toBase64());
    return request;
}

QByteArray WeChatQrLoginClient::buildClientVersion() const
{
    const QStringList parts = QCoreApplication::applicationVersion().split('.');
    const int major = parts.value(0).toInt();
    const int minor = parts.value(1).toInt();
    const int patch = parts.value(2).toInt();
    const quint32 value = ((major & 0xff) << 16) | ((minor & 0xff) << 8) | (patch & 0xff);
    return QByteArray::number(value);
}

void WeChatQrLoginClient::armTimeout(QNetworkReply *reply, int timeoutMs) const
{
    QTimer::singleShot(timeoutMs, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });
}

} // namespace chatbot
} // namespace uos_ai
