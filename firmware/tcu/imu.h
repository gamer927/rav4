// MPU-6050 gyro (GY-521 module) on the shared I2C bus, used only for yaw
// rate. Lateral g is then yaw rate x road speed, which doesn't care how the
// module is tilted, only that its Z axis is roughly vertical.
#pragma once
#include <stdint.h>

class Imu {
 public:
  bool begin(uint8_t addr = 0x68);
  bool present() const { return present_; }
  bool readYawDps(float& dps);  // false on a bus error

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  uint8_t addr_ = 0x68;
  bool present_ = false;
};
