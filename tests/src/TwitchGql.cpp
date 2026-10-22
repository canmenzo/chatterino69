#include "providers/twitch/api/TwitchGql.hpp"

#include "singletons/Settings.hpp"
#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

/// The suite shares one Settings instance, so each test puts back what it
/// changed.
class TwitchGqlTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        this->enabled = getSettings()->enableTwitchGql;
        this->token = gql::token();
    }

    void TearDown() override
    {
        getSettings()->enableTwitchGql.setValue(this->enabled);
        gql::setToken(this->token);
    }

private:
    bool enabled = false;
    QString token;
};

QJsonObject parseJson(const char *json)
{
    return QJsonDocument::fromJson(json).object();
}

}  // namespace

TEST_F(TwitchGqlTest, isOffUntilEnabledAndGivenAToken)
{
    getSettings()->enableTwitchGql.setValue(false);
    gql::setToken("");
    EXPECT_FALSE(gql::isEnabled());
    EXPECT_FALSE(gql::unavailableReason().isEmpty());

    // a token alone is not enough, the user has to opt in
    gql::setToken("abc123");
    EXPECT_FALSE(gql::isEnabled());

    // and opting in without a token is not enough either
    gql::setToken("");
    getSettings()->enableTwitchGql.setValue(true);
    EXPECT_FALSE(gql::isEnabled());

    gql::setToken("abc123");
    EXPECT_TRUE(gql::isEnabled());
    EXPECT_TRUE(gql::unavailableReason().isEmpty());
}

TEST_F(TwitchGqlTest, stripsPrefixesPeoplePasteWithTheToken)
{
    gql::setToken("  abc123  ");
    EXPECT_EQ(gql::token(), "abc123");

    gql::setToken("OAuth abc123");
    EXPECT_EQ(gql::token(), "abc123");

    // copied straight out of a browser's network tab
    gql::setToken("Authorization: OAuth abc123");
    EXPECT_EQ(gql::token(), "abc123");

    gql::setToken("authorization:oauth abc123");
    EXPECT_EQ(gql::token(), "abc123");
}

TEST_F(TwitchGqlTest, readsTheFirstErrorMessage)
{
    EXPECT_EQ(gql::firstError(parseJson(R"({"data":{"ok":true}})")), "");
    EXPECT_EQ(gql::firstError(parseJson(R"({"errors":[]})")), "");

    EXPECT_EQ(gql::firstError(parseJson(
                  R"({"errors":[{"message":"failed integrity check"}]})")),
              "failed integrity check");

    // an error with no message still has to read as a failure
    EXPECT_FALSE(gql::firstError(parseJson(R"({"errors":[{}]})")).isEmpty());
}
