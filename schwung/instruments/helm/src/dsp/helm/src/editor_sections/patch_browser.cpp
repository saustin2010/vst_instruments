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

#include "patch_browser.h"

#include "colors.h"
#include "browser_look_and_feel.h"
#include "fonts.h"
#include "helm_common.h"
#include "load_save.h"
#include "synth_gui_interface.h"

#define BANKS_WIDTH_PERCENT 0.23
#define FOLDERS_WIDTH_PERCENT 0.2
#define PATCHES_WIDTH_PERCENT 0.285
#define PATCH_INFO_WIDTH_PERCENT 0.285
#define TEXT_PADDING 4.0f
#define LINUX_SYSTEM_PATCH_DIRECTORY "/usr/share/helm/patches"
#define LINUX_USER_PATCH_DIRECTORY "~/.helm/User Patches"
#define BROWSING_HEIGHT 430.0f
#define BROWSE_PADDING 8.0f
#define BUTTON_HEIGHT 30.0f


namespace {
  juce::Array<juce::File> getSelectedFolders(juce::ListBox* view, FileListBoxModel* model) {
    juce::SparseSet<int> selected_rows = view->getSelectedRows();

    juce::Array<juce::File> selected_folders;
    for (int i = 0; i < selected_rows.size(); ++i)
      selected_folders.add(model->getFileAtRow(selected_rows[i]));

    return selected_folders;
  }

  juce::Array<juce::File> getFoldersToScan(juce::ListBox* view, FileListBoxModel* model) {
    if (view->getSelectedRows().size())
      return getSelectedFolders(view, model);

    return model->getAllFiles();
  }

  void setSelectedRows(juce::ListBox* view, FileListBoxModel* model, juce::Array<juce::File> selected) {
    view->deselectAllRows();

    for (int sel = 0, row = 0; sel < selected.size() && row < model->getNumRows();) {
      juce::String selected_path = selected[sel].getFullPathName();
      int compare = model->getFileAtRow(row).getFullPathName().compare(selected_path);

      if (compare < 0)
        ++row;
      else if (compare > 0)
        ++sel;
      else {
        view->selectRow(row, true, false);
        ++row;
        ++sel;
      }
    }
  }
} // namespace

