#ifndef WECHATQRLOGINCLIENT_H
#define WECHATQRLOGINCLIENT_H

#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QTimer;

namespace uos_ai {
namespace chatbot {

class WeChatQrLoginClient : public QObject
{
    Q_OBJECT

public:
    explicit WeChatQrLoginClient(QObject *parent = nullptr);

    void startLogin(const QString &botType = QStringLiteral("3"));
    void cancel();

Q_SIGNALS:
    void qrCodeReady(const QString &imageUrl);
    void statusChanged(const QString &message);
    void loginSucceeded(const QString &baseUrl, const QString &botToken,
                        const QString &accountId, const QString &userId);
    void loginFailed(const QString &message);

private:
    void requestQrCode();
    void requestStatus();
    void scheduleNextPoll(int delayMs = 1000);
    void handleQrCodeReply(QNetworkReply *reply);
    void handleStatusReply(QNetworkReply *reply);
    void finishWithError(const QString &message);
    void finishWithSuccess(const QString &baseUrl, const QString &botToken,
                           const QString &accountId, const QString &userId);
    void resetState();

    QNetworkRequest buildGetRequest(const QUrl &url) const;
    QByteArray      buildClientVersion() const;
    void            armTimeout(QNetworkReply *reply, int timeoutMs) const;

private:
    QNetworkAccessManager *m_http = nullptr;
    QTimer                *m_pollTimer = nullptr;
    QTimer                *m_totalTimeoutTimer = nullptr;
    QPointer<QNetworkReply> m_reply;

    QString m_botType;
    QString m_qrCode;
    QString m_qrCodeImageUrl;
    QString m_currentPollBaseUrl;
    int     m_refreshCount = 0;
    bool    m_running = false;
    quint64 m_generation = 0;

    static constexpr int kQrFetchTimeoutMs = 15000;
    static constexpr int kStatusTimeoutMs = 35000;
    static constexpr int kStatusPollIntervalMs = 1000;
    static constexpr int kTotalTimeoutMs = 480000;
    static constexpr int kMaxRefreshCount = 3;
};

} // namespace chatbot
} // namespace uos_ai

#endif // WECHATQRLOGINCLIENT_H
