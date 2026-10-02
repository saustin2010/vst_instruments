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
#ifndef PATCH_SELECTOR_H
#define PATCH_SELECTOR_H

#include "JuceHeader.h"
#include "patch_browser.h"
#include "synth_section.h"

class PatchSelector : public SynthSection, public PatchBrowser::PatchSelectedListener {
  public:
    PatchSelector();
    ~PatchSelector();

    void paint(juce::Graphics& g) override;
    void paintBackground(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void buttonClicked(juce::Button* buttonThatWasClicked) override;
    void newPatchSelected(juce::File patch) override;
    void setModified(bool modified);
    void setSaveSection(SaveSection* save_section) { save_section_ = save_section; }
    void setBrowser(PatchBrowser* browser) {
      browser_ = browser;
      browser_->setListener(this);
    }
    int getBrowseHeight();

    void initPatch();

  private:
    void loadFromFile(juce::File& patch);

    juce::String folder_text_;
    juce::String patch_text_;

    juce::ScopedPointer<juce::TextButton> prev_patch_;
    juce::ScopedPointer<juce::TextButton> next_patch_;
    juce::ScopedPointer<juce::TextButton> save_;
    juce::ScopedPointer<juce::TextButton> export_;
    juce::ScopedPointer<juce::TextButton> browse_;
    PatchBrowser* browser_;
    SaveSection* save_section_;
    bool modified_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchSelector)
};

#endif // PATCH_SELECTOR_H
