#pragma once
/**
 * @file board_devkit.hpp
 * @brief Pin profile of a bare devkit on the bench: same plan as horloge-v1,
 *        nothing has to be connected. Driving an unconnected data pin is harmless.
 */

#include <cstdint>

namespace clock_board {

inline constexpr char kName[] = "devkit";
inline constexpr uint8_t kLedDataGpio = 13U;
inline constexpr uint8_t kLdrAdcGpio = 33U;
inline constexpr uint8_t kI2cSdaGpio = 27U;
inline constexpr uint8_t kI2cSclGpio = 26U;
inline constexpr uint8_t kButtonGpio = 25U;

}  // namespace clock_board
