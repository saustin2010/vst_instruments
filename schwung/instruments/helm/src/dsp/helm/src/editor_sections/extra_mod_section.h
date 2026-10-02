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
#ifndef EXTRA_MOD_SECTION_H
#define EXTRA_MOD_SECTION_H

#include "JuceHeader.h"
#include "synth_section.h"
#include "synth_slider.h"

class ExtraModSection : public SynthSection {
  public:
    ExtraModSection(juce::String name);
    ~ExtraModSection();

    void paintBackground(juce::Graphics& g) override;
    void resized() override;
    void drawTextToRightOfComponent(juce::Graphics& g, Component* component, juce::String text);

  private:
    juce::ScopedPointer<ModulationButton> aftertouch_mod_;
    juce::ScopedPointer<ModulationButton> note_mod_;
    juce::ScopedPointer<ModulationButton> velocity_mod_;
    juce::ScopedPointer<ModulationButton> mod_wheel_mod_;
    juce::ScopedPointer<ModulationButton> pitch_wheel_mod_;
    juce::ScopedPointer<ModulationButton> random_mod_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExtraModSection)
};

#endif // EXTRA_MOD_SECTION_H
