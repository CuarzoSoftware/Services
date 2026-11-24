#ifndef CZ_SVWIFI_H
#define CZ_SVWIFI_H

#include <CZ/Services/Services.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZBusWorker.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Core/CZBus.h>
#include <memory>
#include <string>
#include <vector>

struct sd_bus_slot;

/// A Wi-Fi network in range (its access points grouped by name).
struct CZ::SVWiFiNetwork
{
    std::string ssid;               ///< Its name
    CZ::UInt8 strength { 0 };       ///< 0..100, of its strongest access point
    bool secured { false };         ///< Asks for a password (or credentials)
    bool enterprise { false };      ///< 802.1X (not supported for joining)
    bool sae { false };             ///< WPA3 only
    bool wep { false };             ///< WEP only (not supported for joining)
    std::string accessPoint;        ///< Its strongest access point (D-Bus path)
    std::string connection;         ///< Its saved profile (D-Bus path), empty if unknown
    std::string uuid;               ///< Its saved profile's UUID
    bool autoJoin { true };         ///< Its saved profile connects automatically

    /// It has a saved profile (joined before).
    bool known() const noexcept { return !connection.empty(); }

    /// "None", "WEP", "WPA2 Personal", "WPA3 Personal" or "WPA2 Enterprise".
    std::string securityName() const noexcept;
};

/// NetworkManager's Wi-Fi: the device, its link and the networks in range.
struct CZ::SVWiFiState
{
    /// The device's connection.
    enum class Link
    {
        Disconnected,   ///< Not connected (or off)
        Connecting,     ///< Joining a network (activeSsid)
        Connected       ///< Connected to activeSsid
    };

    bool loaded { false };          ///< Read at least once
    bool available { false };       ///< NetworkManager runs and there's a Wi-Fi device
    bool enabled { false };         ///< Wi-Fi is on
    std::string device;             ///< The Wi-Fi device (D-Bus path)
    std::string interface;          ///< Its network interface, e.g. "wlp0s20f3"
    std::string hwAddress;          ///< Its MAC address
    Link link { Link::Disconnected };   ///< Its connection
    std::string activeSsid;         ///< Connected (or connecting) to
    std::string ipAddress;          ///< Its IP address while connected, e.g. "192.168.1.20/24"
    std::string router;             ///< The gateway's address while connected
    std::vector<SVWiFiNetwork> networks; ///< In range, by name

    /// The network connected (or connecting) to, nullptr if none (or not in networks).
    const SVWiFiNetwork *active() const noexcept;

    /// @cond INTERNAL
    CZ::UInt32 deviceState { 0 }, stateReason { 0 };
    /// @endcond
};

/**
 * @brief NetworkManager's Wi-Fi (system bus): its state, joining and forgetting networks.
 *
 * Everything is read and done off the UI thread (see CZBusWorker); onChanged tells when state()
 * changed. Without NetworkManager (or a Wi-Fi device) state().available is false.
 *
 * Shared by the process: see GetOrMake().
 */
class CZ::SVWiFi final : public CZ::CZObject
{
public:
    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SVWiFi> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SVWiFi> Get() noexcept;

    ~SVWiFi() noexcept;

    /// The current state (see onChanged).
    const SVWiFiState &state() const noexcept { return m_state; }

    /// Turns Wi-Fi on or off (shown in state() right away).
    void setEnabled(bool enabled) noexcept;

    /**
     * @brief Asks the device to look for networks (e.g. while a list of networks is shown).
     *
     * While it's on and no network is listed (e.g. right after turning it on, when the device
     * isn't ready to scan yet), it's asked again every few seconds until some show up.
     */
    void scan() noexcept;

    /// On, but no network is listed yet (e.g. just turned on): looking for them.
    bool searching() const noexcept { return m_state.available && m_state.enabled && m_state.networks.empty(); }

    /**
     * @brief Joins @p network: with its saved profile, or a new one (with @p password if secured).
     *
     * If it fails because of the password, onJoinFailed is emitted (a new profile is deleted).
     */
    void join(const SVWiFiNetwork &network, const std::string &password = "") noexcept;

    /// Deletes its saved profile.
    void forget(const SVWiFiNetwork &network) noexcept;

    /// Whether its saved profile connects automatically.
    void setAutoJoin(const SVWiFiNetwork &network, bool autoJoin) noexcept;

    /// state() changed.
    CZ::CZSignal<> onChanged;

    /// Joining @p ssid failed: wrongPassword if it was rejected (or missing).
    CZ::CZSignal<const std::string& /*ssid*/, bool /*wrongPassword*/> onJoinFailed;

private:
    SVWiFi() noexcept;
    void refresh() noexcept;
    void apply(SVWiFiState state) noexcept;

    CZ::CZBusWorker m_worker;
    SVWiFiState m_state;
    std::unique_ptr<CZ::CZTimer> m_refreshTimer;
    std::unique_ptr<CZ::CZTimer> m_scanRetry; // While on without networks (e.g. just turned on)
    std::shared_ptr<CZ::CZBus> m_system; // Kept: the connection lives while it's held (the match too)
    sd_bus_slot *m_match {};
    bool m_busy { false }, m_again { false };
    bool m_scanPending { false }; // Asked before the device was known

    // A join in progress (see onJoinFailed)
    struct Joining
    {
        std::string ssid;
        std::string newConnection; // Created for it
        CZ::Int64 startMs { 0 };
        bool sawProgress { false };
    };
    std::unique_ptr<Joining> m_joining;
};

#endif // CZ_SVWIFI_H
