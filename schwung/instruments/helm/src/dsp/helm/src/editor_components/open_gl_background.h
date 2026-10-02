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
#ifndef OPEN_GL_BACKGROUND_H
#define OPEN_GL_BACKGROUND_H

#include "JuceHeader.h"

class OpenGLBackground {
  public:
    OpenGLBackground();
    virtual ~OpenGLBackground();

    void updateBackgroundImage(juce::Image background);
    virtual void init(juce::OpenGLContext& open_gl_context);
    virtual void render(juce::OpenGLContext& open_gl_context);
    virtual void destroy(juce::OpenGLContext& open_gl_context);

    juce::OpenGLShaderProgram* shader() { return image_shader_; }
    juce::OpenGLShaderProgram::Uniform* texture_uniform() { return texture_uniform_; }

    void bind(juce::OpenGLContext& open_gl_context);
    void enableAttributes(juce::OpenGLContext& open_gl_context);
    void disableAttributes(juce::OpenGLContext& open_gl_context);

  private:
    juce::ScopedPointer<juce::OpenGLShaderProgram> image_shader_;
    juce::ScopedPointer<juce::OpenGLShaderProgram::Uniform> texture_uniform_;
    juce::ScopedPointer<juce::OpenGLShaderProgram::Attribute> position_;
    juce::ScopedPointer<juce::OpenGLShaderProgram::Attribute> texture_coordinates_;

    float vertices_[16];

    juce::OpenGLTexture background_;
    bool new_background_;
    juce::Image background_image_;

    GLuint vertex_buffer_;
    GLuint triangle_buffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGLBackground)
};

#endif // OPEN_GL_BACKGROUND_H
