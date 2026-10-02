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

#ifndef FONTS_H
#define FONTS_H

#include "JuceHeader.h"

class Fonts {
  public:
    virtual ~Fonts() { }

    juce::Font& proportional_regular() { return proportional_regular_; }
    juce::Font& proportional_light() { return proportional_light_; }
    juce::Font& monospace() { return monospace_; }

    static Fonts* instance() {
      static Fonts instance;
      return &instance;
    }

  private:
    Fonts();

    static juce::ScopedPointer<Fonts> instance_;

    juce::Font proportional_regular_;
    juce::Font proportional_light_;
    juce::Font monospace_;
};

#endif // FONTS_H
