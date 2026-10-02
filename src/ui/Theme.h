#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
    // Colours. Surfaces and text tokens are for the dark theme; the categorical
    // series colours are a fixed-order set validated (lightness band, chroma,
    // colour-blind separation, contrast) against the dark surface.
    namespace palette
    {
        inline const juce::Colour window        { 0xff121211 };
        inline const juce::Colour surface       { 0xff1a1a19 };
        inline const juce::Colour surfaceRaised { 0xff262624 };
        inline const juce::Colour outline       { 0xff32322f };
        inline const juce::Colour grid          { 0xff2c2c2a };

        inline const juce::Colour textPrimary   { 0xffffffff };
        inline const juce::Colour textSecondary { 0xffc3c2b7 };
        inline const juce::Colour textMuted     { 0xff8c8b82 };

        // Categorical series, in this order, never cycled
        inline const juce::Colour seriesBlue    { 0xff3987e5 };
        inline const juce::Colour seriesOrange  { 0xffd95926 };
        inline const juce::Colour seriesAqua    { 0xff199e70 };
        inline const juce::Colour seriesYellow  { 0xffc98500 };

        // Status colours are reserved for state and always come with an icon and a label
        inline const juce::Colour good          { 0xff0ca30c };
        inline const juce::Colour warning       { 0xfffab219 };
        inline const juce::Colour critical      { 0xffd03b3b };
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
            g.setColour(palette::outline);
            g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
        }
    };
}