PatchBrowser::PatchBrowser() : Overlay("patch_browser") {
  listener_ = nullptr;
  save_section_ = nullptr;
  delete_section_ = nullptr;

  banks_model_ = new FileListBoxModel();
  banks_model_->setListener(this);
  juce::Array<juce::File> bank_locations;
  juce::File bank_dir = LoadSave::getBankDirectory();
  bank_locations.add(bank_dir);
  banks_model_->rescanFiles(bank_locations);

  banks_view_ = new juce::ListBox("banks", banks_model_);
  banks_view_->setMultipleSelectionEnabled(false);
  banks_view_->setClickingTogglesRowSelection(true);
  banks_view_->updateContent();
  addAndMakeVisible(banks_view_);

  folders_model_ = new FileListBoxModel();
  folders_model_->setListener(this);

  folders_view_ = new juce::ListBox("folders", folders_model_);
  folders_view_->setMultipleSelectionEnabled(true);
  folders_view_->setClickingTogglesRowSelection(true);
  folders_view_->updateContent();
  addAndMakeVisible(folders_view_);

  patches_model_ = new FileListBoxModel();
  patches_model_->setListener(this);

  patches_view_ = new juce::ListBox("patches", patches_model_);
  patches_view_->updateContent();
  addAndMakeVisible(patches_view_);

  banks_view_->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff323232));
  folders_view_->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff323232));
  patches_view_->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff323232));

  search_box_ = new juce::TextEditor("Search");
  search_box_->addListener(this);
  search_box_->setSelectAllWhenFocused(true);
  search_box_->setTextToShowWhenEmpty(TRANS("Search"), juce::Colour(0xff777777));
  search_box_->setFont(Fonts::instance()->proportional_light().withPointHeight(16.0f));
  search_box_->setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff888888));
  search_box_->setColour(juce::TextEditor::textColourId, juce::Colour(0xffcccccc));
  search_box_->setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xffcccccc));
  search_box_->setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff888888));
  search_box_->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff323232));
  search_box_->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff888888));
  search_box_->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff888888));
  addAndMakeVisible(search_box_);

  import_bank_button_ = new juce::TextButton(TRANS("Import Bank"));
  import_bank_button_->addListener(this);
  addAndMakeVisible(import_bank_button_);

  export_bank_button_ = new juce::TextButton(TRANS("Export Bank"));
  export_bank_button_->addListener(this);
  addAndMakeVisible(export_bank_button_);
  export_bank_button_->setEnabled(false);

  selectedFilesChanged(banks_model_);
  selectedFilesChanged(folders_model_);

  cc_license_link_ = new juce::HyperlinkButton("CC-BY",
                                         juce::URL("https://creativecommons.org/licenses/by/4.0/"));
  cc_license_link_->setFont(Fonts::instance()->monospace().withPointHeight(12.0f),
                            false, juce::Justification::centredLeft);
  cc_license_link_->setColour(juce::HyperlinkButton::textColourId, juce::Colour(0xffffd740));
  addAndMakeVisible(cc_license_link_);

  gpl_license_link_ = new juce::HyperlinkButton("GPL-3",
                                          juce::URL("http://www.gnu.org/licenses/gpl-3.0.en.html"));
  gpl_license_link_->setFont(Fonts::instance()->monospace().withPointHeight(12.0f),
                             false, juce::Justification::centredLeft);
  gpl_license_link_->setColour(juce::HyperlinkButton::textColourId, juce::Colour(0xffffd740));
  addAndMakeVisible(gpl_license_link_);

  save_as_button_ = new juce::TextButton(TRANS("Save Patch As"));
  save_as_button_->addListener(this);
  addAndMakeVisible(save_as_button_);

  delete_patch_button_ = new juce::TextButton(TRANS("Delete Patch"));
  delete_patch_button_->addListener(this);
  addAndMakeVisible(delete_patch_button_);

  hide_button_ = new juce::TextButton("X");
  hide_button_->addListener(this);
  addAndMakeVisible(hide_button_);

  done_button_ = new juce::TextButton("Done");
  done_button_->addListener(this);
  addAndMakeVisible(done_button_);

  addKeyListener(this);
}

PatchBrowser::~PatchBrowser() {
}

void PatchBrowser::paint(juce::Graphics& g) {
  g.fillAll(Colors::overlay_screen);
  g.setColour(juce::Colour(0xff111111));
  g.fillRect(0.0f, 0.0f, 1.0f * getWidth(), size_ratio_ * BROWSING_HEIGHT);

  g.setColour(Colors::background);
  float info_width = getPatchInfoWidth();
  juce::Rectangle<int> data_rect(getWidth() - info_width - BROWSE_PADDING, BROWSE_PADDING,
                           info_width, size_ratio_ * BROWSING_HEIGHT - 2.0f * BROWSE_PADDING);
  g.fillRect(data_rect);

  if (isPatchSelected()) {
    float data_x = data_rect.getX();
    float division = size_ratio_ * 90.0f;
    float buffer = 20.0f;

    g.setFont(Fonts::instance()->proportional_light().withPointHeight(14.0f));
    g.setColour(juce::Colour(0xff888888));

    g.fillRect(data_x + division + buffer / 2.0f, BROWSE_PADDING + 70.0f,
               1.0f, 120.0f);

    g.drawText(TRANS("AUTHOR"),
               data_x, BROWSE_PADDING + 80.0f, division, 20.0f,
               juce::Justification::centredRight, false);
    g.drawText(TRANS("BANK"),
               data_x, BROWSE_PADDING + 120.0f, division, 20.0f,
               juce::Justification::centredRight, false);
    g.drawText(TRANS("LICENSE"),
               data_x, BROWSE_PADDING + 160.0f, division, 20.0f,
               juce::Justification::centredRight, false);

    g.setFont(Fonts::instance()->monospace().withPointHeight(16.0f));
    g.setColour(Colors::audio);

    juce::File selected_patch = getSelectedPatch();
    g.drawFittedText(selected_patch.getFileNameWithoutExtension(),
                     data_x + 2.0f * BROWSE_PADDING, 4.0f * BROWSE_PADDING,
                     info_width - 2.0f * BROWSE_PADDING, 20.0f,
                     juce::Justification::centred, true);

    g.setFont(Fonts::instance()->monospace().withPointHeight(12.0f));
    g.setColour(Colors::control_label_text);

    float data_width = info_width - division - buffer - 2.0f * BROWSE_PADDING;
    g.drawText(author_,
               data_x + division + buffer, BROWSE_PADDING + 80.0f, data_width, 20.0f,
               juce::Justification::centredLeft, true);
    g.drawText(selected_patch.getParentDirectory().getParentDirectory().getFileName(),
               data_x + division + buffer, BROWSE_PADDING + 120.0f, data_width, 20.0f,
               juce::Justification::centredLeft, true);
  }
}

