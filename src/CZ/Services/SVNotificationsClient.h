#ifndef CZ_SVNOTIFICATIONSCLIENT_H
#define CZ_SVNOTIFICATIONSCLIENT_H

#include <CZ/Services/SVNotification.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct sd_bus_slot;

/**
 * @brief The client side of desktop notifications (user bus): sending them, and controlling
 *        Gazette (Cuarzo's notifications daemon). See SVNotificationsServer for the other side.
 *
 * Sending works with any daemon implementing the
 * [Desktop Notifications Specification](https://specifications.freedesktop.org/notification-spec/latest/)
 * (`org.freedesktop.Notifications`). onActionInvoked and onClosed are emitted for the
 * notifications sent from here only.
 *
 * Gazette's state (whether it runs, its notification center, Do Not Disturb) is tracked over
 * `org.cuarzo.Gazette`, onChanged tells when it changed.
 *
 * Shared by the process: see GetOrMake().
 *
 * @code
 * auto notifications { SVNotificationsClient::GetOrMake() };
 * SVNotification n;
 * n.summary = "Download complete";
 * n.body = "photo.png";
 * n.actions = { { "default", "Open" } };
 * notifications->send(n);
 * notifications->onActionInvoked.subscribe(this, [](UInt32 id, const std::string &action){ ... });
 * @endcode
 */
class CZ::SVNotificationsClient final : public CZ::CZObject
{
public:
    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SVNotificationsClient> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SVNotificationsClient> Get() noexcept;

    ~SVNotificationsClient() noexcept;

    /* ---------------------------------------- Sending ---------------------------------------- */

    /**
     * @brief Sends @p notification.
     *
     * @param sent Called with its id once the server replied (0 if it failed, e.g. no daemon).
     */
    void send(const SVNotification &notification, std::function<void(CZ::UInt32 id)> sent = {}) noexcept;

    /// Closes a notification sent from here (its banner, and from the notification center).
    void close(CZ::UInt32 id) noexcept;

    /// An action of a notification sent from here was invoked ("default": it was clicked).
    CZ::CZSignal<CZ::UInt32 /*id*/, const std::string& /*action*/> onActionInvoked;

    /// A notification sent from here was closed.
    CZ::CZSignal<CZ::UInt32 /*id*/, SVNotification::CloseReason> onClosed;

    /* ---------------------------------------- Gazette ---------------------------------------- */

    /// Gazette is running.
    bool available() const noexcept { return m_available; }

    /// Its notification center is shown.
    bool centerShown() const noexcept { return m_centerShown; }

    /// Do Not Disturb is on (new notifications show no banners).
    bool doNotDisturb() const noexcept { return m_dnd; }

    /// Notifications in the notification center.
    CZ::UInt32 count() const noexcept { return m_count; }

    /// Shows or hides the notification center on the screen (output) named @p screen (empty: the default one).
    void toggleCenter(const std::string &screen = {}) noexcept;

    /// Shows the notification center on the screen (output) named @p screen (empty: the default one).
    void showCenter(const std::string &screen = {}) noexcept;

    /// Hides the notification center.
    void hideCenter() noexcept;

    /// Turns Do Not Disturb on or off (Gazette saves it).
    void setDoNotDisturb(bool enabled) noexcept;

    /// Gazette's state above changed (or it started or quit).
    CZ::CZSignal<> onChanged;

private:
    SVNotificationsClient() noexcept;
    void fetchState() noexcept;
    void setState(bool available, bool centerShown, bool dnd, CZ::UInt32 count) noexcept;

    std::shared_ptr<CZ::CZBus> m_bus;
    sd_bus_slot *m_stateMatch {}, *m_ownerMatch {}, *m_actionMatch {}, *m_closedMatch {};
    sd_bus_slot *m_fetchSlot {};

    // Calls waiting for their reply (released with the instance: no reply reaches a dead one)
    struct Pending
    {
        SVNotificationsClient *self;
        CZ::UInt64 key;
        sd_bus_slot *slot { nullptr };
        std::function<void(CZ::UInt32)> sent;
    };
    std::map<CZ::UInt64, std::unique_ptr<Pending>> m_pending;
    CZ::UInt64 m_nextKey { 1 };

    std::set<CZ::UInt32> m_sent; // Ids of ours not closed yet

    bool m_available { false }, m_centerShown { false }, m_dnd { false };
    CZ::UInt32 m_count { 0 };
};

#endif // CZ_SVNOTIFICATIONSCLIENT_H
