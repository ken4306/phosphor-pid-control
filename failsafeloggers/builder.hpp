#pragma once

#include "pid/zone_interface.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace pid_control
{

void buildFailsafeLoggers(
    const std::unordered_map<int64_t, std::shared_ptr<ZoneInterface>>& zones,
    const size_t logMaxCountPerSecond = 20);

} // namespace pid_control
