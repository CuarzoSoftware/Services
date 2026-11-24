#ifndef CZ_SVSTATUS_H
#define CZ_SVSTATUS_H

#include <CZ/Services/Services.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Core/CZBus.h>
#include <CZ/Core/CZBusWorker.h>
#include <memory>
#include <string>

struct sd_bus_slot;

/**
 * @brief The system status for a bar's indicators: network, volume and battery.
 *
 * Each is exposed as a freedesktop symbolic icon name (empty = nothing to show, e.g. no battery):
 * - Network: NetworkManager (system bus). The Wi-Fi signal strength of the active wireless
 *   connection, otherwise wired, otherwise Wi-Fi offline/disabled. Hidden without NetworkManager
 *   or network devices.
 * - Volume: the default output's, from SVAudio (shared with the rest of the process). Muted, low,
 *   medium or high.
 * - Battery: UPower's display device (system bus), its level in steps of 10 %, and charging /
 *   charged / plugged in; and its warning level (batteryWarning), for low battery notifications.
 *
 * Everything is event driven (D-Bus signals, SVAudio's changes), onChanged is emitted when the state
 * changes. The queries run off the UI thread (see CZBusWorker).
 *
 * Shared by the process: see GetOrMake().
 */
class CZ::SVStatus final : public CZ::CZObject
{
public:
    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SVStatus> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SVStatus> Get() noexcept;

    ~SVStatus() noexcept;

    /// Network: network-wireless-signal-{excellent,good,ok,weak,none}-symbolic, network-wired-symbolic,
    /// network-wireless-offline-symbolic or network-wireless-disabled-symbolic (empty: no network devices).
    std::string networkIcon;

    /// Volume: audio-volume-{muted,low,medium,high}-symbolic (empty: no audio output).
    std::string volumeIcon;

    /// Battery: battery-level-{0..100}[-charging|-charged|-plugged-in]-symbolic (empty: no battery).
    std::string batteryIcon;

    /// Output volume (0 to 1, may exceed 1 if boosted), -1 without an audio output.
    float volume { -1.f };

    /// The output is muted.
    bool muted { false };

    /// Battery level in percent, -1 without a battery.
    CZ::Int32 batteryPercent { -1 };

    /// UPower's warning level of the battery (its WarningLevel).
    enum class BatteryWarning : CZ::UInt8
    {
        None,       ///< Fine, charging or no battery
        Low,        ///< At or under UPower's PercentageLow (20 % by default)
        Critical,   ///< At or under PercentageCritical (5 % by default)
        Action      ///< At or under PercentageAction (2 % by default): UPower is about to hibernate or power off
    };

    /// Set while discharging (see BatteryWarning).
    BatteryWarning batteryWarning { BatteryWarning::None };

    /// On AC power (charging, fully charged or not charging), false without a battery.
    bool batteryOnPower { false };

    /// Sets the output volume (0 to 1, muted at 0, unmuted above). Coalesced: fine to call on every drag step.
    void setVolume(float volume) noexcept;

    /// The network's state in words (e.g. "Wi-Fi 72 %"), for logs.
    std::string networkInfo;

    /// The volume's state in words (e.g. "Volume 40 %"), for logs.
    std::string volumeInfo;

    /// The battery's state in words (e.g. "Battery 55 %-charging"), for logs.
    std::string batteryInfo;

    /// Something above changed.
    CZ::CZSignal<> onChanged;

private:
    SVStatus() noexcept;
    void refreshNetwork() noexcept;
    void refreshBattery() noexcept;
    void applyBattery(double percentage, CZ::UInt32 state, CZ::UInt32 warningLevel) noexcept;
    void applyVolumeState() noexcept;
    void set(std::string &icon, std::string value) noexcept;

    std::shared_ptr<SVAudio> m_audio;
    std::shared_ptr<CZ::CZBus> m_system;
    sd_bus_slot *m_networkMatch {}, *m_batteryMatch {};

    // Debounce bursts of events (e.g. NetworkManager scans)
    std::unique_ptr<CZ::CZTimer> m_networkTimer;
    bool m_networkBusy { false }, m_networkAgain { false };

    // Last: destroyed first, so no completion runs on a half destroyed object.
    CZ::CZBusWorker m_worker { CZ::CZBusWorker::System };
};

#endif // CZ_SVSTATUS_H
