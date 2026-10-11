#pragma once

#include <chrono>

namespace settings_store {

// Debounce short edits, but checkpoint continuous dragging within two seconds.
class SaveSchedule {
public:
    using Clock = std::chrono::steady_clock;
    void MarkDirty(Clock::time_point now) {
        if (!dirty_) firstChange_ = now;
        dirty_ = true;
        lastChange_ = now;
    }
    bool Due(Clock::time_point now) const {
        return dirty_ && now >= retryAfter_ &&
            (now - lastChange_ >= std::chrono::seconds(1) ||
             now - firstChange_ >= std::chrono::seconds(2));
    }
    void Complete(bool success, Clock::time_point now) {
        if (success) *this = {};
        else retryAfter_ = now + std::chrono::seconds(2);
    }
    bool Pending() const { return dirty_; }
private:
    bool dirty_ = false;
    Clock::time_point firstChange_{}, lastChange_{}, retryAfter_{};
};

} // namespace settings_store
