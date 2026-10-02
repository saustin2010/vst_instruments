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

#include "modulation_look_and_feel.h"

#include "colors.h"
#include "fonts.h"
#include "modulation_slider.h"
#include "mopo.h"
#include "synth_gui_interface.h"
#include "text_look_and_feel.h"

ModulationLookAndFeel::ModulationLookAndFeel() {
  setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xff222222));
  setColour(juce::TooltipWindow::textColourId, juce::Colour(0xffdddddd));
}

void ModulationLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float slider_pos, float min, float max,
                                             const juce::Slider::SliderStyle style, juce::Slider& slider) {
  ModulationSlider* mod_slider = dynamic_cast<ModulationSlider*>(&slider);
  if (!mod_slider)
    return;
  juce::Slider* destination = mod_slider->getDestinationSlider();
  float destination_percentage = destination->valueToProportionOfLength(destination->getValue());
  float destination_range = destination->getMaximum() - destination->getMinimum();
  float mod_percentage = slider.getValue() / destination_range;

  if (mod_percentage == 0.0) {
    g.setColour(juce::Colour(0x11b9f6ca));
    g.fillRect(0, 0, slider.getWidth(), slider.getHeight());
    g.setColour(juce::Colour(0xffb9f6ca));
    g.drawRect(0, 0, slider.getWidth(), slider.getHeight(), 1);
  }
  else {
    g.setColour(juce::Colour(0x1100e676));
    g.fillRect(0, 0, slider.getWidth(), slider.getHeight());
    g.setColour(Colors::modulation);
    g.drawRect(0.0f, 0.0f, float(slider.getWidth()), float(slider.getHeight()), 2.5f);
  }

  g.setColour(juce::Colour(0x5500e676));

  if (style == juce::Slider::SliderStyle::LinearBar) {
    float destination_position = width * destination_percentage;
    float mod_diff = width * mod_percentage;
    float from = std::min<float>(destination_position + mod_diff, destination_position);
    float to = std::max<float>(destination_position + mod_diff, destination_position);

    g.fillRect(x + from, float(y), to - from, float(height));
    g.setColour(juce::Colour(0xffffffff));
    g.fillRect(x + destination_position + mod_diff, 1.0f * y, 2.0f, 1.0f * height);
  }
  else if (style == juce::Slider::SliderStyle::LinearBarVertical) {
    float destination_position = height * (1.0f - destination_percentage);
    float mod_diff = height * mod_percentage;
    float from = std::min<float>(destination_position - mod_diff, destination_position);
    float to = std::max<float>(destination_position - mod_diff, destination_position);

    g.fillRect(float(x), y + from, float(width), to - from);
    g.setColour(juce::Colour(0xffffffff));
    g.fillRect(float(x), y + destination_position - mod_diff, 1.0f * width, 2.0f);
  }
}

void ModulationLookAndFeel::drawLinearSliderThumb(juce::Graphics& g, int x, int y, int width, int height,
                                                  float slider_pos, float min, float max,
                                                  const juce::Slider::SliderStyle style, juce::Slider& slider) {
}

void ModulationLookAndFeel::drawTextModulation(juce::Graphics& g, juce::Slider& slider, float percent) {
  if (percent == 0.0f) {
    g.setColour(juce::Colour(0x11b9f6ca));
    g.fillRect(0, 0, slider.getWidth(), slider.getHeight());
    g.setColour(juce::Colour(0xffb9f6ca));
    g.drawRect(0, 0, slider.getWidth(), slider.getHeight(), 1);
  }
  else {
    g.setColour(juce::Colour(0x1100e676));
    g.fillRect(0, 0, slider.getWidth(), slider.getHeight());
    g.setColour(Colors::modulation);
    g.drawRect(0.0f, 0.0f, float(slider.getWidth()), float(slider.getHeight()), 2.5f);
  }

  g.setColour(juce::Colour(0x5500e676));

  if (percent < 0.0f) {
    g.fillRect(2.5f, 2.5f, slider.getWidth() - 5.0f, -percent * (slider.getHeight() - 5.0f));
  }
  else if (percent > 0.0f) {
    float height = percent * (slider.getHeight() - 5.0f);
    g.fillRect(2.5f, slider.getHeight() - height - 2.5f, slider.getWidth() - 5.0f, height);
  }
}

void ModulationLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float slider_t, float start_angle, float end_angle,
                                             juce::Slider& slider) {
  ModulationSlider* mod_slider = dynamic_cast<ModulationSlider*>(&slider);
  if (!mod_slider)
    return;

  SynthSlider* destination = mod_slider->getDestinationSlider();
  if (&destination->getLookAndFeel() == TextLookAndFeel::instance()) {
    float destination_range = destination->getMaximum() - destination->getMinimum();
    float mod_percentage = slider.getValue() / destination_range;
    drawTextModulation(g, slider, mod_percentage);
    return;
  }

  float destination_percentage = destination->valueToProportionOfLength(destination->getValue());
  float destination_range = destination->getMaximum() - destination->getMinimum();
  float destination_angle = start_angle + destination_percentage * (end_angle - start_angle);
  float mod_diff = slider.getValue() * (end_angle - start_angle) / destination_range;

  float draw_radius = std::min(width / 2.0f, height / 2.0f);
  float knob_radius = 0.65f * draw_radius;
  juce::PathStrokeType stroke_type =
      juce::PathStrokeType(knob_radius, juce::PathStrokeType::beveled, juce::PathStrokeType::butt);

  if (mod_diff == 0.0) {
    g.setColour(juce::Colour(0x33b9f6ca));
    g.fillEllipse(width / 2.0f - knob_radius, height / 2.0f - knob_radius,
                  2.0 * knob_radius, 2.0 * knob_radius);
    g.setColour(Colors::modulation);
    g.drawEllipse(width / 2.0f - knob_radius + 0.5f, height / 2.0f - knob_radius + 0.5f,
                  2.0f * knob_radius - 1.0f, 2.0f * knob_radius - 1.0f, 1.0f);
  }
  else {
    g.setColour(juce::Colour(0xaa00e676));
    g.fillEllipse(width / 2.0f - knob_radius, height / 2.0f - knob_radius,
                  2.0 * knob_radius, 2.0 * knob_radius);
    g.setColour(Colors::modulation);
    g.drawEllipse(width / 2.0f - knob_radius + 1.5f, height / 2.0f - knob_radius + 1.5f,
                  2.0f * knob_radius - 3.0f, 2.0f * knob_radius - 3.0f, 3.0f);
  }


  juce::Path active_section;
  float center_x = x + draw_radius;
  float center_y = y + draw_radius;
  if (destination_angle > mopo::PI)
    destination_angle -= 2.0f * static_cast<float>(mopo::PI);
  active_section.addCentredArc(center_x, center_y, knob_radius / 2.0, knob_radius / 2.0,
                               destination_angle, mod_diff, 0, true);

  g.setColour(juce::Colour(0xff69f0ae));
  g.strokePath(active_section, stroke_type);

  float end_x = draw_radius + 0.9f * knob_radius * sin(destination_angle + mod_diff);
  float end_y = draw_radius - 0.9f * knob_radius * cos(destination_angle + mod_diff);
  g.setColour(juce::Colour(0xffffffff));
  g.drawLine(draw_radius, draw_radius, end_x, end_y, 1.0f);
}

void ModulationLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                             bool hover, bool is_down) {
  static const juce::DropShadow shadow(juce::Colour(0x88000000), 2, juce::Point<int>(0, 0));

  static const juce::Image on_active_2x =
      juce::ImageCache::getFromMemory(BinaryData::modulation_selected_active_2x_png,
                                BinaryData::modulation_selected_active_2x_pngSize);
  static const juce::Image on_active_1x =
      juce::ImageCache::getFromMemory(BinaryData::modulation_selected_active_1x_png,
                                BinaryData::modulation_selected_active_1x_pngSize);
  static const juce::Image off_active_2x =
      juce::ImageCache::getFromMemory(BinaryData::modulation_unselected_active_2x_png,
                                BinaryData::modulation_unselected_active_2x_pngSize);
  static const juce::Image on_inactive_2x =
      juce::ImageCache::getFromMemory(BinaryData::modulation_selected_inactive_2x_png,
                                BinaryData::modulation_selected_inactive_2x_pngSize);
  static const juce::Image off_inactive_2x =
      juce::ImageCache::getFromMemory(BinaryData::modulation_unselected_inactive_2x_png,
                                BinaryData::modulation_unselected_inactive_2x_pngSize);


  g.saveState();
  float ratio = (1.0f * button.getWidth()) / on_active_1x.getWidth();
  g.addTransform(juce::AffineTransform::scale(ratio, ratio));
  shadow.drawForImage(g, on_active_1x);
  g.restoreState();

  juce::Image button_image;
  SynthGuiInterface* parent = button.findParentComponentOfClass<SynthGuiInterface>();
  if (parent && parent->getSynth()->getSourceConnections(button.getName().toStdString()).size()) {
    if (button.getToggleState())
      button_image = on_active_2x;
    else
      button_image = off_active_2x;
  }
  else {
    if (button.getToggleState())
      button_image = on_inactive_2x;
    else
      button_image = off_inactive_2x;
  }

  g.setColour(juce::Colours::white);
  g.drawImage(button_image,
              0, 0, button.getWidth(), button.getHeight(),
              0, 0, button_image.getWidth(), button_image.getHeight());

  if (is_down) {
    g.setColour(juce::Colour(0x11000000));
    g.fillEllipse(1, 2, button.getWidth() - 2, button.getHeight() - 2);
  }
  else if (hover) {
    g.setColour(juce::Colour(0x11ffffff));
    g.fillEllipse(1, 2, button.getWidth() - 2, button.getHeight() - 2);
  }
}

int ModulationLookAndFeel::getSliderPopupPlacement(juce::Slider& slider) {
  SynthSlider* s_slider = dynamic_cast<SynthSlider*>(&slider);
  if (s_slider)
    return s_slider->getPopupPlacement();

  return LookAndFeel_V3::getSliderPopupPlacement(slider);
}

juce::Font ModulationLookAndFeel::getPopupMenuFont() {
  return Fonts::instance()->proportional_regular().withPointHeight(14.0f);
}

juce::Font ModulationLookAndFeel::getSliderPopupFont(juce::Slider& slider) {
  return Fonts::instance()->proportional_regular().withPointHeight(14.0f);
}
