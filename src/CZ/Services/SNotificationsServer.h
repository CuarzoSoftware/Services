#ifndef CZ_SNOTIFICATIONSSERVER_H
#define CZ_SNOTIFICATIONSSERVER_H

#include <CZ/Services/SNotification.h>
#include <CZ/Core/CZObject.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct sd_bus_slot;

/**
 * @brief The server side of desktop notifications (user bus), for a notifications daemon.
 *
 * Implements the protocol and leaves showing the notifications to the daemon, through Handlers:
 *
 * - `org.freedesktop.Notifications` at `/org/freedesktop/Notifications` (the
 *   [Desktop Notifications Specification](https://specifications.freedesktop.org/notification-spec/latest/) 1.2):
 *   Notify (its arguments and hints read into an SNotification), CloseNotification,
 *   GetCapabilities, GetServerInformation, and the NotificationClosed / ActionInvoked signals
 *   (emitClosed(), emitActionInvoked()). Hints read: urgency, desktop-entry, image-data (and the
 *   older image_data and icon_data), image-path (and image_path), transient and resident.
 * - Optionally, the notification center's `org.cuarzo.Gazette` at `/org/cuarzo/Gazette` (what
 *   SNotificationsClient controls): ToggleCenter(s screen), ShowCenter(s screen), HideCenter(),
 *   SetDoNotDisturb(b), GetState() → (b centerShown, b doNotDisturb, u count), and the
 *   StateChanged(bbu) signal (emitCenterState()).
 *
 * @code
 * SNotificationsServer::Handlers handlers;
 * handlers.notify = [](const SNotification &n) -> UInt32 { return show(n); }; // Its id
 * handlers.close = [](UInt32 id){ remove(id, SNotification::CloseReason::Closed); };
 * server = SNotificationsServer::GetOrMake();
 * server->start(handlers, { .name = "My Daemon", .vendor = "Me", .version = "1.0" });
 * @endcode
 *
 * Shared by the process: see GetOrMake().
 */
class CZ::SNotificationsServer final : public CZ::CZObject
{
public:
    /// What the notification center's interface reports.
    struct CenterState
    {
        bool shown { false };
        bool doNotDisturb { false };
        CZ::UInt32 count { 0 };     ///< Notifications in it
    };

    /// What the daemon does with the requests (called on the main thread).
    struct Handlers
    {
        /**
         * @brief Notify: shows @p notification (its summary and body as sent, see PlainText()).
         *
         * Required. Returns its id: notification.replaces if that one is still there (updated in
         * place), else a new one (never 0).
         */
        std::function<CZ::UInt32(const SNotification &notification)> notify;

        /// CloseNotification: removes it (the daemon emits NotificationClosed with Closed, see emitClosed()).
        std::function<void(CZ::UInt32 id)> close;

        /// The notification center's interface, registered if centerState is set.
        std::function<CenterState()> centerState;
        std::function<void(const std::string &screen)> toggleCenter, showCenter; ///< Screen (output) name, empty: the default one
        std::function<void()> hideCenter;
        std::function<void(bool enabled)> setDoNotDisturb;
    };

    /// What GetServerInformation and GetCapabilities answer.
    struct Info
    {
        std::string name { "Cuarzo" };
        std::string vendor { "Cuarzo" };
        std::string version { "1.0" };
        std::vector<std::string> capabilities { "actions", "body", "body-markup", "icon-static", "persistence" };
    };

    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SNotificationsServer> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SNotificationsServer> Get() noexcept;

    ~SNotificationsServer() noexcept;

    /**
     * @brief Registers the interfaces and owns their names (replacing another daemon if it allows it).
     *
     * @return false without a user bus or if org.freedesktop.Notifications couldn't be owned.
     */
    bool start(const Handlers &handlers, const Info &info) noexcept;

    /// start() with the default Info.
    bool start(const Handlers &handlers) noexcept;

    /// Started (see start()).
    bool running() const noexcept { return m_running; }

    /// NotificationClosed: tells its app.
    void emitClosed(CZ::UInt32 id, SNotification::CloseReason reason) noexcept;

    /// ActionInvoked: tells its app ("default": the notification was clicked).
    void emitActionInvoked(CZ::UInt32 id, const std::string &action) noexcept;

    /// StateChanged: the notification center's state changed (call it on every change).
    void emitCenterState() noexcept;

    /// Removes the markup (tags, a line break for `<br>`) of a summary or body and decodes its entities.
    static std::string PlainText(const std::string &markup) noexcept;

    /// @cond INTERNAL
    const Handlers &handlers() const noexcept { return m_handlers; }
    const Info &info() const noexcept { return m_info; }
    /// @endcond

private:
    SNotificationsServer() noexcept;
    std::shared_ptr<CZ::CZBus> m_bus;
    sd_bus_slot *m_notificationsSlot {}, *m_centerSlot {};
    Handlers m_handlers;
    Info m_info;
    bool m_running { false };
};

#endif // CZ_SNOTIFICATIONSSERVER_H
