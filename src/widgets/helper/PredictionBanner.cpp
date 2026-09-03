#include "widgets/helper/PredictionBanner.hpp"

#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Theme.hpp"
#include "util/FormatTime.hpp"
#include "util/Helpers.hpp"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace {

using namespace chatterino;

/// Twitch names outcome colours rather than sending them, so map them back.
QColor colorFor(const QString &name)
{
    if (name == "PINK")
    {
        return {"#f5009b"};
    }
    if (name == "GREEN")
    {
        return {"#00c7ac"};
    }
    return {"#387aff"};  // BLUE, and the fallback
}

QString formatOutcome(const TwitchPredictionOutcome &outcome,
                      qint64 totalPoints, bool won)
{
    auto share = totalPoints > 0 ? (outcome.points * 100) / totalPoints : 0;

    auto text = QStringLiteral("%1  %2%  %3 points, %4 %5")
                    .arg(outcome.title)
                    .arg(share)
                    .arg(localizeNumbers(static_cast<int>(outcome.points)))
                    .arg(outcome.users)
                    .arg(outcome.users == 1 ? "predictor" : "predictors");

    return won ? text + "  (winner)" : text;
}

}  // namespace

namespace chatterino {

PredictionBanner::PredictionBanner(QWidget *parent)
    : BaseWidget(parent)
{
    this->setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    auto *header = new QHBoxLayout();
    this->title_ = new QLabel(this);
    this->remaining_ = new QLabel(this);
    this->title_->setTextFormat(Qt::PlainText);
    header->addWidget(this->title_, 1);
    header->addWidget(this->remaining_);
    layout->addLayout(header);

    this->outcomes_ = new QVBoxLayout();
    this->outcomes_->setSpacing(1);
    layout->addLayout(this->outcomes_);

    this->countdown_ = new QTimer(this);
    this->countdown_->setInterval(1000);
    QObject::connect(this->countdown_, &QTimer::timeout, [this] {
        this->updateCountdown();
    });

    this->themeChangedEvent();
    this->hide();
}

void PredictionBanner::setChannel(ChannelPtr channel)
{
    this->channelConnections_.clear();
    this->channel_ = std::move(channel);

    if (auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get()))
    {
        this->channelConnections_.managedConnect(twitch->predictionChanged,
                                                 [this] {
                                                     this->refresh();
                                                 });
    }

    this->refresh();
}

void PredictionBanner::refresh()
{
    auto *twitch = dynamic_cast<TwitchChannel *>(this->channel_.get());
    if (twitch == nullptr)
    {
        this->countdown_->stop();
        this->hide();
        return;
    }

    auto prediction = twitch->prediction();
    if (!prediction ||
        (prediction->status != "ACTIVE" && prediction->status != "LOCKED"))
    {
        this->countdown_->stop();
        this->hide();
        return;
    }

    this->title_->setText(prediction->status == "LOCKED"
                              ? prediction->title + "  (locked)"
                              : prediction->title);

    while (this->outcomeLabels_.size() < prediction->outcomes.size())
    {
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        this->outcomes_->addWidget(label);
        this->outcomeLabels_.push_back(label);
    }

    for (size_t i = 0; i < this->outcomeLabels_.size(); i++)
    {
        auto *label = this->outcomeLabels_[i];
        if (i >= prediction->outcomes.size())
        {
            label->hide();
            continue;
        }

        const auto &outcome = prediction->outcomes[i];
        auto won = !prediction->winningOutcomeId.isEmpty() &&
                   outcome.id == prediction->winningOutcomeId;

        label->setText(formatOutcome(outcome, prediction->totalPoints, won));
        label->setStyleSheet(QStringLiteral("QLabel { color: %1 }")
                                 .arg(colorFor(outcome.color).name()));
        label->show();
    }

    this->locksAt_ =
        prediction->isActive() ? prediction->locksAt() : QDateTime{};
    this->updateCountdown();
    if (this->locksAt_.isValid())
    {
        this->countdown_->start();
    }
    else
    {
        this->countdown_->stop();
    }

    this->show();
}

void PredictionBanner::updateCountdown()
{
    if (!this->locksAt_.isValid())
    {
        this->remaining_->hide();
        return;
    }

    auto secondsLeft =
        QDateTime::currentDateTimeUtc().secsTo(this->locksAt_.toUTC());
    if (secondsLeft <= 0)
    {
        // entries have closed, the lock event will follow
        this->countdown_->stop();
        this->remaining_->hide();
        return;
    }

    this->remaining_->setText(formatTime(static_cast<int>(secondsLeft)));
    this->remaining_->show();
}

void PredictionBanner::themeChangedEvent()
{
    BaseWidget::themeChangedEvent();

    this->setStyleSheet(
        QStringLiteral("chatterino--PredictionBanner { background: %1 }")
            .arg(this->theme->messages.backgrounds.alternate.name()));
    this->title_->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; color: %1 }")
            .arg(this->theme->messages.textColors.regular.name()));
    this->remaining_->setStyleSheet(
        QStringLiteral("QLabel { color: %1 }")
            .arg(this->theme->messages.textColors.system.name()));
}

}  // namespace chatterino
