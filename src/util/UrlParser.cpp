#include "util/UrlParser.hpp"

#include <QRegularExpression>
#include <QUrl>

namespace chatterino {

std::optional<UrlParser::KickChannelResult> UrlParser::parseKickChannel(
    const QString &input)
{
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    // Check if it's a URL
    if (isKickUrl(trimmed))
    {
        // Normalize the URL
        QString normalized = trimmed;
        if (!normalized.startsWith("http://") &&
            !normalized.startsWith("https://"))
        {
            normalized = "https://" + normalized;
        }

        QUrl url(normalized);
        if (!url.isValid())
        {
            return std::nullopt;
        }

        // Extract path - should be /{username}
        QString path = url.path();
        if (path.isEmpty() || path == "/")
        {
            return std::nullopt;
        }

        // Remove leading slash
        if (path.startsWith('/'))
        {
            path = path.mid(1);
        }

        // Remove any trailing parts (e.g., /xqc/clips -> xqc)
        int slashIndex = path.indexOf('/');
        if (slashIndex > 0)
        {
            path = path.left(slashIndex);
        }

        QString slug = normalizeChannelSlug(path);
        if (slug.isEmpty())
        {
            return std::nullopt;
        }

        return KickChannelResult{slug};
    }

    // Not a URL, treat as plain username
    QString slug = normalizeChannelSlug(trimmed);

    // Validate: username should be alphanumeric with underscores, 3-25 chars
    static const QRegularExpression usernameRegex(
        QStringLiteral("^[a-z0-9_]{1,25}$"));
    if (!usernameRegex.match(slug).hasMatch())
    {
        return std::nullopt;
    }

    return KickChannelResult{slug};
}

bool UrlParser::isKickUrl(const QString &input)
{
    QString lower = input.toLower();

    // Check for common Kick URL patterns
    return lower.contains("kick.com/") || lower.startsWith("kick.com") ||
           lower.contains("www.kick.com");
}

QString UrlParser::normalizeChannelSlug(const QString &slug)
{
    return slug.trimmed().toLower();
}

}  // namespace chatterino

