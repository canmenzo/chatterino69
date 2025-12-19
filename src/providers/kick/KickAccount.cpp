#include "providers/kick/KickAccount.hpp"

#include "common/Env.hpp"
#include "common/ProviderId.hpp"
#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QUrlQuery>

namespace chatterino {

namespace {
    // Settings keys for storing Kick account credentials
    const QString SETTINGS_KEY_ACCESS_TOKEN = "kick/accessToken";
    const QString SETTINGS_KEY_REFRESH_TOKEN = "kick/refreshToken";
    const QString SETTINGS_KEY_EXPIRES_AT = "kick/expiresAt";
    const QString SETTINGS_KEY_USERNAME = "kick/username";
    const QString SETTINGS_KEY_USER_ID = "kick/userId";
}  // namespace

KickAccount::KickAccount(const QString &username, const QString &accessToken,
                         const QString &refreshToken, const QDateTime &expiresAt)
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
        return false;
    }

    qCDebug(chatterinoKick) << "Refreshing access token...";

    QNetworkAccessManager *manager = new QNetworkAccessManager();

    QUrl tokenUrl(KickOAuthFlow::KICK_TOKEN_URL);
    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("grant_type", "refresh_token");
    postData.addQueryItem("client_id", "chatterino7");  // Placeholder
    postData.addQueryItem("refresh_token", this->refreshToken_);

    QNetworkReply *reply =
        manager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, manager] {
        reply->deleteLater();
        manager->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            qCWarning(chatterinoKick)
                << "Token refresh failed:" << reply->errorString();
            this->tokenRefreshed.invoke(false);
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject())
        {
            qCWarning(chatterinoKick) << "Invalid refresh response";
            this->tokenRefreshed.invoke(false);
            return;
        }

        QJsonObject obj = doc.object();

        if (obj.contains("error"))
        {
            qCWarning(chatterinoKick)
                << "Refresh error:" << obj["error"].toString();
            this->tokenRefreshed.invoke(false);
            return;
        }

        this->accessToken_ = obj["access_token"].toString();
        if (obj.contains("refresh_token"))
        {
            this->refreshToken_ = obj["refresh_token"].toString();
        }

        int expiresIn = obj["expires_in"].toInt(3600);
        this->expiresAt_ = QDateTime::currentDateTime().addSecs(expiresIn);

        qCDebug(chatterinoKick) << "Token refreshed successfully";
        this->saveToSettings();
        this->tokenRefreshed.invoke(true);
    });

    return true;
}

void KickAccount::saveToSettings()
{
    QSettings settings;

    settings.setValue(SETTINGS_KEY_ACCESS_TOKEN, this->accessToken_);
    settings.setValue(SETTINGS_KEY_REFRESH_TOKEN, this->refreshToken_);
    settings.setValue(SETTINGS_KEY_EXPIRES_AT, this->expiresAt_.toString(Qt::ISODate));
    settings.setValue(SETTINGS_KEY_USER_ID, this->userId_);

    qCDebug(chatterinoKick) << "Kick account credentials saved to settings";
}

std::shared_ptr<KickAccount> KickAccount::loadFromSettings()
{
    QSettings settings;

    QString accessToken = settings.value(SETTINGS_KEY_ACCESS_TOKEN).toString();
    QString refreshToken = settings.value(SETTINGS_KEY_REFRESH_TOKEN).toString();
    QString expiresAtStr = settings.value(SETTINGS_KEY_EXPIRES_AT).toString();
    QString username = settings.value(SETTINGS_KEY_USERNAME).toString();

    if (accessToken.isEmpty())
    {
        qCDebug(chatterinoKick) << "No saved Kick account found";
        return nullptr;
    }

    QDateTime expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);

    auto account = std::make_shared<KickAccount>(username, accessToken,
                                                 refreshToken, expiresAt);
    account->userId_ = settings.value(SETTINGS_KEY_USER_ID).toInt();

    qCDebug(chatterinoKick) << "Loaded Kick account from settings";
    return account;
}

void KickAccount::clearSavedCredentials()
{
    QSettings settings;
    settings.remove(SETTINGS_KEY_ACCESS_TOKEN);
    settings.remove(SETTINGS_KEY_REFRESH_TOKEN);
    settings.remove(SETTINGS_KEY_EXPIRES_AT);
    settings.remove(SETTINGS_KEY_USERNAME);
    settings.remove(SETTINGS_KEY_USER_ID);

    qCDebug(chatterinoKick) << "Kick account credentials cleared";
}

}  // namespace chatterino

