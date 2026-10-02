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
#ifndef CONTRIBUTE_SECTION_H
#define CONTRIBUTE_SECTION_H

#include "JuceHeader.h"
#include "overlay.h"

class CustomAmountEditor : public juce::TextEditor {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
        virtual void textEditorFocusGained(FocusChangeType cause) { }
    };

    CustomAmountEditor(juce::String name) : TextEditor(name) { }

    void focusGained(FocusChangeType cause) override {
      for (Listener* listener: listeners_)
        listener->textEditorFocusGained(cause);
      TextEditor::focusGained(cause);
    }

    void addFocusListener(Listener* listener) {
      listeners_.push_back(listener);
    }

  private:
    std::vector<Listener*> listeners_;
};

class ContributeSection : public Overlay, public juce::Button::Listener, CustomAmountEditor::Listener {
  public:
    ContributeSection(juce::String name);
    ~ContributeSection() { }
    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::Rectangle<int> getInfoRect();

    void mouseUp(const juce::MouseEvent& e) override;
    void buttonClicked(juce::Button* clicked_button) override;

    void textEditorFocusGained(FocusChangeType cause) override;

  private:
    juce::URL getUrl();

    juce::ScopedPointer<juce::Button> give_100_button_;
    juce::ScopedPointer<juce::Button> give_50_button_;
    juce::ScopedPointer<juce::Button> give_25_button_;
    juce::ScopedPointer<juce::Button> give_10_button_;
    juce::ScopedPointer<juce::Button> give_5_button_;
    std::set<juce::Button*> give_buttons_;
    juce::ScopedPointer<CustomAmountEditor> custom_amount_;

    juce::ScopedPointer<juce::Button> remind_button_;
    juce::ScopedPointer<juce::Button> never_again_button_;
    juce::ScopedPointer<juce::Button> pay_button_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContributeSection)
};

#endif // CONTRIBUTE_SECTION_H
