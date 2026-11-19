#pragma once

#include "messages/MessageFlag.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <memory>
#include <optional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace chatterino {

class Channel;
struct Message;
using MessagePtr = std::shared_ptr<const Message>;

namespace spoilerguard {

enum class Verdict : std::uint8_t {
    /// Shown as usual.
    Clean,
    /// Could not be checked. Never shown, and nobody is punished for it.
    Unchecked,
    /// Taken out of chat without banning the author.
    Hide,
    /// Taken out of chat and the author is banned for good.
    Ban,
};

/// One entry for the model to judge: a chat line, or the user name of
/// someone new to the chat, which can carry a spoiler just as well.
struct Line {
    QString text;
    bool isName{false};
};

/// "JasonDiesAtTheEnd" as the words it is made of.
QString spellOut(const QString &name);

/// The Messages API request for one batch of chat lines.
QJsonObject buildRequest(const std::vector<Line> &lines,
                         const QStringList &recent, const QString &game,
                         const QString &model);

/// One verdict per line of the batch, or nothing when the response is not
/// a usable answer (a refusal, a cut-off reply, malformed JSON).
std::optional<std::vector<Verdict>> parseResponse(const QByteArray &body,
                                                  size_t lineCount);

}  // namespace spoilerguard

/// Keeps story spoilers out of the streamer's own chat.
///
/// Every chat line in a guarded channel is held back until a model has read it.
/// Clean lines are then shown just under a second late; spoilers are never
/// shown, and their author is removed on the platform the line came from. When
/// the model cannot be reached the lines stay hidden, because a spoiler that
/// got through cannot be unread.
class SpoilerGuard : public QObject
{
public:
    static SpoilerGuard &instance();

    /// Whether @a message has to be held back before @a channel may show it.
    static bool shouldScreen(const Channel &channel, const Message &message);

    void screen(std::shared_ptr<Channel> channel, MessagePtr message,
                std::optional<MessageFlags> overridingFlags);

    /// Adds a line to spoiler-guard.log. The channels record what a platform
    /// answered when asked to remove a spoiler, which is the only way to tell
    /// afterwards why a ban did not happen.
    static void note(const QString &text);

    /// True for a user the guard removed. Their ban notice repeats the user
    /// name, which may be the spoiler, so it is dropped.
    static bool removedUser(const QString &login);

    /// True for a line the guard deleted. Its deletion notice quotes the
    /// text, so it is dropped.
    static bool removedMessage(const QString &id);

private:
    explicit SpoilerGuard(QObject *parent);

    struct Held {
        std::weak_ptr<Channel> channel;
        MessagePtr message;
        std::optional<MessageFlags> overridingFlags;
        /// Empty until the request this line went out in has come back.
        std::optional<spoilerguard::Verdict> verdict;
    };

    /// One request to the model: a run of held lines, plus the user names
    /// that were new when it went out.
    struct Batch {
        quint64 firstSeq{0};
        size_t count{0};
        QStringList names;
        QByteArray body;
        int attempts{0};
    };

    void scheduleSend();
    void sendBatch();
    void post(Batch batch);
    void handleReply(QNetworkReply *reply, Batch batch);
    void finish(const Batch &batch,
                const std::vector<spoilerguard::Verdict> &verdicts);
    void flush();
    void warn(Channel &channel, const QString &text);

    Held &line(quint64 seq);
    /// Refills the rate limit's allowance and says how long until a request
    /// may go out, 0 when one may go now.
    int msUntilAllowed();

    /// Arrival order, and the order lines are shown in: a line waits for the
    /// ones ahead of it even when its own verdict came back first.
    std::deque<Held> held_;
    /// Lines are numbered as they arrive, so a request can find its lines
    /// again after others were shown and left the queue.
    quint64 frontSeq_{0};
    quint64 nextUnsent_{0};
    int openRequests_{0};

    QNetworkAccessManager *network_;
    QTimer sendTimer_;
    QElapsedTimer allowanceClock_;
    double allowance_;
    QElapsedTimer lastWarning_;

    /// The last few lines that were shown, so the model can tell a wave of
    /// reactions to what is on screen from a spoiler.
    QStringList recent_;
    QSet<QString> removedUsers_;
    QSet<QString> removedMessages_;
    /// The chats that were told the guard is screening them.
    QSet<QString> announced_;

    /// Each user name is judged once: while a request carries it, and after
    /// it was found harmless or a spoiler.
    QSet<QString> pendingNames_;
    QSet<QString> clearedNames_;
    QSet<QString> spoilerNames_;
};

}  // namespace chatterino
