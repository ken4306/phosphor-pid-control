/**
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pidloop.hpp"

#include "pid/pidcontroller.hpp"
#include "pid/tuning.hpp"
#include "pid/zone_interface.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>

namespace pid_control
{

static void processThermals(const std::shared_ptr<ZoneInterface>& zone)
{
    // Get the latest margins.
    zone->updateSensors();
    // Zero out the set point goals.
    zone->clearSetPoints();
    zone->clearRPMCeilings();
    // Run the margin PIDs.
    zone->processThermals();
    // Get the maximum RPM setpoint.
    zone->determineMaxSetPointRequest();
}

void pidControlLoop(const std::shared_ptr<ZoneInterface>& zone,
                    const std::shared_ptr<boost::asio::steady_timer>& timer,
                    const bool* isCanceling, bool first, uint64_t cycleCnt)
{
    if (*isCanceling)
    {
        return;
    }

    std::chrono::steady_clock::time_point nextTime;

    if (first)
    {
        if (loggingEnabled)
        {
            zone->initializeLog();
        }

        zone->initializeCache();
        processThermals(zone);

        nextTime = std::chrono::steady_clock::now();
    }
    else
    {
        nextTime = timer->expiry();
    }

    uint64_t msPerFanCycle = zone->getCycleIntervalTime();

    // Push forward the original expiration time of timer, instead of just
    // resetting it with expires_after() from now, to make sure the interval
    // is of the expected duration, and not stretched out by CPU time taken.
    nextTime += std::chrono::milliseconds(msPerFanCycle);
    timer->expires_at(nextTime);
    timer->async_wait([zone, timer, cycleCnt, isCanceling, msPerFanCycle](
                          const boost::system::error_code& ec) mutable {
        if (ec == boost::asio::error::operation_aborted)
        {
            return; // timer being canceled, stop loop
        }

        /*
         * This should sleep on the conditional wait for the listen thread
         * to tell us it's in sync.  But then we also need a timeout option
         * in case phosphor-hwmon is down, we can go into some weird failure
         * more.
         *
         * Another approach would be to start all sensors in worst-case
         * values, and fail-safe mode and then clear out of fail-safe mode
         * once we start getting values.  Which I think it is a solid
         * approach.
         *
         * For now this runs before it necessarily has any sensor values.
         * For the host sensors they start out in fail-safe mode.  For the
         * fans, they start out as 0 as input and then are adjusted once
         * they have values.
         *
         * If a fan has failed, it's value will be whatever we're told or
         * however we retrieve it.  This program disregards fan values of 0,
         * so any code providing a fan speed can set to 0 on failure and
         * that fan value will be effectively ignored.  The PID algorithm
         * will be unhappy but nothing bad will happen.
         *
         * TODO(venture): If the fan value is 0 should that loop just be
         * skipped? Right now, a 0 value is ignored in
         * FanController::inputProc()
         */

        // Check if we should just go back to sleep.
        if (zone->getManualMode())
        {
            pidControlLoop(zone, timer, isCanceling, false, cycleCnt);
            return;
        }

        // Get the latest fan speeds.
        zone->updateFanTelemetry();

        uint64_t msPerThermalCycle = zone->getUpdateThermalsCycle();

        // Process thermal cycles at a rate that is less often than fan
        // cycles. If thermal time is not an exact multiple of fan time,
        // there will be some remainder left over, to keep the timing
        // correct, as the intervals are staggered into one another.
        if (cycleCnt >= msPerThermalCycle)
        {
            cycleCnt -= msPerThermalCycle;

            processThermals(zone);
        }

        // Run the fan PIDs every iteration.
        zone->processFans();

        if (loggingEnabled)
        {
            std::ostringstream out;
            out << "," << zone->getFailSafeMode() << std::endl;
            zone->writeLog(out.str());
        }

        // Count how many milliseconds have elapsed, so we can know when
        // to perform thermal cycles, in proper ratio with fan cycles.
        cycleCnt += msPerFanCycle;

        pidControlLoop(zone, timer, isCanceling, false, cycleCnt);
    });
}

} // namespace pid_control
