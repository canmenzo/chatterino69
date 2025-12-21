#include "providers/seventv/SeventvPaints.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/Outcome.hpp"
#include "common/QLogging.hpp"
#include "messages/Image.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/seventv/paints/PaintDropShadow.hpp"
#include "providers/seventv/paints/RadialGradientPaint.hpp"
#include "providers/seventv/paints/UrlPaint.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "singletons/WindowManager.hpp"
#include "util/DebugCount.hpp"
#include "util/PostToThread.hpp"

#include <QUrlQuery>

namespace {
using namespace chatterino;
using namespace literals;

QColor rgbaToQColor(const uint32_t color)
{
    auto red = (int)((color >> 24) & 0xFF);
    auto green = (int)((color >> 16) & 0xFF);
    auto blue = (int)((color >> 8) & 0xFF);
    auto alpha = (int)(color & 0xFF);

    return {red, green, blue, alpha};
}

std::optional<QColor> parsePaintColor(const QJsonValue &color)
{
    if (color.isNull())
    {
        return std::nullopt;
    }

    return rgbaToQColor(color.toInt());
}

QGradientStops parsePaintStops(const QJsonArray &stops)
{
    QGradientStops parsedStops;
    double lastStop = -1;

    for (const auto &stop : stops)
    {
        const auto stopObject = stop.toObject();

        const auto rgbaColor = stopObject["color"].toInt();
        auto position = stopObject["at"].toDouble();

        // HACK: qt does not support hard edges in gradients like css does
        // Setting a different color at the same position twice just overwrites
        // the previous color. So we have to shift the second point slightly
        // ahead, simulating an actual hard edge
        if (position <= lastStop)
        {
            position = lastStop + 0.0000001;
        }

        lastStop = position;
        parsedStops.append(QGradientStop(position, rgbaToQColor(rgbaColor)));
    }

    return parsedStops;
}

std::vector<PaintDropShadow> parseDropShadows(const QJsonArray &dropShadows)
{
    std::vector<PaintDropShadow> parsedDropShadows;

    for (const auto &shadow : dropShadows)
    {
        const auto shadowObject = shadow.toObject();

        const auto xOffset = shadowObject["x_offset"].toDouble();
        const auto yOffset = shadowObject["y_offset"].toDouble();
        const auto radius = shadowObject["radius"].toDouble();
        const auto rgbaColor = shadowObject["color"].toInt();

        parsedDropShadows.emplace_back(xOffset, yOffset, radius,
                                       rgbaToQColor(rgbaColor));
    }

    return parsedDropShadows;
}

std::optional<std::shared_ptr<Paint>> parsePaint(const QJsonObject &paintJson)
{
    const QString name = paintJson["name"].toString();
    const QString id = paintJson["id"].toString();

    const auto color = parsePaintColor(paintJson["color"]);
    const bool repeat = paintJson["repeat"].toBool();
    const float angle = (float)paintJson["angle"].toDouble();

    const QGradientStops stops = parsePaintStops(paintJson["stops"].toArray());

    const auto shadows = parseDropShadows(paintJson["shadows"].toArray());

    const QString function = paintJson["function"].toString();
    if (function == "LINEAR_GRADIENT" || function == "linear-gradient")
    {
        return std::make_shared<LinearGradientPaint>(name, id, color, stops,
                                                     repeat, angle, shadows);
    }

    if (function == "RADIAL_GRADIENT" || function == "radial-gradient")
    {
        return std::make_shared<RadialGradientPaint>(name, id, stops, repeat,
                                                     shadows);
    }

    if (function == "URL" || function == "url")
    {
        const QString url = paintJson["image_url"].toString();
        const ImagePtr image = Image::fromUrl({url}, 1);
        if (image == nullptr)
        {
            return std::nullopt;
        }

        return std::make_shared<UrlPaint>(name, id, image, shadows);
    }

    return std::nullopt;
}

}  // namespace

