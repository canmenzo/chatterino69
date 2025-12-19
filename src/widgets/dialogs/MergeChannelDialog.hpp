#pragma once

#include "common/Channel.hpp"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>

namespace chatterino {

/// Dialog for merging channels from different platforms
class MergeChannelDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MergeChannelDialog(ChannelPtr sourceChannel, QWidget *parent = nullptr);

    /// Get the selected channel to merge with
    [[nodiscard]] ChannelPtr getSelectedChannel() const;

    /// Set callback for when merge is confirmed
    void setOnMerge(std::function<void(ChannelPtr, ChannelPtr)> callback);

private:
    void setupUI();
    void updateSuggestion();
    void onMergeClicked();

    ChannelPtr sourceChannel_;
    ChannelPtr selectedChannel_;
    std::function<void(ChannelPtr, ChannelPtr)> onMergeCallback_;

    // UI elements
    QVBoxLayout *mainLayout_{nullptr};
    QLabel *sourceLabel_{nullptr};
    QLabel *targetLabel_{nullptr};
    QComboBox *platformCombo_{nullptr};
    QLineEdit *channelInput_{nullptr};
    QLabel *suggestionLabel_{nullptr};
    QPushButton *mergeButton_{nullptr};
    QPushButton *cancelButton_{nullptr};
};

}  // namespace chatterino

