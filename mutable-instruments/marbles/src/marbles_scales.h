// The six preset scales of Marbles (marbles/settings.cc, preset_scales; Copyright 2015 Emilie Gillet, MIT), copied
// unchanged for the MPC port (steve/schwung-ports/marbles, 2026-10-01): MAJOR, MINOR, PENTATONIC, PELOG, BHAIRAV, SHRI.
#pragma once
#include "marbles/random/quantizer.h"

namespace marbles {

static const Scale preset_scales[6] = {
  // C major
  {
    1.0f,
    12,
    {
      { 0.0000f, 255 },  // C
      { 0.0833f, 16 },   // C#
      { 0.1667f, 96 },   // D
      { 0.2500f, 24 },   // D#
      { 0.3333f, 128 },  // E
      { 0.4167f, 64 },   // F
      { 0.5000f, 8 },    // F#
      { 0.5833f, 192 },  // G
      { 0.6667f, 16 },   // G#
      { 0.7500f, 96 },   // A
      { 0.8333f, 24 },   // A#
      { 0.9167f, 128 },  // B
    }
  },
  
  // C minor
  {
    1.0f,
    12,
    {
      { 0.0000f, 255 },  // C
      { 0.0833f, 16 },   // C#
      { 0.1667f, 96 },   // D
      { 0.2500f, 128 },  // Eb
      { 0.3333f, 8 },    // E
      { 0.4167f, 64 },   // F
      { 0.5000f, 4 },    // F#
      { 0.5833f, 192 },  // G
      { 0.6667f, 96 },   // G#
      { 0.7500f, 16 },   // A
      { 0.8333f, 128 },  // Bb
      { 0.9167f, 16 },   // B
    }
  },
  
  // Pentatonic
  {
    1.0f,
    12,
    {
      { 0.0000f, 255 },  // C
      { 0.0833f, 4 },    // C#
      { 0.1667f, 96 },   // D
      { 0.2500f, 4 },    // Eb
      { 0.3333f, 4 },    // E
      { 0.4167f, 140 },  // F
      { 0.5000f, 4 },    // F#
      { 0.5833f, 192 },  // G
      { 0.6667f, 4 },    // G#
      { 0.7500f, 96 },   // A
      { 0.8333f, 4 },    // Bb
      { 0.9167f, 4 },    // B
    }
  },
  
  // Pelog
  {
    1.0f,
    7,
    {
      { 0.0000f, 255 },  // C
      { 0.1275f, 128 },  // Db+
      { 0.2625f, 32 },  // Eb-
      { 0.4600f, 8 },    // F#-
      { 0.5883f, 192 },  // G
      { 0.7067f, 64 },  // Ab
      { 0.8817f, 16 },    // Bb+
    }
  },
  
  // Raag Bhairav That
  {
    1.0f,
    12,
    {
      { 0.0000f, 255 }, // ** Sa
      { 0.0752f, 128 }, // ** Komal Re
      { 0.1699f, 4 },   //    Re
      { 0.2630f, 4 },   //    Komal Ga
      { 0.3219f, 128 }, // ** Ga
      { 0.4150f, 64 },  // ** Ma
      { 0.4918f, 4 },   //    Tivre Ma
      { 0.5850f, 192 }, // ** Pa
      { 0.6601f, 64 },  // ** Komal Dha
      { 0.7549f, 4 },   //    Dha
      { 0.8479f, 4 },   //    Komal Ni
      { 0.9069f, 64 },  // ** Ni
    }
  },
  
  // Raag Shri
  {
    1.0f,
    12,
    {
      { 0.0000f, 255 }, // ** Sa
      { 0.0752f, 4 },   //    Komal Re
      { 0.1699f, 128 }, // ** Re
      { 0.2630f, 64 },  // ** Komal Ga
      { 0.3219f, 4 },   //    Ga
      { 0.4150f, 128 }, // ** Ma
      { 0.4918f, 4 },   //    Tivre Ma
      { 0.5850f, 192 }, // ** Pa
      { 0.6601f, 4 },   //    Komal Dha
      { 0.7549f, 64 },  // ** Dha
      { 0.8479f, 128 }, // ** Komal Ni
      { 0.9069f, 4 },   //    Ni
    }
  },
};

}  // namespace marbles
