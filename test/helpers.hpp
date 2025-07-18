// THIS EXISTS AS A COPY OF SDBUSPLUS/TEST/HELPERS.HPP until that is merged.
#pragma once

#include <systemd/sd-bus.h>

#include <sdbusplus/test/sdbus_mock.hpp>

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace pid_control
{

using ::testing::_;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;

/** @brief Setup the expectations for sdbus-based object creation.
 *
 * Objects created that inherit a composition from sdbusplus will all
 * require at least these expectations.
 *
 * If you have future sd_bus_emit_properties_changed_strv calls expected,
 * you'll need to add those calls into your test.  This only captures the
 * property updates you tell it to expect initially.
 *
 * TODO: Make it support more cases, as I'm sure there are more.
 *
 * @param[in] sdbus_mock - Pointer to your sdbus mock interface used with
 *     the sdbusplus::bus_t you created.
 * @param[in] defer - Whether object announcement is deferred.
 * @param[in] path - the dbus path passed to the object
 * @param[in] intf - the dbus interface
 * @param[in] properties - an ordered list of expected property updates.
 * @param[in] index - a pointer to a valid double in a surviving scope.
 */
void SetupDbusObject(sdbusplus::SdBusMock* sdbus_mock, bool defer,
                     const std::string& path, const std::string& intf,
                     const std::vector<std::string>& properties, double* index)
{
    if (!defer)
    {
        EXPECT_CALL(*sdbus_mock,
                    sd_bus_emit_object_added(IsNull(), StrEq(path)))
            .WillOnce(Return(0));
    }

    if (!properties.empty())
    {
        (*index) = 0;
        EXPECT_CALL(*sdbus_mock,
                    sd_bus_emit_properties_changed_strv(IsNull(), StrEq(path),
                                                        StrEq(intf), NotNull()))
            .Times(properties.size())
            .WillRepeatedly(Invoke([=]([[maybe_unused]] sd_bus* bus,
                                       [[maybe_unused]] const char* path,
                                       [[maybe_unused]] const char* interface,
                                       const char** names) {
                EXPECT_STREQ(properties[(*index)++].c_str(), names[0]);
                return 0;
            }));
    }

    return;
}

} // namespace pid_control
