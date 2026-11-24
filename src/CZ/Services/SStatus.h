#ifndef CZ_SSTATUS_H
#define CZ_SSTATUS_H

#include <CZ/Services/Services.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Core/CZBus.h>
#include <CZ/Core/CZBusWorker.h>
#include <CZ/Core/CZEventSource.h>
#include <memory>
#include <string>
#include <sys/types.h>

struct sd_bus_slot;

/**
 * @brief The system status for a bar's indicators: network, volume and battery.
 *
 * Each is exposed as a freedesktop symbolic icon name (empty = nothing to show, e.g. no battery):
 * - Network: NetworkManager (system bus). The Wi-Fi signal strength of the active wireless
 *   connection, otherwise wired, otherwise Wi-Fi offline/disabled. Hidden without NetworkManager
 *   or network devices.
 * - Volume: the default sink of PipeWire/PulseAudio (`wpctl`), refreshed on `pactl subscribe`
 *   events. Muted, low, medium or high.
 * - Battery: UPower's display device (system bus), its level in steps of 10 %, and charging /
 *   charged / plugged in.
 *
 * Everything is event driven (D-Bus signals, the pactl event stream), onChanged is emitted when
 * an icon changes. The queries and wpctl calls run off the UI thread (see CZBusWorker).
 */
class CZ::SStatus final : public CZ::CZObject
{
public:
    SStatus() noexcept;
    ~SStatus() noexcept;

    std::string networkIcon, volumeIcon, batteryIcon;

    /// Output volume (0 to 1, may exceed 1 if boosted) and mute, -1 without an audio output.
    float volume { -1.f };
    bool muted { false };

    /// Battery level in percent, -1 without a battery.
    CZ::Int32 batteryPercent { -1 };

    /// Sets the output volume (0 to 1, unmuting if > 0). Throttled: fine to call on every drag step.
    void setVolume(float volume) noexcept;

    /// Human readable state of each (e.g. "Wi-Fi: 72 %"), for logs.
    std::string networkInfo, volumeInfo, batteryInfo;

    CZ::CZSignal<> onChanged;

private:
    void refreshNetwork() noexcept;
    void refreshBattery() noexcept;
    void refreshVolume() noexcept;
    void startVolumeMonitor() noexcept;
    void stopVolumeMonitor() noexcept;
    void applyBattery(double percentage, CZ::UInt32 state) noexcept;
    void applyVolumeState(bool ok, float volume, bool muted) noexcept;
    void set(std::string &icon, std::string value) noexcept;
    void applyVolume() noexcept;
    std::unique_ptr<CZ::CZTimer> m_setVolumeTimer;
    float m_pendingVolume { -1.f };

    std::shared_ptr<CZ::CZBus> m_system;
    sd_bus_slot *m_networkMatch {}, *m_batteryMatch {};

    // Debounce bursts of events (e.g. NetworkManager scans, volume slider drags).
    std::unique_ptr<CZ::CZTimer> m_networkTimer, m_volumeTimer, m_volumeRetryTimer;

    pid_t m_pactlPid { -1 };
    std::shared_ptr<CZ::CZEventSource> m_pactlSource;
    std::string m_pactlBuffer;

    bool m_networkBusy { false }, m_networkAgain { false };
    bool m_volumeBusy { false }, m_volumeAgain { false };
    bool m_settingVolume { false };

    // Last: destroyed first, so no completion runs on a half destroyed object.
    CZ::CZBusWorker m_worker { CZ::CZBusWorker::System };
};

#endif // CZ_SSTATUS_H
