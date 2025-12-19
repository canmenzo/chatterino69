#pragma once

#include "common/SignalVector.hpp"

#include <boost/signals2.hpp>
#include <pajlada/signals/signal.hpp>
#include <QString>

#include <memory>
#include <mutex>

namespace chatterino {

class KickAccount;
class KickOAuthFlow;

/// Manages Kick.tv user accounts
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

    SignalVector<std::shared_ptr<KickAccount>> accounts;

private:
    std::shared_ptr<KickAccount> currentUser_;
    std::unique_ptr<KickOAuthFlow> oauthFlow_;
    mutable std::mutex mutex_;

    void setCurrentUser(std::shared_ptr<KickAccount> account);
    void onOAuthSuccess(const KickOAuthFlow::Tokens &tokens);
    void onOAuthFailed(const QString &error);

    friend class AccountController;
};

}  // namespace chatterino

