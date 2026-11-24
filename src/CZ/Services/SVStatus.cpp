#include <CZ/Services/SVStatus.h>
#include <CZ/Services/SVAudio.h>
#include <CZ/Services/SVLog.h>
#include <systemd/sd-bus.h>
#include <unistd.h>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace CZ;

static constexpr const char *NM { "org.freedesktop.NetworkManager" };
static constexpr const char *NMPath { "/org/freedesktop/NetworkManager" };
static constexpr const char *UPower { "org.freedesktop.UPower" };
static constexpr const char *UPowerDisplay { "/org/freedesktop/UPower/devices/DisplayDevice" };

/* ---------------------------------- D-Bus property helpers ---------------------------------- */

// Reads a property of a basic type (sd-bus type char @p type) into @p out.
template <class T>
static bool GetProperty(sd_bus *bus, const char *dest, const char *path, const char *iface, const char *prop, char type, T *out) noexcept
{
    return sd_bus_get_property_trivial(bus, dest, path, iface, prop, nullptr, type, out) >= 0;
}

static std::string GetString(sd_bus *bus, const char *dest, const char *path, const char *iface, const char *prop, const char *type = "s") noexcept
{
    sd_bus_message *reply {};
    std::string result;

    if (sd_bus_get_property(bus, dest, path, iface, prop, nullptr, &reply, type) >= 0)
    {
        const char *s {};

        if (sd_bus_message_read(reply, type, &s) >= 0 && s)
            result = s;
    }

    sd_bus_message_unref(reply);
    return result;
}

static std::vector<std::string> GetPaths(sd_bus *bus, const char *dest, const char *path, const char *iface, const char *prop) noexcept
{
    sd_bus_message *reply {};
    std::vector<std::string> result;

    if (sd_bus_get_property(bus, dest, path, iface, prop, nullptr, &reply, "ao") >= 0 &&
        sd_bus_message_enter_container(reply, 'a', "o") >= 0)
    {
        const char *p {};

        while (sd_bus_message_read(reply, "o", &p) > 0)
            result.emplace_back(p);
    }

    sd_bus_message_unref(reply);
    return result;
}

/* ------------------------------------------ SVStatus ----------------------------------------- */

static std::weak_ptr<SVStatus> s_status;

std::shared_ptr<SVStatus> SVStatus::GetOrMake() noexcept
{
    auto status { s_status.lock() };

    if (!status)
        s_status = status = std::shared_ptr<SVStatus>(new SVStatus());

    return status;
}

std::shared_ptr<SVStatus> SVStatus::Get() noexcept
{
    return s_status.lock();
}

SVStatus::SVStatus() noexcept
{
    m_networkTimer = std::make_unique<CZTimer>([this](CZTimer*){ refreshNetwork(); });

    m_system = CZBus::GetOrMakeSystem();

    if (m_system)
    {
        // Any NetworkManager property change (connections, devices, access points' strength...).
        sd_bus_match_signal_async(m_system->bus(), &m_networkMatch, NM, nullptr,
            "org.freedesktop.DBus.Properties", "PropertiesChanged",
            [](sd_bus_message *, void *data, sd_bus_error *) -> int {
                auto *s { static_cast<SVStatus*>(data) };

                if (!s->m_networkTimer->running())
                    s->m_networkTimer->start(500);
                return 0;
            }, nullptr, this);

        sd_bus_match_signal_async(m_system->bus(), &m_batteryMatch, UPower, UPowerDisplay,
            "org.freedesktop.DBus.Properties", "PropertiesChanged",
            [](sd_bus_message *, void *data, sd_bus_error *) -> int {
                static_cast<SVStatus*>(data)->refreshBattery();
                return 0;
            }, nullptr, this);

        refreshNetwork();
        refreshBattery();
    }
    else
        SVLog(CZWarning, CZLN, "No system bus: network and battery indicators disabled");

    m_audio = SVAudio::GetOrMake();
    m_audio->onChanged.subscribe(this, [this]{ applyVolumeState(); });
    applyVolumeState();
}

