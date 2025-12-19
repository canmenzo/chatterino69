#include "providers/kick/KickAccountManager.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickOAuthFlow.hpp"
#include "singletons/Settings.hpp"

namespace chatterino {

KickAccountManager::KickAccountManager()
{
}

std::shared_ptr<KickAccount> KickAccountManager::getCurrent()
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->currentUser_;
}

QString KickAccountManager::getCurrentUsername() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->currentUser_)
    {
        return this->currentUser_->getUserName();
    }
    return QString();
}

bool KickAccountManager::isLoggedIn() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->currentUser_ != nullptr &&
           this->currentUser_->isAuthenticated();
}

void KickAccountManager::startLogin()
{
    if (this->oauthFlow_ && this->oauthFlow_->isInProgress())
    {
        qCWarning(chatterinoKick) << "OAuth flow already in progress";
        return;
    }

    this->oauthFlow_ = std::make_unique<KickOAuthFlow>();

    std::ignore = this->oauthFlow_->authenticationSuccess.connect(
        [this](const KickOAuthFlow::Tokens &tokens) {
            this->onOAuthSuccess(tokens);
        });

    std::ignore = this->oauthFlow_->authenticationFailed.connect(
        [this](const QString &error) {
            this->onOAuthFailed(error);
        });

    if (!this->oauthFlow_->start())
    {
        qCWarning(chatterinoKick) << "Failed to start OAuth flow";
    }
}

void KickAccountManager::logout()
{
    std::lock_guard<std::mutex> lock(this->mutex_);

    if (this->currentUser_)
    {
        KickAccount::clearSavedCredentials();
        this->currentUser_.reset();
        this->accounts.clear();
        qCDebug(chatterinoKick) << "User logged out";
    }

    // Notify listeners without holding the lock
    this->currentUserChanged();
}

void KickAccountManager::load()
{
    auto account = KickAccount::loadFromSettings();
    if (account)
    {
        this->setCurrentUser(account);
        this->accounts.append(account);
    }
}

void KickAccountManager::reloadUsers()
{
    // Reload account from settings
    auto account = KickAccount::loadFromSettings();
    if (account)
    {
        this->setCurrentUser(account);
    }
}

void KickAccountManager::setCurrentUser(std::shared_ptr<KickAccount> account)
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->currentUser_ = account;
    }
    this->currentUserChanged();
}

void KickAccountManager::onOAuthSuccess(const KickOAuthFlow::Tokens &tokens)
{
    qCDebug(chatterinoKick) << "OAuth succeeded, creating account...";

    // Create account with tokens (username will be fetched via API later)
    // For now, use a placeholder username
    auto account = std::make_shared<KickAccount>(
        "kick_user",  // Will be updated with actual username
        tokens.accessToken, tokens.refreshToken, tokens.expiresAt);

    account->saveToSettings();
    this->setCurrentUser(account);
    this->accounts.append(account);

    this->loginSucceeded.invoke(account);
    qCDebug(chatterinoKick) << "Kick login successful";
}

void KickAccountManager::onOAuthFailed(const QString &error)
{
    qCWarning(chatterinoKick) << "OAuth failed:" << error;
    this->loginFailed.invoke(error);
}

}  // namespace chatterino

