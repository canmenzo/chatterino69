#include "providers/youtube/YouTubeOAuthFlow.hpp"

#include "common/QLogging.hpp"
#include "providers/youtube/YouTubeApi.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

const QString AUTH_URL =
    QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");

/// Posting a live chat message needs the full-control scope; there is no
/// narrower one that permits liveChatMessages.insert.
const QString SCOPE =
    QStringLiteral("https://www.googleapis.com/auth/youtube.force-ssl");

/// How long the loopback port stays open waiting for the browser.
constexpr int AUTH_TIMEOUT_MS = 5 * 60 * 1000;

}  // namespace

namespace chatterino {

YouTubeOAuthFlow::YouTubeOAuthFlow(QObject *parent)
    : QObject(parent)
{
}

YouTubeOAuthFlow::~YouTubeOAuthFlow()
{
    this->cancel();
}

const QString &YouTubeOAuthFlow::redirectUri() const
{
    return this->redirectUri_;
}

bool YouTubeOAuthFlow::start()
{
    if (YouTubeApi::clientId().isEmpty())
    {
        Q_EMIT this->failed(
            QStringLiteral("CHATTERINO_YOUTUBE_CLIENT_ID is not set"));
        return false;
    }

    this->server_ = std::make_unique<QTcpServer>(this);
    QObject::connect(this->server_.get(), &QTcpServer::newConnection, this,
                     &YouTubeOAuthFlow::onConnection);

    // Port 0 asks the OS for any free port, then the redirect URI is built from
    // whatever we actually got.
    if (!this->server_->listen(QHostAddress::LocalHost, 0))
    {
        Q_EMIT this->failed(QStringLiteral(
            "Could not open a local port for the YouTube login"));
        this->server_.reset();
        return false;
    }

    this->redirectUri_ =
        QStringLiteral("http://127.0.0.1:%1").arg(this->server_->serverPort());
    this->state_ =
        QString::number(QRandomGenerator::system()->generate64(), 16);

    QByteArray verifier(32, 0);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(verifier.data()), 8);
    this->codeVerifier_ = QString::fromLatin1(verifier.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    auto challenge = QCryptographicHash::hash(this->codeVerifier_.toLatin1(),
                                              QCryptographicHash::Sha256)
                         .toBase64(QByteArray::Base64UrlEncoding |
                                   QByteArray::OmitTrailingEquals);

    QUrl url{AUTH_URL};
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), YouTubeApi::clientId());
    query.addQueryItem(QStringLiteral("redirect_uri"), this->redirectUri_);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), SCOPE);
    // offline + consent is what makes Google return a refresh token, without
    // which the login would silently stop working after an hour.
    query.addQueryItem(QStringLiteral("access_type"),
                       QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("state"), this->state_);
    query.addQueryItem(QStringLiteral("code_challenge"),
                       QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"),
                       QStringLiteral("S256"));
    url.setQuery(query);

    qCDebug(chatterinoYouTube)
        << "YouTube OAuth listening on" << this->redirectUri_;

    QTimer::singleShot(AUTH_TIMEOUT_MS, this, [this] {
        if (this->server_)
        {
            this->cancel();
            Q_EMIT this->failed(
                QStringLiteral("Timed out waiting for Google in the browser"));
        }
    });

    QDesktopServices::openUrl(url);
    return true;
}

void YouTubeOAuthFlow::cancel()
{
    if (this->server_)
    {
        this->server_->close();
        this->server_.reset();
    }
}

void YouTubeOAuthFlow::onConnection()
{
    auto *socket = this->server_->nextPendingConnection();
    if (socket == nullptr)
    {
        return;
    }

    QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        auto request = QString::fromUtf8(socket->readAll());
        // "GET /?code=... HTTP/1.1"
        auto firstLine = request.section(QStringLiteral("\r\n"), 0, 0);
        auto target = firstLine.section(' ', 1, 1);

        QUrlQuery query{
            QUrl{QStringLiteral("http://127.0.0.1") + target}.query()};

        auto error = query.queryItemValue(QStringLiteral("error"));
        auto state = query.queryItemValue(QStringLiteral("state"));
        auto code = query.queryItemValue(QStringLiteral("code"));

        // Browsers also request /favicon.ico on this port, and any web page
        // can reach a loopback port, so ignore everything that is not a
        // callback carrying this flow's state rather than failing the flow.
        if ((code.isEmpty() && error.isEmpty()) || state != this->state_)
        {
            socket->close();
            socket->deleteLater();
            return;
        }

        // The socket is a child of server_, so tearing the server down inside
        // the socket's own readyRead would delete it mid-signal.
        this->server_->close();
        QTimer::singleShot(0, this, [this] {
            this->cancel();
        });

        if (!error.isEmpty())
        {
            this->respond(socket, QStringLiteral("Login cancelled"),
                          QStringLiteral("You can close this tab."));
            Q_EMIT this->failed(error);
            return;
        }

        this->respond(socket, QStringLiteral("Signed in to YouTube"),
                      QStringLiteral("You can close this tab and go back to "
                                     "Chatterino."));
        Q_EMIT this->codeReceived(code, this->redirectUri_,
                                  this->codeVerifier_);
    });

    QObject::connect(socket, &QTcpSocket::disconnected, socket,
                     &QTcpSocket::deleteLater);
}

void YouTubeOAuthFlow::respond(QTcpSocket *socket, const QString &title,
                               const QString &body)
{
    auto html =
        QStringLiteral("<!doctype html><meta charset=\"utf-8\">"
                       "<title>%1</title>"
                       "<body style=\"font-family:system-ui;background:#18181b;"
                       "color:#efeff1;display:grid;place-items:center;"
                       "height:100vh;margin:0\">"
                       "<div style=\"text-align:center\"><h2>%1</h2><p>%2</p>"
                       "</div></body>")
            .arg(title, body);

    auto payload = html.toUtf8();
    QByteArray response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Content-Length: " +
                          QByteArray::number(payload.size()) +
                          "\r\n"
                          "Connection: close\r\n\r\n" +
                          payload;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

}  // namespace chatterino
