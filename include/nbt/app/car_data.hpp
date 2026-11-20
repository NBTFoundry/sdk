// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_CAR_DATA_HPP
#define NBT_APP_CAR_DATA_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace app {

/* Capability `car-data` (bridge 1.6): the iDrive car data the application
 * named in its signed package (`bridge-cds`), as the car gives it: iDrive's
 * own text, usually JSON such as {"gear":2}. NBT Manager subscribes to the
 * names while the application runs; a name has no value until the car gives
 * one. Reading is cheap: poll it from the main loop. */
class CarData {
 public:
  CarData();
  /* From NBT_CDS_DIR; false when the capability was not granted. */
  bool init();
  /* The latest value of `name`, NUL terminated in `value` (`size` bytes, 201
   * hold any); `sequence` (may be NULL) grows with each new value. False when
   * there is none yet. */
  bool read(const char* name, char* value, size_t size, uint64_t* sequence) const;

 private:
  char dir_[256];
};

}  // namespace app
}  // namespace nbt

#endif
