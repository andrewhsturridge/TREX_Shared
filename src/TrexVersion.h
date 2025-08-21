#pragma once
#include <stdint.h>

// ---- Bump these for each release ----
#define TREX_FW_MAJOR 0
#define TREX_FW_MINOR 5

// Optional: printable "0.4"
#define _STR(x) #x
#define STR(x) _STR(x)
#define TREX_FW_VERSION_STR  STR(TREX_FW_MAJOR) "." STR(TREX_FW_MINOR)

// Numeric access if you prefer constexpr:
constexpr uint8_t TREX_FW_MAJOR_NUM = TREX_FW_MAJOR;
constexpr uint8_t TREX_FW_MINOR_NUM = TREX_FW_MINOR;
