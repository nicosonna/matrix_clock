#pragma once
/**
 * @file board.hpp
 * @brief Selects the pin profile chosen in Kconfig (CLOCK_BOARD_*).
 *
 * Every GPIO number of the firmware lives in one of the board_*.hpp profiles,
 * as `constexpr`. No GPIO number anywhere else (CLAUDE.md).
 */

#include "sdkconfig.h"

#if defined(CONFIG_CLOCK_BOARD_HORLOGE_V1)
#include "clock_board/board_horloge_v1.hpp"
#elif defined(CONFIG_CLOCK_BOARD_DEVKIT)
#include "clock_board/board_devkit.hpp"
#else
#error "No board profile selected: set CONFIG_CLOCK_BOARD_* (menuconfig > matrix_clock - target board)"
#endif
