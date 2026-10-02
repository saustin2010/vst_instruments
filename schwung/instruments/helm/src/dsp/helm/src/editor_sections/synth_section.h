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
#ifndef SYNTH_SECTION_H
#define SYNTH_SECTION_H

#include "JuceHeader.h"
#include "modulation_button.h"
#include "helm_common.h"
#include "synth_button.h"
#include <map>

class OpenGLComponent;
class SynthSlider;

class SynthSection : public juce::Component, public juce::Slider::Listener,
                     public juce::Button::Listener, public SynthButton::ButtonListener {
  public:
    SynthSection(juce::String name) : Component(name), activator_(nullptr), size_ratio_(1.0f) { }

    // Drawing.
    virtual void reset();
    virtual void resized() override;
    virtual void paint(juce::Graphics& g) override;
    virtual void paintBackground(juce::Graphics& g);
    virtual void paintContainer(juce::Graphics& g);
    virtual void setSizeRatio(float ratio);
    void paintKnobShadows(juce::Graphics& g);
    void drawTextForComponent(juce::Graphics& g, juce::String text, Component* component, int space = 4);

    void paintChildrenBackgrounds(juce::Graphics& g);
    void paintChildBackground(juce::Graphics& g, SynthSection* child);
    void paintOpenGLBackground(juce::Graphics& g, OpenGLComponent* child);
    void initOpenGLComponents(juce::OpenGLContext& open_gl_context);
    void renderOpenGLComponents(juce::OpenGLContext& open_gl_context, bool animate);
    void destroyOpenGLComponents(juce::OpenGLContext& open_gl_context);

    // Widget Listeners.
    virtual void sliderValueChanged(juce::Slider* moved_slider) override;
    virtual void buttonClicked(juce::Button* clicked_button) override;
    virtual void guiChanged(SynthButton* button) override;

    std::map<std::string, SynthSlider*> getAllSliders() { return all_sliders_; }
    std::map<std::string, juce::Button*> getAllButtons() { return all_buttons_; }
    std::map<std::string, ModulationButton*> getAllModulationButtons() {
      return all_modulation_buttons_;
    }

    virtual void setActive(bool active = true);
    virtual void animate(bool animate = true);
    virtual void setAllValues(mopo::control_map& controls);
    virtual void setValue(const std::string& name, mopo::mopo_float value,
                          juce::NotificationType notification = juce::sendNotification);

  protected:
    void addButton(juce::Button* button, bool show = true);
    void addModulationButton(ModulationButton* button, bool show = true);
    void addSlider(SynthSlider* slider, bool show = true);
    void addSubSection(SynthSection* section, bool show = true);
    void addOpenGLComponent(OpenGLComponent* open_gl_component);
    void setActivator(SynthButton* activator);
    float getTitleWidth();
    float getStandardKnobSize();
    float getSmallKnobSize();
    float getModButtonWidth();

    std::map<std::string, SynthSection*> sub_sections_;
    std::set<OpenGLComponent*> open_gl_components_;

    std::map<std::string, SynthSlider*> slider_lookup_;
    std::map<std::string, juce::Button*> button_lookup_;
    std::map<std::string, ModulationButton*> modulation_buttons_;

    std::map<std::string, SynthSlider*> all_sliders_;
    std::map<std::string, juce::Button*> all_buttons_;
    std::map<std::string, ModulationButton*> all_modulation_buttons_;
    juce::ToggleButton* activator_;

    juce::Image background_;
    float size_ratio_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthSection)
};

#endif // SYNTH_SECTION_H
