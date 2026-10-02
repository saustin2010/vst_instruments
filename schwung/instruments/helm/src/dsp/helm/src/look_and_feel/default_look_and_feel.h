/* Copyright 2013-2017 Matt Tytel
 *
 * helm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * helm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with helm.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DEFAULT_LOOK_AND_FEEL_H
#define DEFAULT_LOOK_AND_FEEL_H

#include "JuceHeader.h"

class DefaultLookAndFeel : public juce::LookAndFeel_V3 {
  public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float slider_pos, float min, float max,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawLinearSliderThumb(juce::Graphics& g, int x, int y, int width, int height,
                               float slider_pos, float min, float max,
                               const juce::Slider::SliderStyle style, juce::Slider& slider) override;

    virtual void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                  float slider_t, float start_angle, float end_angle,
                                  juce::Slider& slider) override;

    virtual void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                  bool hover, bool is_down) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour &backgroundColour,
                              bool hover,
                              bool is_down) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool hover, bool is_down) override;

    void fillHorizontalRect(juce::Graphics& g, float x1, float x2, float height);
    void fillVerticalRect(juce::Graphics& g, float y1, float y2, float width);
    void fillSplitHorizontalRect(juce::Graphics& g, float x1, float x2, float height, juce::Colour fill_color);
    void fillSplitVerticalRect(juce::Graphics& g, float y1, float y2, float width, juce::Colour fill_color);

    int getSliderPopupPlacement(juce::Slider& slider) override;

    juce::Font getPopupMenuFont() override;
    juce::Font getSliderPopupFont(juce::Slider& slider) override;

    static DefaultLookAndFeel* instance() {
      static DefaultLookAndFeel instance;
      return &instance;
    }

  protected:
    DefaultLookAndFeel();
};

#endif // DEFAULT_LOOK_AND_FEEL_H