void PatchBrowser::resized() {
  float start_x = BROWSE_PADDING;
  int button_padding = BROWSE_PADDING / 2;

  int banks_width = getBanksWidth();
  int folders_width = getFoldersWidth();
  int patches_width = getPatchesWidth();
  int patch_info_width = getPatchInfoWidth();
  float height = size_ratio_ * BROWSING_HEIGHT - 2.0f * BROWSE_PADDING;
  float height_with_buttons = height - BUTTON_HEIGHT - button_padding;
  float search_box_height = 28.0f;

  int import_export_width = (banks_width - button_padding) / 2.0f;
  banks_view_->setBounds(start_x, BROWSE_PADDING, banks_width, height_with_buttons);
  import_bank_button_->setBounds(start_x, banks_view_->getBottom() + button_padding,
                                 import_export_width, BUTTON_HEIGHT);
  export_bank_button_->setBounds(start_x + banks_width - import_export_width,
                                 banks_view_->getBottom() + button_padding,
                                 import_export_width, BUTTON_HEIGHT);
  folders_view_->setBounds(banks_view_->getRight() + BROWSE_PADDING,
                           BROWSE_PADDING, folders_width, height);

  float patches_x = folders_view_->getRight() + BROWSE_PADDING;
  search_box_->setBounds(patches_x, BROWSE_PADDING, patches_width, search_box_height);
  patches_view_->setBounds(patches_x, search_box_height + BROWSE_PADDING,
                           patches_width, height_with_buttons - search_box_height);

  float button_width = (patches_width - button_padding) / 2.0f;
  save_as_button_->setBounds(patches_view_->getX(), patches_view_->getBottom() + button_padding,
                             button_width, BUTTON_HEIGHT);
  delete_patch_button_->setBounds(patches_view_->getRight() - button_width,
                                  patches_view_->getBottom() + button_padding,
                                  button_width, BUTTON_HEIGHT);
  delete_patch_button_->setEnabled(false);

  float data_x = patches_view_->getRight() + BROWSE_PADDING;
  float division = size_ratio_ * 90.0f;
  float buffer = 20.0f;
  cc_license_link_->setBounds(data_x + division + buffer, BROWSE_PADDING + 160.0f,
                              200.0f, 20.0f);
  gpl_license_link_->setBounds(data_x + division + buffer, BROWSE_PADDING + 160.0f,
                               200.0f, 20.0f);

  hide_button_->setBounds(getWidth() - 21 - BROWSE_PADDING, BROWSE_PADDING, 20, 20);
  int done_width = size_ratio_ * 200;
  int done_height = size_ratio_ * 1.5 * BUTTON_HEIGHT;
  int padding = size_ratio_ * (patch_info_width - done_width) / 2;

  done_button_->setBounds(data_x + padding,
                          save_as_button_->getBottom() - BROWSE_PADDING - done_height,
                          done_width, done_height);
}

void PatchBrowser::visibilityChanged() {
  Overlay::visibilityChanged();
  if (isVisible()) {
    search_box_->setText("");
    search_box_->grabKeyboardFocus();

    bool is_cc = license_.contains("creativecommons");
    cc_license_link_->setVisible(isPatchSelected() && is_cc);
    gpl_license_link_->setVisible(isPatchSelected() && !is_cc);
  }
}

