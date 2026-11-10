#pragma once

#include <cstdint>

#include "settings/SettingsModel.h"

namespace reading {

    struct BookIdentity {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;

        bool operator==(const BookIdentity&) const = default;
    };

    // "Onde parei": last real reading position before a jump (see reader/ReadingMarks.h).
    // Absent in old sidecars: defaults to invalid, so old files keep working.
    struct ResumePoint {
        uint32_t wordIndex = 0;
        uint32_t chapter = 0; // chapter index + 1, 0 = none
        uint32_t percent = 0;
        bool valid = false;
        bool pending = false; // jumped away and not yet settled at the new place

        bool operator==(const ResumePoint&) const = default;
    };

    struct State {
        uint32_t wordIndex = 0;
        ResumePoint resume;
        settings::ReadingOverrides overrides;

        bool operator==(const State&) const = default;
    };

    struct StoredState {
        BookIdentity identity;
        State reading;

        bool operator==(const StoredState&) const = default;
    };

} // namespace reading
