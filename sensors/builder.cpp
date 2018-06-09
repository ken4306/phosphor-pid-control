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

#include <iostream>
#include <map>
#include <string>

/* Configuration. */
#include "conf.hpp"

#include "dbus/dbuspassive.hpp"
#include "interfaces.hpp"
#include "notimpl/readonly.hpp"
#include "notimpl/writeonly.hpp"
#include "sensors/builder.hpp"
#include "sensors/manager.hpp"
#include "sensors/host.hpp"
#include "sensors/pluggable.hpp"
#include "sysfs/sysfsread.hpp"
#include "sysfs/sysfswrite.hpp"
#include "util.hpp"

static constexpr bool deferSignals = true;

SensorManager BuildSensors(
    const std::map<std::string, struct sensor>& config)
{
    SensorManager mgmr;
    auto& HostSensorBus = mgmr.getHostBus();
    auto& PassiveListeningBus = mgmr.getPassiveBus();

    for (auto& it : config)
    {
        std::unique_ptr<ReadInterface> ri;
        std::unique_ptr<WriteInterface> wi;

        std::string name = it.first;
        const struct sensor* info = &it.second;

        std::cerr << "Sensor: " << name << " " << info->type << " ";
        std::cerr << info->readpath << " " << info->writepath << "\n";

        IOInterfaceType rtype = GetReadInterfaceType(info->readpath);
        IOInterfaceType wtype = GetWriteInterfaceType(info->writepath);

        // fan sensors can be ready any way and written others.
        // fan sensors are the only sensors this is designed to write.
        // Nothing here should be write-only, although, in theory a fan could be.
        // I'm just not sure how that would fit together.
        // TODO(venture): It should check with the ObjectMapper to check if
        // that sensor exists on the Dbus.
        switch (rtype)
        {
            case IOInterfaceType::DBUSPASSIVE:
                ri = std::make_unique<DbusPassive>(
                         PassiveListeningBus,
                         info->type,
                         name);
                break;
            case IOInterfaceType::EXTERNAL:
                // These are a special case for read-only.
                break;
            case IOInterfaceType::SYSFS:
                ri = std::make_unique<SysFsRead>(info->readpath);
                break;
            default:
                ri = std::make_unique<WriteOnly>();
                break;
        }

        if (info->type == "fan")
        {
            switch (wtype)
            {
                case IOInterfaceType::SYSFS:
                    if (info->max > 0)
                    {
                        wi = std::make_unique<SysFsWritePercent>(
                                 info->writepath,
                                 info->min,
                                 info->max);
                    }
                    else
                    {
                        wi = std::make_unique<SysFsWrite>(
                                 info->writepath,
                                 info->min,
                                 info->max);
                    }

                    break;
                default:
                    wi = std::make_unique<ReadOnlyNoExcept>();
                    break;
            }

            auto sensor = std::make_unique<PluggableSensor>(
                              name,
                              info->timeout,
                              std::move(ri),
                              std::move(wi));
            mgmr.addSensor(info->type, name, std::move(sensor));
        }
        else if (info->type == "temp" || info->type == "margin")
        {
            // These sensors are read-only, but only for this application
            // which only writes to fan sensors.
            std::cerr << info->type << " readpath: " << info->readpath << "\n";

            if (IOInterfaceType::EXTERNAL == rtype)
            {
                std::cerr << "Creating HostSensor: " << name
                          << " path: " << info->readpath << "\n";

                /*
                 * The reason we handle this as a HostSensor is because it's
                 * not quite pluggable; but maybe it could be.
                 */
                auto sensor = HostSensor::CreateTemp(
                                  name,
                                  info->timeout,
                                  HostSensorBus,
                                  info->readpath.c_str(),
                                  deferSignals);
                mgmr.addSensor(info->type, name, std::move(sensor));
            }
            else
            {
                wi = std::make_unique<ReadOnlyNoExcept>();
                auto sensor = std::make_unique<PluggableSensor>(
                                  name,
                                  info->timeout,
                                  std::move(ri),
                                  std::move(wi));
                mgmr.addSensor(info->type, name, std::move(sensor));
            }
        }
    }

    return mgmr;
}