void PatchBrowser::selectedFilesChanged(FileListBoxModel* model) {
  if (model == banks_model_) {
    scanFolders();
    export_bank_button_->setEnabled(banks_view_->getSelectedRows().size() == 1);
  }
  if (model == banks_model_ || model == folders_model_)
    scanPatches();
  else if (model == patches_model_) {
    juce::SparseSet<int> selected_rows = patches_view_->getSelectedRows();
    delete_patch_button_->setEnabled(selected_rows.size());

    if (selected_rows.size()) {
      external_patch_ = juce::File();
      juce::File patch = patches_model_->getFileAtRow(selected_rows[0]);
      loadFromFile(patch);

      if (listener_)
        listener_->newPatchSelected(patch);
    }
    else {
      cc_license_link_->setVisible(false);
      gpl_license_link_->setVisible(false);
    }
    repaint();
  }
}

void PatchBrowser::textEditorTextChanged(juce::TextEditor& editor) {
  scanPatches();
}

void PatchBrowser::textEditorEscapeKeyPressed(juce::TextEditor& editor) {
  if (isVisible())
    setVisible(false);
}

void PatchBrowser::fileSaved(juce::File saved_file) {
  patches_view_->deselectAllRows();
  folders_view_->deselectAllRows();
  banks_view_->deselectAllRows();
  scanAll();
  int index = patches_model_->getIndexOfFile(saved_file);
  patches_view_->selectRow(index);
}

void PatchBrowser::fileDeleted(juce::File saved_file) {
  scanAll();
}

void PatchBrowser::buttonClicked(juce::Button* clicked_button) {
  if (clicked_button == save_as_button_ && save_section_)
    save_section_->setVisible(true);
  else if (clicked_button == delete_patch_button_ && delete_section_) {
    juce::File selected_patch = getSelectedPatch();
    if (selected_patch.exists()) {
      delete_section_->setFileToDelete(selected_patch);
      delete_section_->setVisible(true);
    }
  }
  else if (clicked_button == hide_button_ || clicked_button == done_button_)
    setVisible(false);
  else if (clicked_button == import_bank_button_) {
    LoadSave::importBank();
    scanAll();
  }
  else if (clicked_button == export_bank_button_) {
    juce::Array<juce::File> banks = getFoldersToScan(banks_view_, banks_model_);
    if (banks.size())
      LoadSave::exportBank(banks[0].getFileName());
  }
}

bool PatchBrowser::keyPressed(const juce::KeyPress &key, Component *origin) {
  if (key.getKeyCode() == juce::KeyPress::escapeKey && isVisible()) {
    setVisible(false);
    return true;
  }
  return search_box_->hasKeyboardFocus(true);
}

bool PatchBrowser::keyStateChanged(bool is_key_down, Component *origin) {
  if (is_key_down)
    return search_box_->hasKeyboardFocus(true);
  return false;
}

void PatchBrowser::mouseUp(const juce::MouseEvent& e) {
  if (e.getPosition().y > size_ratio_ * BROWSING_HEIGHT)
    setVisible(false);
}

bool PatchBrowser::isPatchSelected() {
  return external_patch_.exists() || patches_view_->getSelectedRows().size();
}

juce::File PatchBrowser::getSelectedPatch() {
  if (external_patch_.exists())
    return external_patch_;

  juce::SparseSet<int> selected_rows = patches_view_->getSelectedRows();
  if (selected_rows.size())
    return patches_model_->getFileAtRow(selected_rows[0]);
  return juce::File();
}

void PatchBrowser::jumpToPatch(int indices) {
  static const FileSorterAscending file_sorter;

  juce::File parent = external_patch_.getParentDirectory();
  if (parent.exists()) {
    juce::Array<juce::File> patches;
    parent.findChildFiles(patches, juce::File::findFiles, false, juce::String("*.") + mopo::PATCH_EXTENSION);
    patches.sort(file_sorter);
    int index = patches.indexOf(external_patch_);
    index = (index + indices + patches.size()) % patches.size();

    juce::File new_patch = patches[index];
    loadFromFile(new_patch);
    externalPatchLoaded(new_patch);
  }
  else {
    juce::SparseSet<int> selected_rows = patches_view_->getSelectedRows();
    if (selected_rows.size()) {
      int num_rows = patches_model_->getNumRows();
      int row = (selected_rows[0] + indices + num_rows) % num_rows;
      patches_view_->selectRow(row);
    }
    else
      patches_view_->selectRow(0);
  }
}

