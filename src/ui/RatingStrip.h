#pragma once

#include "Theme.h"
#include <array>
#include <functional>

namespace ui
{
    // Seven buttons for the listener's rating: how calming does the sound feel, from
    // 1 (not at all) to 7 (very). Only usable while recording.
    class RatingStrip : public juce::Component
    {
    public:
        std::function<void(int)> onRating;

        RatingStrip()
        {
            setTitle("Listener rating");

            for (size_t i = 0; i < buttons.size(); ++i)
            {
                auto& button = buttons[i];
                button.setButtonText(juce::String(i + 1));
                button.setHasFocusOutline(true);
                button.setTooltip("Rate how calming the sound is right now, from 1 (not at all) to 7 (very calming). "
                                  "Only while recording. Keys 1 to 7 do the same.");
                button.onClick = [this, rating = static_cast<int>(i) + 1]()
                {
                    if (onRating)
                        onRating(rating);
                };
                addAndMakeVisible(button);
            }

            setState(false, 0);
        }

        static constexpr int preferredWidth = 52 + 7 * 30;

        // `enabled` is true while recording; `rating` is the current one (0 = none)
        void setState(bool enabled, int rating)
        {
            for (size_t i = 0; i < buttons.size(); ++i)
            {
                buttons[i].setEnabled(enabled);
                buttons[i].setToggleState(enabled && rating == static_cast<int>(i) + 1, juce::dontSendNotification);
            }

            if (enabled != isActive)
            {
                isActive = enabled;
                repaint();
            }
        }

        void paint(juce::Graphics& g) override
        {
            g.setColour(isActive ? palette::textSecondary : palette::textMuted);
            g.setFont(font(12.0f));
            g.drawText("Rating", getLocalBounds().removeFromLeft(48), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            area.removeFromLeft(52);

            for (auto& button : buttons)
            {
                button.setBounds(area.removeFromLeft(28).reduced(0, 1));
                area.removeFromLeft(2);
            }
        }

    private:
        std::array<juce::TextButton, 7> buttons;
        bool isActive = false;
    };
}
