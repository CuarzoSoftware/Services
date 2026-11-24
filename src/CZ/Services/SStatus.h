#ifndef CZ_SSTATUS_H
#define CZ_SSTATUS_H

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
 * - Volume: the default output's, from SAudio (shared with the rest of the process). Muted, low,
 *   medium or high.
 * - Battery: UPower's display device (system bus), its level in steps of 10 %, and charging /
 *   charged / plugged in.
 *
 * Everything is event driven (D-Bus signals, SAudio's changes), onChanged is emitted when the state
 * changes. The queries run off the UI thread (see CZBusWorker).
 *
 * Shared by the process: see GetOrMake().
 */
class CZ::SStatus final : public CZ::CZObject
{
public:
    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SStatus> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SStatus> Get() noexcept;

    ~SStatus() noexcept;

    std::string networkIcon, volumeIcon, batteryIcon;

    /// Output volume (0 to 1, may exceed 1 if boosted) and mute, -1 without an audio output.
    float volume { -1.f };
    bool muted { false };

    /// Battery level in percent, -1 without a battery.
    CZ::Int32 batteryPercent { -1 };

    /// Sets the output volume (0 to 1, muted at 0, unmuted above). Coalesced: fine to call on every drag step.
    void setVolume(float volume) noexcept;

    /// Human readable state of each (e.g. "Wi-Fi: 72 %"), for logs.
    std::string networkInfo, volumeInfo, batteryInfo;

    CZ::CZSignal<> onChanged;

private:
    SStatus() noexcept;
    void refreshNetwork() noexcept;
    void refreshBattery() noexcept;
    void applyBattery(double percentage, CZ::UInt32 state) noexcept;
    void applyVolumeState() noexcept;
    void set(std::string &icon, std::string value) noexcept;

    std::shared_ptr<SAudio> m_audio;
    std::shared_ptr<CZ::CZBus> m_system;
    sd_bus_slot *m_networkMatch {}, *m_batteryMatch {};

    // Debounce bursts of events (e.g. NetworkManager scans)
    std::unique_ptr<CZ::CZTimer> m_networkTimer;
    bool m_networkBusy { false }, m_networkAgain { false };

    // Last: destroyed first, so no completion runs on a half destroyed object.
    CZ::CZBusWorker m_worker { CZ::CZBusWorker::System };
};

#endif // CZ_SSTATUS_H
