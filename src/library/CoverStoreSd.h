#pragma once

#include "library/CoverStore.h"

namespace covers {
// File access for CoverStore on the device's microSD (Board::Storage::filesystem()).
[[nodiscard]] Io sdIo();
} // namespace covers
