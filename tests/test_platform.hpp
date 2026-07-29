// tests/test_platform.hpp
//
// TestPlatform — a NullPlatform subclass with deterministic, controllable time.
// Inherits all input injection helpers from NullPlatform (inject_key_down, etc.)
// so tests can drive the game headlessly without a GPU or real window.

#pragma once

#include "engine/platform/platform.hpp"

#include <cstdint>

namespace resf2::test {

class TestPlatform : public platform::NullPlatform {
public:
    TestPlatform() = default;
    ~TestPlatform() override = default;

    // Set the clock to an absolute value.
    void set_fixed_time_ms(std::uint64_t ms) noexcept { fixed_time_ms_ = ms; }

    // Advance the clock by a delta (used by run_frames to simulate frame pacing).
    void advance_time_ms(std::uint64_t ms) noexcept { fixed_time_ms_ += ms; }

    // Override the wall-clock time with our deterministic value.
    [[nodiscard]] std::uint64_t now_ms() const noexcept override {
        return fixed_time_ms_;
    }

private:
    std::uint64_t fixed_time_ms_ = 0;
};

}  // namespace resf2::test