void PatchBrowser::loadPrevPatch() {
  jumpToPatch(-1);
}

void PatchBrowser::loadNextPatch() {
  jumpToPatch(1);
}

void PatchBrowser::externalPatchLoaded(juce::File file) {
  external_patch_ = file;
  patches_view_->deselectAllRows();
  setPatchInfo(file);
}

bool PatchBrowser::loadFromFile(juce::File& patch) {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return false;

  SynthBase* synth = parent->getSynth();
  if (synth->loadFromFile(patch)) {
    setPatchInfo(patch);
    synth->setPatchName(patch.getFileNameWithoutExtension());
    synth->setFolderName(patch.getParentDirectory().getFileName());
    synth->setAuthor(author_);
    return true;
  }
  return false;
}

void PatchBrowser::setPatchInfo(juce::File& patch) {
  juce::var parsed_json_state;
  if (patch.exists() && juce::JSON::parse(patch.loadFileAsString(), parsed_json_state).wasOk()) {
    author_ = LoadSave::getAuthor(parsed_json_state);
    license_ = LoadSave::getLicense(parsed_json_state);

    bool is_cc = license_.contains("creativecommons");
    cc_license_link_->setVisible(is_cc);
    gpl_license_link_->setVisible(!is_cc);
  }
}

void PatchBrowser::setSaveSection(SaveSection* save_section) {
  save_section_ = save_section;
  save_section_->setListener(this);
}

void PatchBrowser::setDeleteSection(DeleteSection* delete_section) {
  delete_section_ = delete_section;
  delete_section_->addDeleteListener(this);
  banks_model_->setDeleteSection(delete_section);
  folders_model_->setDeleteSection(delete_section);
  patches_model_->setDeleteSection(delete_section);
}

void PatchBrowser::scanBanks() {
  juce::Array<juce::File> top_level;
  top_level.add(LoadSave::getBankDirectory());
  juce::Array<juce::File> banks_selected = getSelectedFolders(banks_view_, banks_model_);

  banks_model_->rescanFiles(top_level);
  banks_view_->updateContent();
  setSelectedRows(banks_view_, banks_model_, banks_selected);
}

void PatchBrowser::scanFolders() {
  juce::Array<juce::File> banks = getFoldersToScan(banks_view_, banks_model_);
  juce::Array<juce::File> folders_selected = getSelectedFolders(folders_view_, folders_model_);

  folders_model_->rescanFiles(banks);
  folders_view_->updateContent();
  setSelectedRows(folders_view_, folders_model_, folders_selected);
}

void PatchBrowser::scanPatches() {
  juce::Array<juce::File> folders = getFoldersToScan(folders_view_, folders_model_);
  juce::Array<juce::File> patches_selected = getSelectedFolders(patches_view_, patches_model_);

  juce::String search = "*" + search_box_->getText() + "*." + mopo::PATCH_EXTENSION;
  patches_model_->rescanFiles(folders, search, true);
  patches_view_->updateContent();
  setSelectedRows(patches_view_, patches_model_, patches_selected);
}

void PatchBrowser::scanAll() {
  scanBanks();
  scanFolders();
  scanPatches();
}

float PatchBrowser::getBanksWidth() {
  return (getWidth() - 5.0f * BROWSE_PADDING) * BANKS_WIDTH_PERCENT;
}

float PatchBrowser::getFoldersWidth() {
  return (getWidth() - 5.0f * BROWSE_PADDING) * FOLDERS_WIDTH_PERCENT;
}

float PatchBrowser::getPatchesWidth() {
  return (getWidth() - 5.0f * BROWSE_PADDING) * PATCHES_WIDTH_PERCENT;
}

float PatchBrowser::getPatchInfoWidth() {
  return (getWidth() - 5.0f * BROWSE_PADDING) * PATCH_INFO_WIDTH_PERCENT;
}
