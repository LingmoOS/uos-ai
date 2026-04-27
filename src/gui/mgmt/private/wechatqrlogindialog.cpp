#include "wechatqrlogindialog.h"

#include "chatbot/wechatqrloginclient.h"

#include <DTitlebar>
#include <DFontSizeManager>
#include <DGuiApplicationHelper>
#include <DPaletteHelper>

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibrary>
#include <QLoggingCategory>
#include <QPainter>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

using namespace uos_ai;

Q_DECLARE_LOGGING_CATEGORY(logAIGUI)

namespace {

struct QRcode {
    int version;
    int width;
    unsigned char *data;
};

using QrEncodeStringFn = QRcode *(*)(const char *string, int version, int level, int hint, int casesensitive);
using QrCodeFreeFn = void (*)(QRcode *qrcode);

struct QrEncodeApi {
    QLibrary *library = nullptr;
    QrEncodeStringFn encodeString = nullptr;
    QrCodeFreeFn freeCode = nullptr;

    bool available() const
    {
        return library && encodeString && freeCode;
    }
};

const QrEncodeApi &qrEncodeApi()
{
    static const QrEncodeApi api = [] {
        QrEncodeApi result;
        const QStringList candidates = {
            QStringLiteral("libqrencode.so.4"),
            QStringLiteral("libqrencode.so"),
        };

        for (const QString &candidate : candidates) {
            QLibrary *library = new QLibrary(candidate);
            if (!library->load()) {
                delete library;
                continue;
            }

            result.encodeString = reinterpret_cast<QrEncodeStringFn>(library->resolve("QRcode_encodeString"));
            result.freeCode = reinterpret_cast<QrCodeFreeFn>(library->resolve("QRcode_free"));
            if (result.encodeString && result.freeCode) {
                result.library = library;
                return result;
            }

            library->unload();
            delete library;
        }

        return result;
    }();

    return api;
}

} // namespace

WeChatQrLoginDialog::WeChatQrLoginDialog(DWidget *parent)
    : DAbstractDialog(parent)
    , m_client(new chatbot::WeChatQrLoginClient(this))
{
    initUI();
    initConnect();
    setModal(true);
    m_client->startLogin();
}

void WeChatQrLoginDialog::initUI()
{
    setFixedWidth(420);

    auto *titleBar = new DTitlebar(this);
    titleBar->setMenuVisible(false);
    titleBar->setBackgroundTransparent(true);
    titleBar->setTitle(tr("Connect WeChat by QR Code"));
    DFontSizeManager::instance()->bind(titleBar, DFontSizeManager::T5, QFont::DemiBold);

    m_qrImageLabel = new QLabel(this);
    m_qrImageLabel->setFixedSize(220, 220);
    m_qrImageLabel->setAlignment(Qt::AlignCenter);
    m_qrImageLabel->setText(tr("Loading QR code..."));

    m_statusLabel = new DLabel(tr("Preparing WeChat QR login..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    DFontSizeManager::instance()->bind(m_statusLabel, DFontSizeManager::T7, QFont::Normal);

    m_linkLabel = new DLabel(this);
    m_linkLabel->setAlignment(Qt::AlignCenter);
    m_linkLabel->setOpenExternalLinks(false);
    m_linkLabel->setWordWrap(true);

    m_cancelBtn = new DPushButton(tr("Cancel"), this);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addStretch();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 0, 20, 20);
    mainLayout->setSpacing(16);
    mainLayout->addWidget(titleBar);
    mainLayout->addWidget(m_qrImageLabel, 0, Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_linkLabel);
    mainLayout->addStretch();
    mainLayout->addLayout(btnLayout);
}

void WeChatQrLoginDialog::initConnect()
{
    connect(m_cancelBtn, &DPushButton::clicked, this, [this] {
        m_client->cancel();
        reject();
    });

    connect(m_linkLabel, &DLabel::linkActivated, this, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    connect(m_client, &chatbot::WeChatQrLoginClient::statusChanged,
            this, [this](const QString &message) {
        m_statusLabel->setText(message);
    });
    connect(m_client, &chatbot::WeChatQrLoginClient::qrCodeReady,
            this, [this](const QString &url) {
        m_qrUrl = url;
        updateLinkLabel(url);
        loadQrImage(url);
    });
    connect(m_client, &chatbot::WeChatQrLoginClient::loginFailed,
            this, [this](const QString &message) {
        m_statusLabel->setText(message);
        m_qrImageLabel->setText(tr("Unable to load QR code."));
    });
    connect(m_client, &chatbot::WeChatQrLoginClient::loginSucceeded,
            this, [this](const QString &baseUrl, const QString &botToken,
                         const QString &accountId, const QString &userId) {
        m_baseUrl = baseUrl;
        m_botToken = botToken;
        m_accountId = accountId;
        m_userId = userId;
        accept();
    });
}

void WeChatQrLoginDialog::updateLinkLabel(const QString &url)
{
    const QColor color = DPaletteHelper::instance()->palette(m_linkLabel).color(DPalette::Normal, DPalette::Highlight);
    m_linkLabel->setText(QString("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
                         .arg(url, color.name(), tr("Open QR code in browser")));
}

void WeChatQrLoginDialog::loadQrImage(const QString &url)
{
    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty()) {
        m_qrImageLabel->setText(tr("Open the QR code link in your browser."));
        return;
    }

    if (!trySetQrPixmapFromContent(trimmedUrl))
        m_qrImageLabel->setText(tr("Open the QR code link in your browser."));
}

bool WeChatQrLoginDialog::trySetQrPixmapFromContent(const QString &content)
{
    const QString trimmedContent = content.trimmed();
    if (trimmedContent.isEmpty())
        return false;

    const QrEncodeApi &api = qrEncodeApi();
    if (!api.available()) {
        qCWarning(logAIGUI) << "libqrencode is unavailable, falling back to browser link";
        return false;
    }

    QRcode *qrCode = api.encodeString(trimmedContent.toUtf8().constData(), 0, 1, 2, 1);
    if (!qrCode || qrCode->width <= 0 || !qrCode->data) {
        if (qrCode)
            api.freeCode(qrCode);
        qCWarning(logAIGUI) << "Failed to generate QR code from content";
        return false;
    }

    constexpr int quietZoneModules = 4;
    const int moduleCount = qrCode->width + quietZoneModules * 2;
    const int targetEdge = qMin(m_qrImageLabel->width(), m_qrImageLabel->height());
    const int scale = qMax(1, targetEdge / moduleCount);
    const int imageSize = moduleCount * scale;

    QImage image(imageSize, imageSize, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = 0; y < qrCode->width; ++y) {
        for (int x = 0; x < qrCode->width; ++x) {
            if ((qrCode->data[y * qrCode->width + x] & 0x01) == 0)
                continue;
            painter.drawRect((x + quietZoneModules) * scale,
                             (y + quietZoneModules) * scale,
                             scale,
                             scale);
        }
    }
    painter.end();

    api.freeCode(qrCode);

    m_qrImageLabel->setPixmap(QPixmap::fromImage(image));
    return true;
}
