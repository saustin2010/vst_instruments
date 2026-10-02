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
#ifndef GRAPHICAL_STEP_SEQUENCER_H
#define GRAPHICAL_STEP_SEQUENCER_H

#include "JuceHeader.h"
#include "mopo.h"
#include "synth_slider.h"
#include <vector>

class GraphicalStepSequencer : public juce::Component, public juce::Timer, public juce::Slider::Listener,
                               public SynthSlider::SliderListener {
  public:
    GraphicalStepSequencer();
    ~GraphicalStepSequencer();

    void timerCallback() override;
    void setNumStepsSlider(SynthSlider* num_steps_slider);
    void setStepSliders(std::vector<juce::Slider*> sliders);
    void sliderValueChanged(juce::Slider* moved_slider) override;
    void guiChanged(SynthSlider* slider) override;

    void resetBackground();
    void showRealtimeFeedback(bool show_feedback = true);

    void paint(juce::Graphics& g) override;
    void paintBackground(juce::Graphics& g);
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

  private:
    int getHoveredStep(juce::Point<int> position);
    void updateHover(int step_index);
    void changeStep(const juce::MouseEvent& e);
    void ensureMinSize();

    int num_steps_;
    mopo::Output* step_generator_output_;
    int last_step_;
    SynthSlider* num_steps_slider_;
    int highlighted_step_;
    std::vector<juce::Slider*> sequence_;
    juce::Point<int> last_edit_position_;

    juce::Image background_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphicalStepSequencer)
};

#endif // GRAPHICAL_STEP_SEQUENCER_H
