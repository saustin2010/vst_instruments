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

#pragma once
#ifndef WAVE_SELECTOR_H
#define WAVE_SELECTOR_H

#include "JuceHeader.h"
#include "synth_slider.h"

class WaveSelector : public SynthSlider {
  public:
    WaveSelector(juce::String name);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseEvent(const juce::MouseEvent& e);
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

  private:
    void resizeSin(float x, float y, float width, float height);
    void resizeTriangle(float x, float y, float width, float height);
    void resizeSquare(float x, float y, float width, float height);
    void resizeDownSaw(float x, float y, float width, float height);
    void resizeUpSaw(float x, float y, float width, float height);
    void resizeNoise(float x, float y, float width, float height);

    juce::Path sine_;
    juce::Path triangle_;
    juce::Path square_;
    juce::Path down_saw_;
    juce::Path up_saw_;
    juce::Path three_step_;
    juce::Path four_step_;
    juce::Path eight_step_;
    juce::Path three_pyramid_;
    juce::Path five_pyramid_;
    juce::Path nine_pyramid_;
    juce::Path noise_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveSelector)
};

#endif // WAVE_SELECTOR_H
