#pragma once

#include "controllers/accounts/Account.hpp"
#include "providers/kick/KickOAuthFlow.hpp"

#include <pajlada/signals/signal.hpp>
#include <QDateTime>
#include <QString>

#include <memory>
#include <optional>

namespace chatterino {

/// Represents a Kick.com user account with OAuth authentication
class KickAccount : public Account
{
public:
    KickAccount(const QString &username, const QString &accessToken,
                const QString &refreshToken, const QDateTime &expiresAt);

    /// Account interface
    QString toString() const override;

    /// Get the account's Kick user ID
    [[nodiscard]] int getUserId() const;

    /// Get the account's username
    [[nodiscard]] const QString &getUserName() const;

    /// Get the access token for API requests
    [[nodiscard]] QString getAccessToken() const;

    /// Get the refresh token for token renewal
    [[nodiscard]] QString getRefreshToken() const;

    /// Check if the access token has expired
    [[nodiscard]] bool isTokenExpired() const;

    /// Check if the account is authenticated
    [[nodiscard]] bool isAuthenticated() const;

    /// Refresh the access token using the refresh token
    /// @return true if refresh was initiated successfully
    bool refreshAccessToken();

    /// Save account credentials to settings
    void saveToSettings();

    /// Load account from settings
    /// @return loaded account or nullptr if not found
    static std::shared_ptr<KickAccount> loadFromSettings();

    /// Clear saved credentials from settings
    static void clearSavedCredentials();

    /// Signal emitted when token refresh completes
    pajlada::Signals::Signal<bool> tokenRefreshed;

    /// Signal emitted when authentication status changes
    pajlada::Signals::Signal<bool> authenticationChanged;

private:
    /// Internal token refresh implementation with retry support
    void doRefreshAccessToken(int retryAttempt);

    QString username_;
    int userId_{0};
    QString accessToken_;
    QString refreshToken_;
    QDateTime expiresAt_;
    bool isRefreshing_{false};
    static constexpr int MAX_REFRESH_RETRIES = 2;
};

}  // namespace chatterino
