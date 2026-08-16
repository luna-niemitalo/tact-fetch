#pragma once

#include <cstddef>

// Concrete defaults from DESIGN.md #5 -- not yet enforced anywhere (the
// fetch step is still a scaffold, see fetch.h), but the dry-run's
// wall-clock estimate is real and needs real numbers to estimate against.
namespace tactfetch::politeness {

inline constexpr int kDefaultConcurrency = 2;
inline constexpr int kMaxConcurrency = 4;  // half of .build.info's maxhosts=8; refuse above this, never silently clamp

inline constexpr double kMaxNewRequestsPerSecond = 5.0;

inline constexpr int kPerRequestTimeoutSeconds = 30;

inline constexpr int kRetryBaseDelaySeconds = 1;
inline constexpr int kRetryMaxDelaySeconds = 60;
inline constexpr int kRetryMaxAttempts = 5;

inline constexpr int kDryRunMarkerFreshnessMinutes = 10;

}  // namespace tactfetch::politeness
