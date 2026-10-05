#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
    // Colours: the TrailblaiZ brand palette. Surfaces and text are brand neutrals and
    // purples; the data colours are the brand hues, in a fixed order.
    //
    // Checked with the data-viz palette validator against the surface: the five series
    // colours pass chroma, colour-blind separation (worst neighbours: dE 18.7 where 8 is
    // the target), normal-vision separation and 3:1 contrast. They fail only the
    // validator's "lightness band", because the neon brand hues are brighter than its
    // recommended range. That is a visual-balance guideline, not a legibility one.
    // The brand has four data hues, so the fifth series (the rating) is a derived yellow.
    namespace palette
    {
        // Surfaces
        inline const juce::Colour window        { 0xff0a0a0f };
        inline const juce::Colour surface       { 0xff151520 };
        inline const juce::Colour surfaceRaised { 0xff2d1b4e }; // Brand "Russian Violet"
        inline const juce::Colour outline       { 0xff2a2a35 };
        inline const juce::Colour grid          { 0xff22222d };

        // Text (all at least 4.5:1 on the surface and on the raised surface)
        inline const juce::Colour textPrimary   { 0xffe8e8f0 };
        inline const juce::Colour textSecondary { 0xffb9b9cb };
        inline const juce::Colour textMuted     { 0xff9393ab };

        // Brand hues
        inline const juce::Colour brandOrange   { 0xffff5e00 };
        inline const juce::Colour brandPink     { 0xffff2e88 };
        inline const juce::Colour brandCyan     { 0xff00e5ff };
        inline const juce::Colour brandViolet   { 0xff6b4fff };

        // Interactive accent: focus, toggled buttons, selection
        inline const juce::Colour accent        = brandCyan;

        // Data series, in this order, never cycled
        inline const juce::Colour seriesCyan    = brandCyan;
        inline const juce::Colour seriesOrange  = brandOrange;
        inline const juce::Colour seriesViolet  = brandViolet;
        inline const juce::Colour seriesPink    = brandPink;
        inline const juce::Colour seriesYellow  { 0xffffc400 }; // Derived: the brand has no fifth hue

        // Gauge zones and recording. Always shown with an icon and a label, never colour alone.
        inline const juce::Colour good          = brandCyan;    // Low activation, calming
        inline const juce::Colour warning       = brandViolet;  // Medium activation, neutral
        inline const juce::Colour critical      = brandPink;    // High activation, stimulating; also "recording"

        // The brand gradient, orange to pink, for t from 0 to 1
        inline juce::Colour brandGradient(float t) { return brandOrange.interpolatedWith(brandPink, juce::jlimit(0.0f, 1.0f, t)); }
    }

    inline juce::Font font(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline int textWidth(const juce::Font& f, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidthInt(f, text);
    }

    // Drawing helpers shared by the components -------------------------------------

    inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = bounds.toFloat().reduced(0.5f);
        g.setColour(palette::surface);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(palette::outline);
        g.drawRoundedRectangle(r, 10.0f, 1.0f);
    }

    inline void drawHairline(juce::Graphics& g, float x1, float x2, float y, juce::Colour colour)
    {
        g.setColour(colour);
        g.fillRect(juce::Rectangle<float>(x1, std::floor(y), x2 - x1, 1.0f));
    }

    // A dot with a surface-coloured ring, so it stays legible over lines
    inline void drawRingedDot(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour)
    {
        g.setColour(palette::surface);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f + 4.0f, radius * 2.0f + 4.0f).withCentre(centre));
        g.setColour(colour);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
    }

    // Look and feel: flat rounded buttons and tooltips in the plugin's palette
    class AcousticLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        AcousticLookAndFeel()
        {
            setColour(juce::ResizableWindow::backgroundColourId, palette::window);
            setColour(juce::TooltipWindow::backgroundColourId, palette::surfaceRaised);
            setColour(juce::TooltipWindow::textColourId, palette::textPrimary);
            setColour(juce::TooltipWindow::outlineColourId, palette::outline);
            setColour(juce::TextButton::buttonColourId, palette::surfaceRaised);
            setColour(juce::TextButton::buttonOnColourId, palette::surfaceRaised);
            setColour(juce::TextButton::textColourOffId, palette::textPrimary);
            setColour(juce::TextButton::textColourOnId, palette::textPrimary);

            setColour(juce::Slider::textBoxTextColourId, palette::textPrimary);
            setColour(juce::Slider::textBoxBackgroundColourId, palette::surfaceRaised);
            setColour(juce::Slider::textBoxOutlineColourId, palette::outline);
            setColour(juce::Slider::textBoxHighlightColourId, palette::accent.withAlpha(0.4f));
            setColour(juce::Label::textColourId, palette::textSecondary);

            setColour(juce::ComboBox::backgroundColourId, palette::surfaceRaised);
            setColour(juce::ComboBox::textColourId, palette::textPrimary);
            setColour(juce::ComboBox::outlineColourId, palette::outline);
            setColour(juce::ComboBox::arrowColourId, palette::textSecondary);
            setColour(juce::ComboBox::focusedOutlineColourId, palette::accent);
            setColour(juce::PopupMenu::backgroundColourId, palette::surfaceRaised);
            setColour(juce::PopupMenu::textColourId, palette::textPrimary);
            setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::accent.withAlpha(0.3f));
            setColour(juce::PopupMenu::highlightedTextColourId, palette::textPrimary);

            setColour(juce::ToggleButton::textColourId, palette::textPrimary);
            setColour(juce::ToggleButton::tickColourId, palette::accent);
            setColour(juce::ToggleButton::tickDisabledColourId, palette::textMuted);

            setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            setColour(juce::TableHeaderComponent::backgroundColourId, palette::surfaceRaised);
            setColour(juce::TableHeaderComponent::textColourId, palette::textSecondary);
            setColour(juce::TableHeaderComponent::outlineColourId, palette::outline);
            setColour(juce::TableHeaderComponent::highlightColourId, palette::surfaceRaised);
        }

        juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(14.0f, true); }

        void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                  bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

            auto fill = backgroundColour;
            if (!button.isEnabled())
                fill = fill.withMultipliedAlpha(0.5f);
            else if (shouldDrawButtonAsDown)
                fill = fill.darker(0.2f);
            else if (shouldDrawButtonAsHighlighted)
                fill = fill.brighter(0.15f);

            g.setColour(fill);
            g.fillRoundedRectangle(bounds, 8.0f);

            // A toggled-on button (e.g. Settings) gets an accent outline
            g.setColour(button.getToggleState() ? palette::accent : palette::outline);
            g.drawRoundedRectangle(bounds, 8.0f, button.getToggleState() ? 1.5f : 1.0f);
        }

        // Thin track, accent fill, round thumb with a surface ring
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                              float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                              juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal)
            {
                juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
                return;
            }

            const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
            const auto track = juce::Rectangle<float>(static_cast<float>(x), centreY - 2.0f, static_cast<float>(width), 4.0f);

            g.setColour(palette::surfaceRaised);
            g.fillRoundedRectangle(track, 2.0f);

            if (slider.isEnabled())
                g.setGradientFill(juce::ColourGradient(palette::brandOrange, track.getX(), 0.0f,
                                                       palette::brandPink, track.getRight(), 0.0f, false));
            else
                g.setColour(palette::textMuted);

            g.fillRoundedRectangle(track.withRight(sliderPos), 2.0f);

            drawRingedDot(g, { sliderPos, centreY }, 7.0f, palette::textPrimary);
        }
    };
}
