#pragma once

#include "common/Aliases.hpp"
#include "providers/seventv/paints/Paint.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace chatterino {

class SeventvPaints
{
public:
    SeventvPaints();

    void addPaint(const QJsonObject &paintJson);
    void assignPaintToUser(const QString &paintID, const UserName &userName);
    void clearPaintFromUser(const QString &paintID, const UserName &userName);

    /// Load and assign a user's paint from their 7TV user profile response
    /// @param userJson The 7TV user object (from /v3/users/KICK/{id} or similar)
    /// @param userName The username to assign the paint to
    /// @param visibleUserID Optional user ID for badge assignment (Kick user ID as string)
    void loadUserCosmetics(const QJsonObject &userJson, const QString &userName,
                           const QString &visibleUserID = QString());

    std::optional<std::shared_ptr<Paint>> getPaint(
        const QString &userName) const;

private:
    // Mutex for both `paintMap_` and `knownPaints_`
    mutable std::shared_mutex mutex_;

    // user-name => paint
    std::unordered_map<QString, std::shared_ptr<Paint>> paintMap_;
    // paint-id => paint
    std::unordered_map<QString, std::shared_ptr<Paint>> knownPaints_;
};

}  // namespace chatterino
