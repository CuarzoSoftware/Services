#ifndef CZ_SNOTIFICATIONS_H
#define CZ_SNOTIFICATIONS_H

#include <CZ/Services/Services.h>
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

/// A notification to send (see SNotifications::send()).
struct CZ::SNotification
{
    enum class Urgency : CZ::UInt8 { Low, Normal, Critical };

    std::string appName;        ///< Empty: the program's name
    std::string appIcon;        ///< An icon name or a file path
    std::string summary;        ///< Its title
    std::string body;           ///< Plain text (most servers also accept a little markup)
    std::string desktopEntry;   ///< The app's desktop file id without ".desktop", e.g. "org.cuarzo.Settings"
    std::string imagePath;      ///< An image shown with it (a file path or an icon name)

    /// Buttons: an action id and its label (the "default" action is clicking the notification itself).
    std::vector<std::pair<std::string, std::string>> actions;

    Urgency urgency { Urgency::Normal };
    CZ::Int32 timeoutMs { -1 };     ///< -1: the server's default, 0: stays until closed
    CZ::UInt32 replaces { 0 };      ///< The id of one to update in place, 0: a new one
    bool transient { false };       ///< Not kept in the notification center once its banner is gone
    bool resident { false };        ///< Not removed when an action is invoked
};

/**
 * @brief Desktop notifications (user bus): sending them, and controlling Gazette (Cuarzo's
 *        notifications daemon).
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
 * auto notifications { SNotifications::GetOrMake() };
 * SNotification n;
 * n.summary = "Download complete";
 * n.body = "photo.png";
 * n.actions = { { "default", "Open" } };
 * notifications->send(n);
 * notifications->onActionInvoked.subscribe(this, [](UInt32 id, const std::string &action){ ... });
 * @endcode
 */
class CZ::SNotifications final : public CZ::CZObject
{
public:
    /// Why a notification was closed (NotificationClosed's reason).
    enum class CloseReason : CZ::UInt32
    {
        Expired = 1,    ///< Its banner timed out (servers with a notification center keep it there)
        Dismissed = 2,  ///< By the user
        Closed = 3,     ///< By close()
        Undefined = 4
    };

    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SNotifications> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SNotifications> Get() noexcept;

    ~SNotifications() noexcept;

    /* ---------------------------------------- Sending ---------------------------------------- */

    /**
     * @brief Sends @p notification.
     *
     * @param sent Called with its id once the server replied (0 if it failed, e.g. no daemon).
     */
    void send(const SNotification &notification, std::function<void(CZ::UInt32 id)> sent = {}) noexcept;

    /// Closes a notification sent from here (its banner, and from the notification center).
    void close(CZ::UInt32 id) noexcept;

    /// An action of a notification sent from here was invoked ("default": it was clicked).
    CZ::CZSignal<CZ::UInt32 /*id*/, const std::string& /*action*/> onActionInvoked;

    /// A notification sent from here was closed.
    CZ::CZSignal<CZ::UInt32 /*id*/, CloseReason> onClosed;

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
    void showCenter(const std::string &screen = {}) noexcept;
    void hideCenter() noexcept;

    void setDoNotDisturb(bool enabled) noexcept;

    /// Gazette's state above changed (or it started or quit).
    CZ::CZSignal<> onChanged;

private:
    SNotifications() noexcept;
    void fetchState() noexcept;
    void setState(bool available, bool centerShown, bool dnd, CZ::UInt32 count) noexcept;

    std::shared_ptr<CZ::CZBus> m_bus;
    sd_bus_slot *m_stateMatch {}, *m_ownerMatch {}, *m_actionMatch {}, *m_closedMatch {};
    sd_bus_slot *m_fetchSlot {};

    // Calls waiting for their reply (released with the instance: no reply reaches a dead one)
    struct Pending
    {
        SNotifications *self;
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

#endif // CZ_SNOTIFICATIONS_H
