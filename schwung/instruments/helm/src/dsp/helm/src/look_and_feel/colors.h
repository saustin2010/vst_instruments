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
#ifndef COLORS_H
#define COLORS_H

#include "JuceHeader.h"

class Colors {
  public:
    static const juce::Colour background;
    static const juce::Colour tab_heading;
    static const juce::Colour tab_body;
    static const juce::Colour audio;
    static const juce::Colour modulation;
    static const juce::Colour graph_disable;
    static const juce::Colour graph_fill;
    static const juce::Colour tab_heading_text;
    static const juce::Colour control_label_text;
    static const juce::Colour info_background;
    static const juce::Colour overlay_screen;

  private:
    Colors() { }
};

#endif // COLORS_H