namespace chatterino {

SeventvPaints::SeventvPaints() = default;

std::optional<std::shared_ptr<Paint>> SeventvPaints::getPaint(
    const QString &userName) const
{
    std::shared_lock lock(this->mutex_);

    const auto it = this->paintMap_.find(userName);
    if (it != this->paintMap_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void SeventvPaints::addPaint(const QJsonObject &paintJson)
{
    const auto paintID = paintJson["id"].toString();

    std::unique_lock lock(this->mutex_);

    if (this->knownPaints_.find(paintID) != this->knownPaints_.end())
    {
        return;
    }

    std::optional<std::shared_ptr<Paint>> paint = parsePaint(paintJson);
    if (!paint)
    {
        return;
    }

    DebugCount::increase(u"7TV Paints"_s);
    this->knownPaints_[paintID] = *paint;
}

void SeventvPaints::assignPaintToUser(const QString &paintID,
                                      const UserName &userName)
{
    std::unique_lock lock(this->mutex_);

    const auto paintIt = this->knownPaints_.find(paintID);
    if (paintIt != this->knownPaints_.end())
    {
        auto it = this->paintMap_.find(userName.string);
        bool changed = false;
        if (it == this->paintMap_.end())
        {
            this->paintMap_.emplace(userName.string, paintIt->second);
            DebugCount::increase(u"7TV Paint Assignments"_s);
            changed = true;
        }
        else if (it->second != paintIt->second)
        {
            it->second = paintIt->second;
            changed = true;
        }

        if (changed)
        {
            postToThread([] {
                getApp()->getWindows()->invalidateChannelViewBuffers();
            });
        }
    }
}

void SeventvPaints::clearPaintFromUser(const QString &paintID,
                                       const UserName &userName)
{
    std::unique_lock lock(this->mutex_);

    const auto it = this->paintMap_.find(userName.string);
    if (it != this->paintMap_.end() && it->second->id == paintID)
    {
        this->paintMap_.erase(it);
        DebugCount::decrease(u"7TV Paint Assignments"_s);
        postToThread([] {
            getApp()->getWindows()->invalidateChannelViewBuffers();
        });
    }
}

void SeventvPaints::loadUserCosmetics(const QJsonObject &userJson,
                                      const QString &userName,
                                      const QString &visibleUserID)
{
    // 7TV user response structure:
    // {
    //   "user": {
    //     "style": {
    //       "paint_id": "...",
    //       "paint": { ... paint object ... },
    //       "badge_id": "...",
    //       "badge": { ... badge object ... }
    //     }
    //   }
    // }
    // OR directly:
    // {
    //   "style": {
    //     "paint_id": "...",
    //     "paint": { ... paint object ... }
    //   }
    // }

    QJsonObject user = userJson;

    // Check if there's a nested "user" object
    if (userJson.contains("user") && userJson["user"].isObject())
    {
        user = userJson["user"].toObject();
    }

    if (!user.contains("style") || !user["style"].isObject())
    {
        return;
    }

    QJsonObject style = user["style"].toObject();

    // Check for paint
    if (style.contains("paint") && style["paint"].isObject())
    {
        QJsonObject paintJson = style["paint"].toObject();
        QString paintId = paintJson["id"].toString();

        if (!paintId.isEmpty())
        {
            // Add the paint definition
            this->addPaint(paintJson);

            // Assign it to the user
            this->assignPaintToUser(paintId, UserName{userName.toLower()});
        }
    }
    else if (style.contains("paint_id") && !style["paint_id"].isNull())
    {
        // Only paint_id is provided without the full paint object
        // We need to fetch the paint definition via GraphQL cosmetics endpoint
        QString paintId = style["paint_id"].toString();
        if (!paintId.isEmpty())
        {
            QString userNameLower = userName.toLower();

            // First try to assign if paint is already known
            this->assignPaintToUser(paintId, UserName{userNameLower});

            // Check if we already have this paint
            {
                std::shared_lock lock(this->mutex_);
                auto it = this->knownPaints_.find(paintId);
                if (it != this->knownPaints_.end())
                {
                    // Already have it, assignment above is sufficient
                    return;
                }
            }

            // Fetch the paint definition via GraphQL
            qCDebug(chatterinoSeventv)
                << "Fetching paint" << paintId << "for user" << userName;

            getApp()->getSeventvAPI()->getCosmetics(
                {paintId},
                [this, paintId, userNameLower](const QJsonObject &response) {
                    // Response structure: { data: { cosmetics: { paints: [...] } } }
                    QJsonObject data = response["data"].toObject();
                    QJsonArray paints =
                        data["cosmetics"].toObject()["paints"].toArray();

                    for (const auto &paintVal : paints)
                    {
                        QJsonObject paintJson = paintVal.toObject();
                        if (paintJson["id"].toString() == paintId)
                        {
                            // Add the paint definition
                            this->addPaint(paintJson);

                            // Re-assign to the user now that we have the paint
                            this->assignPaintToUser(paintId,
                                                    UserName{userNameLower});

                            qCDebug(chatterinoSeventv)
                                << "Loaded paint" << paintId << "for user"
                                << userNameLower;
                            break;
                        }
                    }
                },
                [paintId](const auto &) {
                    qCDebug(chatterinoSeventv)
                        << "Failed to fetch paint" << paintId;
                });
        }
    }

    // Check for badge
    auto *badges = getApp()->getSeventvBadges();
    if (badges)
    {
        // Determine the user ID for badge assignment
        // For Kick users, we use their Kick user ID as string
        QString badgeUserId = visibleUserID;
        if (badgeUserId.isEmpty())
        {
            // Fallback: use the 7TV user ID from the response
            badgeUserId = user["id"].toString();
        }

        if (style.contains("badge") && style["badge"].isObject())
        {
            QJsonObject badgeJson = style["badge"].toObject();
            QString badgeId = badgeJson["id"].toString();

            if (!badgeId.isEmpty() && !badgeUserId.isEmpty())
            {
                // Register the badge definition
                badges->registerBadge(badgeJson);

                // Assign it to the user
                badges->assignBadgeToUser(badgeId, UserId{badgeUserId});
            }
        }
        else if (style.contains("badge_id") && !style["badge_id"].isNull())
        {
            // Only badge_id provided - fetch the badge definition via GraphQL
            QString badgeId = style["badge_id"].toString();
            if (!badgeId.isEmpty() && !badgeUserId.isEmpty())
            {
                // First try to assign if badge is already known
                badges->assignBadgeToUser(badgeId, UserId{badgeUserId});

                // Also fetch the badge definition in case it's not known yet
                getApp()->getSeventvAPI()->getCosmetics(
                    {badgeId},
                    [badgeId, badgeUserId](const QJsonObject &response) {
                        auto *app = tryGetApp();
                        if (!app)
                        {
                            return;
                        }

                        auto *badges = app->getSeventvBadges();
                        if (!badges)
                        {
                            return;
                        }

                        // Parse the GraphQL response
                        // { "data": { "cosmetics": { "badges": [...] } } }
                        auto data = response["data"].toObject();
                        auto cosmetics = data["cosmetics"].toObject();
                        auto badgesArray = cosmetics["badges"].toArray();

                        for (const auto &badgeVal : badgesArray)
                        {
                            auto badgeJson = badgeVal.toObject();
                            if (badgeJson["id"].toString() == badgeId)
                            {
                                // Register and assign the badge
                                badges->registerBadge(badgeJson);
                                badges->assignBadgeToUser(badgeId,
                                                          UserId{badgeUserId});
                                break;
                            }
                        }
                    },
                    [badgeId](const NetworkResult &) {
                        qCDebug(chatterinoSeventv)
                            << "Failed to fetch badge" << badgeId;
                    });
            }
        }
    }
}

}  // namespace chatterino
