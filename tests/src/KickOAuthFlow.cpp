#include "providers/kick/KickOAuthFlow.hpp"

#include "Test.hpp"

#include <QString>

using namespace chatterino;

TEST(KickOAuthFlow, createsSuccessfully)
{
    KickOAuthFlow flow;
    // Should not crash on construction
    EXPECT_TRUE(true);
}

TEST(KickOAuthFlow, notInProgressByDefault)
{
    KickOAuthFlow flow;
    EXPECT_FALSE(flow.isInProgress());
}

TEST(KickOAuthFlow, authUrlIsCorrect)
{
    // Verify OAuth URL matches Context7 documentation
    // Authorization: https://id.kick.com/oauth/authorize
    QString expectedUrl = "https://id.kick.com/oauth/authorize";
    QString actualUrl = QString::fromLatin1(KickOAuthFlow::KICK_AUTH_URL);

    EXPECT_EQ(actualUrl, expectedUrl);
}

TEST(KickOAuthFlow, tokenUrlIsCorrect)
{
    // Verify token URL matches Context7 documentation
    // Token: https://id.kick.com/oauth/token
    QString expectedUrl = "https://id.kick.com/oauth/token";
    QString actualUrl = QString::fromLatin1(KickOAuthFlow::KICK_TOKEN_URL);

    EXPECT_EQ(actualUrl, expectedUrl);
}

TEST(KickOAuthFlow, redirectUriIsLocalhost)
{
    QString redirectUri = QString::fromLatin1(KickOAuthFlow::REDIRECT_URI);

    EXPECT_TRUE(redirectUri.startsWith("http://localhost"));
}

TEST(KickOAuthFlow, localServerPortIsValid)
{
    int port = KickOAuthFlow::LOCAL_SERVER_PORT;

    // Port should be in valid range
    EXPECT_GT(port, 1024);   // Above reserved ports
    EXPECT_LT(port, 65536);  // Valid port range
}

TEST(KickOAuthFlow, clientConfigUsesProxyUnlessOwnAppIsSet)
{
    auto savedId = qgetenv("CHATTERINO_KICK_CLIENT_ID");
    auto savedSecret = qgetenv("CHATTERINO_KICK_CLIENT_SECRET");

    qunsetenv("CHATTERINO_KICK_CLIENT_ID");
    qunsetenv("CHATTERINO_KICK_CLIENT_SECRET");
    auto config = KickOAuthFlow::clientConfig();
    EXPECT_EQ(config.clientId, KickOAuthFlow::DEFAULT_CLIENT_ID);
    EXPECT_TRUE(config.clientSecret.isEmpty());
    EXPECT_EQ(config.tokenUrl, KickOAuthFlow::TOKEN_PROXY_URL);

    // An id without a secret cannot talk to Kick directly
    qputenv("CHATTERINO_KICK_CLIENT_ID", "own-id");
    EXPECT_EQ(KickOAuthFlow::clientConfig().tokenUrl,
              KickOAuthFlow::TOKEN_PROXY_URL);

    qputenv("CHATTERINO_KICK_CLIENT_SECRET", "own-secret");
    config = KickOAuthFlow::clientConfig();
    EXPECT_EQ(config.clientId, "own-id");
    EXPECT_EQ(config.clientSecret, "own-secret");
    EXPECT_EQ(config.tokenUrl, KickOAuthFlow::KICK_TOKEN_URL);

    auto restore = [](const char *name, const QByteArray &value) {
        return value.isEmpty() ? qunsetenv(name) : qputenv(name, value);
    };
    restore("CHATTERINO_KICK_CLIENT_ID", savedId);
    restore("CHATTERINO_KICK_CLIENT_SECRET", savedSecret);
}

TEST(KickOAuthFlowTokens, defaultConstruction)
{
    KickOAuthFlow::Tokens tokens;

    EXPECT_TRUE(tokens.accessToken.isEmpty());
    EXPECT_TRUE(tokens.refreshToken.isEmpty());
    EXPECT_TRUE(tokens.scope.isEmpty());
    EXPECT_FALSE(tokens.expiresAt.isValid());
}

TEST(KickOAuthFlowTokens, canSetValues)
{
    KickOAuthFlow::Tokens tokens;
    tokens.accessToken = "test_access_token";
    tokens.refreshToken = "test_refresh_token";
    tokens.scope = "chat:write user:read";
    tokens.expiresAt = QDateTime::currentDateTime().addSecs(3600);

    EXPECT_EQ(tokens.accessToken, "test_access_token");
    EXPECT_EQ(tokens.refreshToken, "test_refresh_token");
    EXPECT_EQ(tokens.scope, "chat:write user:read");
    EXPECT_TRUE(tokens.expiresAt.isValid());
}
