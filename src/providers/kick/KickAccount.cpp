#include "providers/kick/KickAccount.hpp"

#include "common/Credentials.hpp"
#include "common/Env.hpp"
#include "common/ProviderId.hpp"
#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>

namespace chatterino {

namespace {
// Older builds kept the tokens in plaintext QSettings under these keys; they
// now live in the system keychain and these are only read to migrate.
const QString SETTINGS_KEY_ACCESS_TOKEN = "kick/accessToken";
const QString SETTINGS_KEY_REFRESH_TOKEN = "kick/refreshToken";
const QString CREDENTIALS_PROVIDER = "kick";
const QString SETTINGS_KEY_EXPIRES_AT = "kick/expiresAt";
const QString SETTINGS_KEY_USERNAME = "kick/username";
const QString SETTINGS_KEY_USER_ID = "kick/userId";
}  // namespace

KickAccount::KickAccount(const QString &username, const QString &accessToken,
                         const QString &refreshToken,
                         const QDateTime &expiresAt)
    : Account(ProviderId::Kick)
    , username_(username)
    , accessToken_(accessToken)
    , refreshToken_(refreshToken)
    , expiresAt_(expiresAt)
{
}

QString KickAccount::toString() const
{
    return this->username_;
}

int KickAccount::getUserId() const
{
    return this->userId_;
}

const QString &KickAccount::getUserName() const
{
    return this->username_;
}

QString KickAccount::getAccessToken() const
{
    return this->accessToken_;
}

QString KickAccount::getRefreshToken() const
{
    return this->refreshToken_;
}

bool KickAccount::isTokenExpired() const
{
    return QDateTime::currentDateTime() >= this->expiresAt_;
}

bool KickAccount::isAuthenticated() const
{
    return !this->accessToken_.isEmpty() && !this->isTokenExpired();
}

bool KickAccount::refreshAccessToken()
{
    if (this->refreshToken_.isEmpty())
    {
        qCWarning(chatterinoKick) << "Cannot refresh: no refresh token";
        this->tokenRefreshed.invoke(false);
        return false;
    }

    // Prevent concurrent refresh attempts
    if (this->isRefreshing_)
    {
        qCDebug(chatterinoKick) << "Token refresh already in progress";
        return true;  // Return true since refresh is already happening
    }

    this->isRefreshing_ = true;
    this->doRefreshAccessToken(0);
    return true;
}

void KickAccount::doRefreshAccessToken(int retryAttempt)
{
    qCDebug(chatterinoKick)
        << "Refreshing access token... (attempt" << (retryAttempt + 1) << "of"
        << (MAX_REFRESH_RETRIES + 1) << ")";

    auto *manager = new QNetworkAccessManager();

    auto config = KickOAuthFlow::clientConfig();

    QUrl tokenUrl(config.tokenUrl);
    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("grant_type", "refresh_token");
    postData.addQueryItem("client_id", config.clientId);
    if (!config.clientSecret.isEmpty())
    {
        postData.addQueryItem("client_secret", config.clientSecret);
    }
    postData.addQueryItem("refresh_token", this->refreshToken_);

    QNetworkReply *reply =
        manager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    QObject::connect(
        reply, &QNetworkReply::finished, [this, reply, manager, retryAttempt] {
            reply->deleteLater();
            manager->deleteLater();

            int statusCode =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                    .toInt();

            if (reply->error() != QNetworkReply::NoError)
            {
                qCWarning(chatterinoKick)
                    << "Token refresh failed (HTTP" << statusCode
                    << "):" << reply->errorString();

                // Retry on transient errors (network issues, 5xx errors)
                bool isTransient =
                    (reply->error() == QNetworkReply::TimeoutError ||
                     reply->error() ==
                         QNetworkReply::TemporaryNetworkFailureError ||
                     reply->error() ==
                         QNetworkReply::NetworkSessionFailedError ||
                     (statusCode >= 500 && statusCode < 600));

                if (isTransient && retryAttempt < MAX_REFRESH_RETRIES)
                {
                    // Exponential backoff: 1s, 2s, 4s
                    int delayMs = 1000 * (1 << retryAttempt);
                    qCDebug(chatterinoKick)
                        << "Retrying token refresh in" << delayMs << "ms...";

                    QTimer::singleShot(delayMs, [this, retryAttempt] {
                        this->doRefreshAccessToken(retryAttempt + 1);
                    });
                    return;
                }

                // Non-recoverable error or max retries reached
                this->isRefreshing_ = false;
                this->tokenRefreshed.invoke(false);
                this->authenticationChanged.invoke(false);
                return;
            }

            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (!doc.isObject())
            {
                qCWarning(chatterinoKick) << "Invalid refresh response format";
                this->isRefreshing_ = false;
                this->tokenRefreshed.invoke(false);
                return;
            }

            QJsonObject obj = doc.object();

            if (obj.contains("error"))
            {
                QString error = obj["error"].toString();
                QString description = obj["error_description"].toString();
                qCWarning(chatterinoKick)
                    << "Refresh error:" << error << "-" << description;

                // If refresh token is invalid, user needs to re-authenticate
                if (error == "invalid_grant" || error == "invalid_token")
                {
                    qCWarning(chatterinoKick) << "Refresh token is invalid. "
                                                 "User must re-authenticate.";
                }

                this->isRefreshing_ = false;
                this->tokenRefreshed.invoke(false);
                this->authenticationChanged.invoke(false);
                return;
            }

            // Update tokens
            QString newAccessToken = obj["access_token"].toString();
            if (newAccessToken.isEmpty())
            {
                qCWarning(chatterinoKick)
                    << "No access token in refresh response";
                this->isRefreshing_ = false;
                this->tokenRefreshed.invoke(false);
                return;
            }

            this->accessToken_ = newAccessToken;
            if (obj.contains("refresh_token"))
            {
                this->refreshToken_ = obj["refresh_token"].toString();
            }

            int expiresIn = obj["expires_in"].toInt(3600);
            this->expiresAt_ = QDateTime::currentDateTime().addSecs(expiresIn);

            qCDebug(chatterinoKick)
                << "Token refreshed successfully, expires in" << expiresIn
                << "seconds";

            this->saveToSettings();
            this->isRefreshing_ = false;
            this->tokenRefreshed.invoke(true);
            this->authenticationChanged.invoke(true);
        });
}

void KickAccount::saveToSettings()
{
    QSettings settings;

    settings.setValue(SETTINGS_KEY_USERNAME,
                      this->username_);  // Save username!
    settings.setValue(SETTINGS_KEY_EXPIRES_AT,
                      this->expiresAt_.toString(Qt::ISODate));
    settings.setValue(SETTINGS_KEY_USER_ID, this->userId_);

    Credentials::instance().set(CREDENTIALS_PROVIDER, "accessToken",
                                this->accessToken_);
    Credentials::instance().set(CREDENTIALS_PROVIDER, "refreshToken",
                                this->refreshToken_);

    qCDebug(chatterinoKick) << "Kick account saved:" << this->username_;
}

void KickAccount::loadFromSettings(
    std::function<void(std::shared_ptr<KickAccount>)> onLoaded)
{
    QSettings settings;

    QString expiresAtStr = settings.value(SETTINGS_KEY_EXPIRES_AT).toString();
    QString username = settings.value(SETTINGS_KEY_USERNAME).toString();
    int userId = settings.value(SETTINGS_KEY_USER_ID).toInt();
    QDateTime expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);