SVStatus::~SVStatus() noexcept
{
    sd_bus_slot_unref(m_networkMatch);
    sd_bus_slot_unref(m_batteryMatch);
}

void SVStatus::set(std::string &icon, std::string value) noexcept
{
    if (icon == value)
        return;

    icon = std::move(value);
    onChanged.notify();
}

void SVStatus::refreshNetwork() noexcept
{
    if (!m_system)
        return;

    // Coalesced: at most one query running, and one more queued.
    if (m_networkBusy)
    {
        m_networkAgain = true;
        return;
    }

    m_networkBusy = true;

    // Many calls: read off the UI thread.
    m_worker.run([this](sd_bus *bus) -> std::function<void()> {
        std::string icon, info;

        if (bus)
        {
            int wifiStrength { -1 };
            bool wired { false };

            // Active connections: a wireless one gives the signal of its access point.
            for (const auto &active : GetPaths(bus, NM, NMPath, NM, "ActiveConnections"))
            {
                const char *iface { "org.freedesktop.NetworkManager.Connection.Active" };
                UInt32 state { 0 };

                if (!GetProperty(bus, NM, active.c_str(), iface, "State", 'u', &state) || state != 2 /* Activated */)
                    continue;

                const std::string type { GetString(bus, NM, active.c_str(), iface, "Type") };

                if (type == "802-3-ethernet")
                    wired = true;
                else if (type == "802-11-wireless")
                {
                    for (const auto &device : GetPaths(bus, NM, active.c_str(), iface, "Devices"))
                    {
                        const std::string ap { GetString(bus, NM, device.c_str(), "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint", "o") };
                        UInt8 strength { 0 };

                        if (ap.size() > 1 && GetProperty(bus, NM, ap.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Strength", 'y', &strength))
                            wifiStrength = std::max(wifiStrength, Int32(strength));
                    }
                }
            }

            // No active Wi-Fi: is there a Wi-Fi device at all?
            bool hasWifi { wifiStrength >= 0 };

            if (!hasWifi)
                for (const auto &device : GetPaths(bus, NM, NMPath, NM, "Devices"))
                {
                    UInt32 type { 0 };

                    if (GetProperty(bus, NM, device.c_str(), "org.freedesktop.NetworkManager.Device", "DeviceType", 'u', &type) && type == 2 /* Wi-Fi */)
                    {
                        hasWifi = true;
                        break;
                    }
                }

            if (wifiStrength >= 0)
            {
                const char *level { wifiStrength >= 80 ? "excellent" : wifiStrength >= 55 ? "good" :
                                    wifiStrength >= 30 ? "ok" : wifiStrength > 5 ? "weak" : "none" };
                icon = std::string("network-wireless-signal-") + level + "-symbolic";
                info = "Wi-Fi " + std::to_string(wifiStrength) + " %";
            }
            else if (wired)
            {
                icon = "network-wired-symbolic";
                info = "Wired";
            }
            else if (hasWifi)
            {
                int enabled { 1 };
                GetProperty(bus, NM, NMPath, NM, "WirelessEnabled", 'b', &enabled);
                icon = enabled ? "network-wireless-offline-symbolic" : "network-wireless-disabled-symbolic";
                info = enabled ? "Wi-Fi disconnected" : "Wi-Fi off";
            }
            else
                info = "No network devices";
        }

        return [this, icon, info]{
            m_networkBusy = false;
            networkInfo = info;
            set(networkIcon, icon);

            if (m_networkAgain)
            {
                m_networkAgain = false;
                refreshNetwork();
            }
        };
    });
}

void SVStatus::refreshBattery() noexcept
{
    if (!m_system)
        return;

    m_worker.run([this](sd_bus *bus) -> std::function<void()> {
        const char *iface { "org.freedesktop.UPower.Device" };
        int present { 0 };
        double percentage { 0.0 };
        UInt32 state { 0 };

        if (!bus ||
            !GetProperty(bus, UPower, UPowerDisplay, iface, "IsPresent", 'b', &present) || !present ||
            !GetProperty(bus, UPower, UPowerDisplay, iface, "Percentage", 'd', &percentage))
        {
            return [this]{
                batteryInfo = "No battery";

                if (batteryPercent != -1 || batteryWarning != BatteryWarning::None || batteryOnPower)
                {
                    batteryPercent = -1;
                    batteryWarning = BatteryWarning::None;
                    batteryOnPower = false;
                    onChanged.notify();
                }

                set(batteryIcon, "");
            };
        }

        UInt32 warningLevel { 0 };
        GetProperty(bus, UPower, UPowerDisplay, iface, "State", 'u', &state);
        GetProperty(bus, UPower, UPowerDisplay, iface, "WarningLevel", 'u', &warningLevel);

        return [this, percentage, state, warningLevel]{ applyBattery(percentage, state, warningLevel); };
    });
}

void SVStatus::applyBattery(double percentage, UInt32 state, UInt32 warningLevel) noexcept
{
    // WarningLevel: 3 low, 4 critical, 5 action (1 none, 2 a UPS discharging)
    const bool onPower { state == 1 || state == 4 || state == 5 };
    const BatteryWarning warning { onPower ? BatteryWarning::None :
        warningLevel == 3 ? BatteryWarning::Low : warningLevel == 4 ? BatteryWarning::Critical :
        warningLevel == 5 ? BatteryWarning::Action : BatteryWarning::None };

    // battery-level-{0..100}[-charging|-charged|-plugged-in]-symbolic
    const Int32 pct { Int32(std::lround(percentage)) };
    const Int32 level { std::clamp(Int32(std::lround(percentage / 10.0)) * 10, 0, 100) };
    std::string suffix;

    if (state == 1)                         suffix = "-charging";      // Charging
    else if (state == 4 && level == 100)    suffix = "-charged";       // Fully charged
    else if (state == 4 || state == 5)      suffix = "-plugged-in";    // On AC, not charging

    batteryInfo = "Battery " + std::to_string(pct) + " %" + suffix;
    std::string icon { "battery-level-" + std::to_string(level) + suffix + "-symbolic" };

    // Everything updated before telling (once)
    if (pct == batteryPercent && warning == batteryWarning && onPower == batteryOnPower && icon == batteryIcon)
        return;

    batteryPercent = pct;
    batteryWarning = warning;
    batteryOnPower = onPower;
    batteryIcon = std::move(icon);
    onChanged.notify();
}

/* ------------------------------------------ Volume ------------------------------------------ */

void SVStatus::applyVolumeState() noexcept
{
    const SVAudioState &audio { m_audio->state() };
    const SVAudioSelection &output { audio.output };

    // Not read yet: nothing to say
    if (!audio.loaded)
        return;

    const bool ok { audio.available && output.valid && output.volume >= 0.f };
    const float volume { ok ? output.volume : -1.f };
    const bool muted { ok && output.muted };

    if (std::abs(volume - this->volume) > 0.001f || muted != this->muted)
    {
        this->volume = volume;
        this->muted = muted;
        onChanged.notify();
    }

    if (!ok)
    {
        volumeInfo = "No audio output";
        set(volumeIcon, "");
        return;
    }

    const char *level { muted || volume <= 0.f ? "muted" : volume < 0.34f ? "low" : volume < 0.67f ? "medium" : "high" };
    volumeInfo = "Volume " + std::to_string(Int32(std::lround(volume * 100.f))) + " %" + (muted ? " (muted)" : "");
    set(volumeIcon, std::string("audio-volume-") + level + "-symbolic");
}

void SVStatus::setVolume(float value) noexcept
{
    m_audio->setVolume(false, std::clamp(value, 0.f, 1.f));
    applyVolumeState(); // Shown right away (SVAudio confirms it)
}
