#pragma once

#include "Theme.h"
#include <functional>

namespace ui
{
    // Where bug reports go. Change these two if the project moves.
    namespace support
    {
        inline const juce::String issuesUrl = "https://github.com/Code4Al1z/AcousticAnalyzer/issues/new";
        inline const juce::String email     = "aliz.pasztor@trailblaiz.studio";

        // Percent-encodes UTF-8 text for a URL query. JUCE's own URL class turns spaces into
        // '+', which mail clients show literally, so the links are built by hand and handed
        // to the operating system unchanged.
        inline juce::String percentEncode(const juce::String& text)
        {
            juce::String out;
            const auto* bytes = reinterpret_cast<const unsigned char*>(text.toRawUTF8());

            for (size_t i = 0; bytes[i] != 0; ++i)
            {
                const auto c = bytes[i];
                const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                                        || c == '-' || c == '.' || c == '_' || c == '~';
                if (unreserved)
                    out << static_cast<char>(c);
                else
                    out << "%" << juce::String::toHexString(static_cast<int>(c)).toUpperCase().paddedLeft('0', 2);
            }

            return out;
        }

        // Windows hands the whole link to the shell, which stops at about 2000 characters
        inline constexpr int maxUrlLength = 1900;

        // A link that builds its text for the given number of diagnostic lines, dropping
        // lines from the end until the link is short enough
        inline juce::String fitUrl(const std::function<juce::String(const juce::String&)>& build, const juce::String& diagnostics)
        {
            auto lines = juce::StringArray::fromLines(diagnostics);

            for (;;)
            {
                const auto url = build(lines.joinIntoString("\n"));
                if (url.length() <= maxUrlLength || lines.isEmpty())
                    return url;
                lines.remove(lines.size() - 1);
            }
        }

        // A draft issue for the person to review and post. Nothing is sent until they press Submit.
        inline juce::String githubIssueUrl(const juce::String& diagnostics)
        {
            return fitUrl([](const juce::String& d)
            {
                const juce::String body =
                    "**What happened?**\n\n\n"
                    "**What did you expect to happen?**\n\n\n"
                    "**Steps to reproduce**\n1. \n\n"
                    "**Details** (filled in by the plugin: version, host and system only)\n```\n" + d + "\n```\n";
                return issuesUrl + "?title=" + percentEncode("Bug: ") + "&body=" + percentEncode(body);
            }, diagnostics);
        }

        // A draft email for people without a GitHub account
        inline juce::String mailtoUrl(const juce::String& diagnostics)
        {
            return fitUrl([](const juce::String& d)
            {
                const juce::String body =
                    "What happened?\n\n\nWhat did you expect to happen?\n\n\nSteps to reproduce:\n1. \n\n"
                    "Details (filled in by the plugin: version, host and system only)\n" + d + "\n";
                return "mailto:" + email + "?subject=" + percentEncode("Acoustic Analyzer bug report") + "&body=" + percentEncode(body);
            }, diagnostics);
        }
    }

    // A text link with a small arrow, drawn to match the footer. Underlines on hover and
    // keyboard focus, and shows a focus outline for keyboard users.
    class LinkButton : public juce::Button
    {
    public:
        explicit LinkButton(const juce::String& text) : juce::Button(text)
        {
            setButtonText(text);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
        {
            const bool focused = hasKeyboardFocus(true);
            const bool hot = isMouseOver || isButtonDown || focused;
            const auto colour = hot ? palette::accent : palette::textSecondary;

            const auto f = font(11.0f);
            const int textW = textWidth(f, getButtonText());
            const int arrowW = 14;
            const auto bounds = getLocalBounds().toFloat();
            const float right = bounds.getRight() - 4.0f;
            const float textLeft = right - static_cast<float>(arrowW + textW);
            const float centreY = bounds.getCentreY();

            g.setColour(colour);
            g.setFont(f);
            g.drawText(getButtonText(), juce::Rectangle<float>(textLeft, bounds.getY(), static_cast<float>(textW) + 2.0f, bounds.getHeight()),
                       juce::Justification::centredLeft);

            if (hot)
                g.fillRect(juce::Rectangle<float>(textLeft, centreY + 7.0f, static_cast<float>(textW), 1.0f));

            juce::Path arrow;
            const float ax = right - 8.0f;
            arrow.addTriangle(ax, centreY - 2.0f, ax + 7.0f, centreY - 2.0f, ax + 3.5f, centreY + 2.5f);
            g.fillPath(arrow);

            if (focused)
            {
                g.setColour(palette::accent);
                g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
            }
        }

        // Tight text width, so the owner can size the button to fit
        static int preferredWidth(const juce::String& text) { return textWidth(font(11.0f), text) + 14 + 12; }
    };

    // "Report a bug" in the corner of the window. Opens a menu: a draft GitHub issue, a draft
    // email, or copy the details to paste anywhere.
    class SupportFooter : public juce::Component
    {
    public:
        // Returns the text for the "details" block: version, host, system, settings
        std::function<juce::String()> getDiagnostics;

        SupportFooter() : reportButton("Report a bug")
        {
            setTitle("Support");
            reportButton.setTitle("Report a bug");
            reportButton.setDescription("Opens a menu: report on GitHub, report by email, or copy diagnostic details");
            reportButton.setTooltip("Report a bug. Opens a draft with your plugin version, host and system details. "
                                    "Nothing is sent until you submit it.");
            reportButton.onClick = [this]() { showMenu(); };
            addAndMakeVisible(reportButton);
        }

        static int preferredWidth() { return LinkButton::preferredWidth("Report a bug"); }

        void resized() override { reportButton.setBounds(getLocalBounds()); }

    private:
        void showMenu()
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Report on GitHub (opens a draft)");
            menu.addItem(2, "Report by email (opens a draft)");
            menu.addSeparator();
            menu.addItem(3, "Copy details to clipboard");

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&reportButton).withMinimumWidth(240),
                               [safeThis = juce::Component::SafePointer<SupportFooter>(this)](int result)
                               {
                                   if (safeThis != nullptr && result != 0)
                                       safeThis->handleChoice(result);
                               });
        }

        void handleChoice(int result)
        {
            const auto details = getDiagnostics ? getDiagnostics() : juce::String();

            if (result == 1)
                juce::Process::openDocument(support::githubIssueUrl(details), {});
            else if (result == 2)
                juce::Process::openDocument(support::mailtoUrl(details), {});
            else if (result == 3)
                copyToClipboard(details);
        }

        void copyToClipboard(const juce::String& details)
        {
            juce::SystemClipboard::copyTextToClipboard(details);

            // Say so on the button for a moment, so the click does not seem to do nothing
            reportButton.setButtonText("Details copied");
            juce::Timer::callAfterDelay(1800, [safeThis = juce::Component::SafePointer<SupportFooter>(this)]()
            {
                if (safeThis != nullptr)
                    safeThis->reportButton.setButtonText("Report a bug");
            });
        }

        LinkButton reportButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SupportFooter)
    };
}
