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

#include "startup.h"
#include "load_save.h"
#include "JuceHeader.h"
#include "synth_base.h"

#define OLD_LINUX_USER_BANK_DIRECTORY "~/.helm/User Patches"

namespace {
  juce::File getOldUserPatchesDirectory() {
    juce::File patch_dir = juce::File("");
#ifdef LINUX
    patch_dir = juce::File(OLD_LINUX_USER_BANK_DIRECTORY);
#elif defined(__APPLE__)
    juce::File data_dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    patch_dir = data_dir.getChildFile(juce::String("Audio/Presets/") + "Helm");
#elif defined(_WIN32)
    juce::File data_dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    patch_dir = data_dir.getChildFile("Helm/patches");
    juce::File folder_dir = patch_dir.getChildFile("User Patches");
#endif
    return patch_dir;
  }

  bool isBankDirectory(juce::File bank) {
    juce::Array<juce::File> patches;
    bank.findChildFiles(patches, juce::File::findFiles, true, juce::String("*.") + mopo::PATCH_EXTENSION);

    return patches.size() && patches[0].getParentDirectory().getParentDirectory() == bank;
  }
} // namespace

void Startup::doStartupChecks(MidiManager* midi_manager, mopo::StringLayout* layout) {
  if (!LoadSave::isInstalled())
    return;

  fixPatchesFolder();

  if (LoadSave::wasUpgraded()) {
    storeOldFactoryPatches();
    copyFactoryPatches();
    LoadSave::saveVersionConfig();
    LoadSave::saveLastAskedForMoney();
  }
  else
    LoadSave::loadConfig(midi_manager, layout);
}

bool Startup::isFirstStartup() {
  return !LoadSave::getConfigFile().exists();
}

void Startup::storeOldFactoryPatches() {
  juce::String old_version = LoadSave::loadVersion();

  if (LoadSave::isInstalled() && LoadSave::compareVersionStrings(old_version, "0.9.0") < 0) {
    juce::File bank_directory = LoadSave::getBankDirectory();
    juce::File old_presets_dest = bank_directory.getChildFile("Old Factory Presets");

    if (old_presets_dest.exists() || old_presets_dest.createDirectory().ok()) {
      juce::File factory_directory = bank_directory.getChildFile("Factory Presets");
      juce::File factory_dest = old_presets_dest.getChildFile("Factory Presets");
      if (factory_dest.exists() || factory_dest.createDirectory().ok()) {
        juce::Array<juce::File> factory_presets;
        factory_directory.findChildFiles(factory_presets, juce::File::findFiles, true,
                                         juce::String("*.") + mopo::PATCH_EXTENSION);

        for (juce::File factory_preset : factory_presets) {
          juce::File new_dest = factory_dest.getChildFile(factory_preset.getFileName());
          factory_preset.moveFileTo(new_dest);
        }

        factory_directory.deleteRecursively();
      }

      juce::File cris_directory = bank_directory.getChildFile("Cris Owl Alvarez");
      juce::File cris_dest = old_presets_dest.getChildFile("Cris Owl Alvarez");
      if (cris_dest.exists() || cris_dest.createDirectory().ok()) {
        juce::Array<juce::File> cris_presets;
        cris_directory.findChildFiles(cris_presets, juce::File::findFiles, true,
                                      juce::String("*.") + mopo::PATCH_EXTENSION);

        for (juce::File cris_preset : cris_presets) {
          juce::File new_dest = cris_dest.getChildFile(cris_preset.getFileName());
          cris_preset.moveFileTo(new_dest);
        }

        cris_directory.deleteRecursively();
      }
    }
  }
}

void Startup::copyFactoryPatches() {
  juce::File factory_bank_dir = LoadSave::getFactoryBankDirectory();
  juce::File destination = LoadSave::getBankDirectory();
  juce::Array<juce::File> factory_banks;
  factory_bank_dir.findChildFiles(factory_banks, juce::File::findDirectories, false);

  for (juce::File factory_bank : factory_banks) {
    if (isBankDirectory(factory_bank)) {
      juce::Array<juce::File> patches;
      factory_bank.findChildFiles(patches, juce::File::findFiles, true,
                                  juce::String("*.") + mopo::PATCH_EXTENSION);

      for (juce::File patch : patches) {
        juce::String relative_path = patch.getRelativePathFrom(factory_bank_dir);
        juce::File patch_location = destination.getChildFile(relative_path);

        if (!patch_location.getParentDirectory().exists())
          patch_location.getParentDirectory().createDirectory();
        patch.copyFileTo(patch_location);
      }
    }
  }
}

void Startup::fixPatchesFolder() {
  juce::File bank_directory = LoadSave::getBankDirectory();
  if (!bank_directory.exists())
    bank_directory.createDirectory();

  juce::File user_bank = LoadSave::getUserBankDirectory();
  if (!user_bank.exists())
    user_bank.createDirectory();

  // Moving old patches from v0.4.1 and earlier to new locations.
  juce::File old_user_bank = getOldUserPatchesDirectory();
  juce::File default_user_folder = user_bank.getChildFile("Default");
  juce::Array<juce::File> misplaced_files;
  old_user_bank.findChildFiles(misplaced_files, juce::File::findFiles, false,
                               juce::String("*.") + mopo::PATCH_EXTENSION);
  if (misplaced_files.size() && !default_user_folder.exists())
    default_user_folder.createDirectory();

  for (int i = 0; i < misplaced_files.size(); ++i) {
    juce::File new_patch_location = default_user_folder.getChildFile(misplaced_files[i].getFileName());
    misplaced_files[i].moveFileTo(new_patch_location);
  }
}

void Startup::updateAllPatches(SynthBase* synth,
                               std::map<std::string, juce::String>* gui_state,
                               const juce::CriticalSection& critical_section) {
  juce::File user_bank = LoadSave::getBankDirectory();
  juce::Array<juce::File> all_patches;
  user_bank.findChildFiles(all_patches, juce::File::findFiles, true,
                           juce::String("*.") + mopo::PATCH_EXTENSION);

  for (juce::File patch : all_patches) {
    juce::var parsed_json_state;
    if (juce::JSON::parse(patch.loadFileAsString(), parsed_json_state).wasOk()) {
      LoadSave::varToState(synth, *gui_state, parsed_json_state);
      juce::String author = LoadSave::getAuthor(parsed_json_state);
      if (author.isEmpty())
        author = patch.getParentDirectory().getParentDirectory().getFileName();

      std::map<std::string, juce::String> gui_state;
      gui_state["author"] = author;
      gui_state["patch_name"] = patch.getFileNameWithoutExtension();
      gui_state["folder_name"] = patch.getParentDirectory().getFileName();

      juce::var save_var = LoadSave::stateToVar(synth, gui_state, critical_section);
      patch.replaceWithText(juce::JSON::toString(save_var));
    }
  }
}
