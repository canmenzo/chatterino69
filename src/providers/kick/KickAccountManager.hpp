#pragma once

#include "common/SignalVector.hpp"
#include "providers/kick/KickOAuthFlow.hpp"

#include <boost/signals2.hpp>
#include <pajlada/signals/signal.hpp>
#include <QJsonDocument>
#include <QString>

#include <memory>
#include <mutex>

class QNetworkAccessManager;

namespace chatterino {

class KickAccount;

/// Manages Kick.com user accounts
/// Similar to TwitchAccountManager but for Kick platform
class KickAccountManager
{
    KickAccountManager();

public:
    /// Get the current Kick account
    /// @return current account or nullptr if not logged in
    std::shared_ptr<KickAccount> getCurrent();

    /// Get the username of the current account
    [[nodiscard]] QString getCurrentUsername() const;

    /// Check if user is logged in to Kick
    [[nodiscard]] bool isLoggedIn() const;

    /// Start the OAuth login flow
    void startLogin();

    /// Log out the current user
    void logout();

    /// Load saved account from settings
    void load();

    /// Reload account data
    void reloadUsers();

    /// Signal emitted when current user changes
    boost::signals2::signal<void()> currentUserChanged;

    /// Signal emitted when login fails
    pajlada::Signals::Signal<QString> loginFailed;

    /// Signal emitted when login succeeds
    pajlada::Signals::Signal<std::shared_ptr<KickAccount>> loginSucceeded;

    /// Signal emitted when authentication expires (token refresh failed)
    pajlada::Signals::NoArgSignal authenticationExpired;

    SignalVector<std::shared_ptr<KickAccount>> accounts;

private:
    std::shared_ptr<KickAccount> currentUser_;
    std::unique_ptr<KickOAuthFlow> oauthFlow_;
    mutable std::mutex mutex_;

    /// Temporary storage for tokens while fetching user info
    KickOAuthFlow::Tokens pendingTokens_;

    void setCurrentUser(std::shared_ptr<KickAccount> account);
    void onOAuthSuccess(const KickOAuthFlow::Tokens &tokens);
    void onOAuthFailed(const QString &error);

    /// Extract username from JWT token payload
    QString extractUsernameFromToken(const QString &accessToken);

    /// Extract username from JSON response (handles various formats)
    QString extractUsernameFromResponse(const QJsonDocument &doc);

    /// Fetch user info from Kick API
    void fetchUserInfoFromApi(const QString &accessToken);

    /// Fetch user info from token introspect endpoint
    void fetchUserInfoFromIntrospect(const QString &accessToken,
                                     QNetworkAccessManager *manager);

    /// Try /api/v1/user endpoint
    void fetchCurrentUser(const QString &accessToken,
                          QNetworkAccessManager *manager);

    /// Try v2 user endpoint
    void fetchUserV2(const QString &accessToken,
                     QNetworkAccessManager *manager);

    /// Prompt user to enter their username manually
    void promptForUsername();

    /// Create account after getting username
    void createAccountWithUsername(const QString &username);

    friend class AccountController;
};

}  // namespace chatterino