    auto makeAccount =
        [username, expiresAt, userId](
            const QString &accessToken,
            const QString &refreshToken) -> std::shared_ptr<KickAccount> {
        if (accessToken.isEmpty())
        {
            qCDebug(chatterinoKick) << "No saved Kick account found";
            return nullptr;
        }
        auto account = std::make_shared<KickAccount>(username, accessToken,
                                                     refreshToken, expiresAt);
        account->userId_ = userId;
        qCDebug(chatterinoKick) << "Loaded Kick account from settings";
        return account;
    };

    if (settings.contains(SETTINGS_KEY_ACCESS_TOKEN))
    {
        auto account =
            makeAccount(settings.value(SETTINGS_KEY_ACCESS_TOKEN).toString(),
                        settings.value(SETTINGS_KEY_REFRESH_TOKEN).toString());
        settings.remove(SETTINGS_KEY_ACCESS_TOKEN);
        settings.remove(SETTINGS_KEY_REFRESH_TOKEN);
        if (account)
        {
            account->saveToSettings();
            qCDebug(chatterinoKick)
                << "Moved Kick tokens from settings to the keychain";
        }
        onLoaded(account);
        return;
    }

    if (username.isEmpty())
    {
        qCDebug(chatterinoKick) << "No saved Kick account found";
        onLoaded(nullptr);
        return;
    }

    Credentials::instance().get(
        CREDENTIALS_PROVIDER, "accessToken", QCoreApplication::instance(),
        [makeAccount, onLoaded](const QString &accessToken) {
            Credentials::instance().get(
                CREDENTIALS_PROVIDER, "refreshToken",
                QCoreApplication::instance(),
                [makeAccount, onLoaded,
                 accessToken](const QString &refreshToken) {
                    onLoaded(makeAccount(accessToken, refreshToken));
                });
        });
}

void KickAccount::clearSavedCredentials()
{
    QSettings settings;
    settings.remove(SETTINGS_KEY_ACCESS_TOKEN);
    settings.remove(SETTINGS_KEY_REFRESH_TOKEN);
    settings.remove(SETTINGS_KEY_EXPIRES_AT);
    settings.remove(SETTINGS_KEY_USERNAME);
    settings.remove(SETTINGS_KEY_USER_ID);

    Credentials::instance().erase(CREDENTIALS_PROVIDER, "accessToken");
    Credentials::instance().erase(CREDENTIALS_PROVIDER, "refreshToken");

    qCDebug(chatterinoKick) << "Kick account credentials cleared";
}

}  // namespace chatterino
