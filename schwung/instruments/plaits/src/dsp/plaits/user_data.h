// Stub for UserData — the hardware Plaits reads user wavetables from Flash.
// In the plugin context there is no Flash; always return nullptr so engines
// fall back to their built-in default data.

#ifndef PLAITS_USER_DATA_H_
#define PLAITS_USER_DATA_H_

#include <stdint.h>

namespace plaits {

class UserData {
 public:
  UserData() {}
  // Returns a pointer to user data for the given engine index, or nullptr if
  // no user data is available (which is always the case in the plugin build).
  const uint8_t* ptr(int /*engine_index*/) const { return nullptr; }
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_H_
