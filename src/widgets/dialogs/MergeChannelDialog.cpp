#include "widgets/dialogs/MergeChannelDialog.hpp"

#include "Application.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"

#include <QHBoxLayout>
#include <QMessageBox>

namespace chatterino {

MergeChannelDialog::MergeChannelDialog(ChannelPtr sourceChannel, QWidget *parent)
    : QDialog(parent)
    , sourceChannel_(std::move(sourceChannel))
{
    this->setWindowTitle("Merge Channels");
    this->setMinimumWidth(350);
    this->setupUI();
    this->updateSuggestion();
}

ChannelPtr MergeChannelDialog::getSelectedChannel() const
{
    return this->selectedChannel_;
}

void MergeChannelDialog::setOnMerge(
    std::function<void(ChannelPtr, ChannelPtr)> callback)
{
    this->onMergeCallback_ = std::move(callback);
}

void MergeChannelDialog::setupUI()
{
    this->mainLayout_ = new QVBoxLayout(this);

    // Source channel info
    QString sourcePlatform;
    QString sourceChannelName = this->sourceChannel_->getName();

    if (dynamic_cast<TwitchChannel *>(this->sourceChannel_.get()))
    {
        sourcePlatform = "Twitch";
    }
    else if (dynamic_cast<KickChannel *>(this->sourceChannel_.get()))
    {
        sourcePlatform = "Kick";
    }
    else
    {
        sourcePlatform = "Unknown";
    }

    this->sourceLabel_ = new QLabel(
        QString("Source: %1 - %2").arg(sourcePlatform, sourceChannelName));
    this->sourceLabel_->setStyleSheet("font-weight: bold;");
    this->mainLayout_->addWidget(this->sourceLabel_);

    // Target platform selection
    this->targetLabel_ = new QLabel("Merge with:");
    this->mainLayout_->addWidget(this->targetLabel_);

    auto *platformLayout = new QHBoxLayout();

    this->platformCombo_ = new QComboBox();
    // Only show platforms different from source
    if (sourcePlatform != "Twitch")
    {
        this->platformCombo_->addItem("Twitch", "twitch");
    }
    if (sourcePlatform != "Kick" && getSettings()->enableKickIntegration)
    {
        this->platformCombo_->addItem("Kick", "kick");
    }
    platformLayout->addWidget(this->platformCombo_);

    this->channelInput_ = new QLineEdit();
    this->channelInput_->setPlaceholderText("Channel name...");
    platformLayout->addWidget(this->channelInput_);

    this->mainLayout_->addLayout(platformLayout);

    // Auto-suggestion label
    this->suggestionLabel_ = new QLabel();
    this->suggestionLabel_->setStyleSheet("color: #888; font-style: italic;");
    this->mainLayout_->addWidget(this->suggestionLabel_);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();

    this->mergeButton_ = new QPushButton("Merge");
    this->mergeButton_->setDefault(true);
    buttonLayout->addWidget(this->mergeButton_);

    this->cancelButton_ = new QPushButton("Cancel");
    buttonLayout->addWidget(this->cancelButton_);

    this->mainLayout_->addLayout(buttonLayout);

    // Connections
    QObject::connect(this->channelInput_, &QLineEdit::textChanged, this,
                     &MergeChannelDialog::updateSuggestion);

    QObject::connect(this->mergeButton_, &QPushButton::clicked, this,
                     &MergeChannelDialog::onMergeClicked);

    QObject::connect(this->cancelButton_, &QPushButton::clicked, this,
                     &QDialog::reject);
}

void MergeChannelDialog::updateSuggestion()
{
    QString sourceChannelName = this->sourceChannel_->getName().toLower();
    QString inputText = this->channelInput_->text().trimmed().toLower();

    if (inputText.isEmpty())
    {
        // Auto-suggest matching channel name
        this->suggestionLabel_->setText(
            QString("Suggestion: Try '%1' (same username)")
                .arg(sourceChannelName));
        this->channelInput_->setText(this->sourceChannel_->getName());
    }
    else if (inputText == sourceChannelName)
    {
        this->suggestionLabel_->setText("✓ Matching username detected");
        this->suggestionLabel_->setStyleSheet("color: #53fc18; font-style: italic;");
    }
    else
    {
        this->suggestionLabel_->setText(
            QString("Note: Merging '%1' with '%2'")
                .arg(this->sourceChannel_->getName(), inputText));
        this->suggestionLabel_->setStyleSheet("color: #888; font-style: italic;");
    }
}

void MergeChannelDialog::onMergeClicked()
{
    QString targetChannelName = this->channelInput_->text().trimmed();

    if (targetChannelName.isEmpty())
    {
        QMessageBox::warning(this, "Invalid Input",
                             "Please enter a channel name to merge with.");
        return;
    }

    QString platform = this->platformCombo_->currentData().toString();

    // Get or create the target channel
    if (platform == "twitch")
    {
        this->selectedChannel_ =
            getApp()->getTwitch()->getOrAddChannel(targetChannelName);
    }
    else if (platform == "kick")
    {
        // TODO: Implement getOrAddChannel for Kick
        // For now, create a new KickChannel
        this->selectedChannel_ =
            std::make_shared<KickChannel>(targetChannelName);
    }

    if (this->selectedChannel_ && this->onMergeCallback_)
    {
        this->onMergeCallback_(this->sourceChannel_, this->selectedChannel_);
    }

    this->accept();
}

}  // namespace chatterino

