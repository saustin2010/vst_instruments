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

#include "bpm_section.h"

#include "bpm_slider.h"
#include "colors.h"
#include "fonts.h"
#include "bpm_slider.h"

#define SHADOW_WIDTH 3

BpmSection::BpmSection(juce::String name) : SynthSection(name) {
  addSlider(bpm_ = new BpmSlider("beats_per_minute"));
  bpm_->setSliderStyle(juce::Slider::LinearBarVertical);
  bpm_->setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
  bpm_->flipColoring(true);
  bpm_->setPopupPlacement(juce::BubbleComponent::below, 0);
}

BpmSection::~BpmSection() {
  bpm_ = nullptr;
}

void BpmSection::paintBackground(juce::Graphics& g) {
  SynthSection::paintContainer(g);
  float title_width = getTitleWidth();
  float shadow_left = title_width - size_ratio_ * SHADOW_WIDTH;

  g.setGradientFill(juce::ColourGradient(juce::Colour(0x22000000), shadow_left, 0.0f,
                                   juce::Colour(0x66000000), title_width, 0.0f,
                                   false));
  g.fillRoundedRectangle(0, 0, title_width, getHeight(), 1.0f);

  g.setColour(Colors::control_label_text);
  g.setFont(Fonts::instance()->proportional_regular().withPointHeight(size_ratio_ * 10.0f));

  g.saveState();
  g.addTransform(juce::AffineTransform::rotation(-static_cast<float>(mopo::PI) / 2.0f, 0, 0));
  g.setColour(Colors::tab_heading_text);
  g.setFont(Fonts::instance()->proportional_light().withPointHeight(size_ratio_ * 13.40f));
  g.drawText(TRANS("BPM"), -getHeight(), 0, getHeight(), title_width,
             juce::Justification::centred, false);
  g.restoreState();

  paintKnobShadows(g);
}

void BpmSection::resized() {
  int title_width = getTitleWidth();
  int bpm_width = getWidth() - title_width;
  bpm_->setBounds(title_width, 0, bpm_width, getHeight());
  SynthSection::resized();
}
