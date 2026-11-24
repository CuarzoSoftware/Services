#ifndef CZ_SVNOTIFICATION_H
#define CZ_SVNOTIFICATION_H

#include <CZ/Services/Services.h>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief A desktop notification (Desktop Notifications Specification 1.2): what an app sends
 *        (SVNotificationsClient::send()) and a server receives (SVNotificationsServer).
 *
 * It also defines what the protocol's clients and servers share: the bus names and the enums.
 */
struct CZ::SVNotification
{
    /// The specification's bus name and interface (the same).
    static constexpr const char *FreedesktopName { "org.freedesktop.Notifications" };
    /// The specification's object path.
    static constexpr const char *FreedesktopPath { "/org/freedesktop/Notifications" };

    /// The notification center's (Gazette's) bus name and interface (the same).
    static constexpr const char *CenterName { "org.cuarzo.Gazette" };
    /// The notification center's object path.
    static constexpr const char *CenterPath { "/org/cuarzo/Gazette" };

    /// The urgency hint.
    enum class Urgency : CZ::UInt8
    {
        Low,        ///< E.g. a download finished
        Normal,     ///< Most notifications
        Critical    ///< Its banner stays until it's closed (e.g. the battery is about to run out)
    };

    /// Why a notification was closed (NotificationClosed's reason).
    enum class CloseReason : CZ::UInt32
    {
        Expired = 1,    ///< Its banner timed out (servers with a notification center keep it there)
        Dismissed = 2,  ///< By the user
        Closed = 3,     ///< By CloseNotification
        Undefined = 4   ///< Any other reason
    };

    /// Pixels sent with it (the image-data hint).
    struct Image
    {
        CZ::Int32 width { 0 };          ///< In pixels
        CZ::Int32 height { 0 };         ///< In pixels
        std::vector<CZ::UInt8> rgba;    ///< width × height pixels: R, G, B, A bytes (alpha not premultiplied)

        /// It has a size and all its pixels.
        bool valid() const noexcept { return width > 0 && height > 0 && rgba.size() == size_t(width) * size_t(height) * 4; }
    };

    std::string appName;        ///< Empty when sending: the program's name
    std::string appIcon;        ///< An icon name or a file path (or file:// URI)
    std::string summary;        ///< Its title
    std::string body;           ///< Text, possibly with a little markup (see SVNotificationsServer::PlainText())
    std::string desktopEntry;   ///< The app's desktop file id without ".desktop", e.g. "org.cuarzo.Settings"
    std::string imagePath;      ///< An image shown with it (a file path, a file:// URI or an icon name)
    Image image;                ///< Or its pixels (wins over imagePath)

    /// Action ids and their labels. The "default" one is clicking the notification (not a button).
    std::vector<std::pair<std::string, std::string>> actions;

    Urgency urgency { Urgency::Normal };    ///< How important it is (critical ones stay until closed)
    CZ::Int32 timeoutMs { -1 };     ///< -1: the server's default, 0: stays until closed
    CZ::UInt32 replaces { 0 };      ///< The id of one to update in place, 0: a new one
    bool transient { false };       ///< Not kept in the notification center once its banner is gone
    bool resident { false };        ///< Not removed when an action is invoked

    /// It has the "default" action.
    bool hasDefault() const noexcept
    {
        for (const auto &[id, label] : actions)
            if (id == "default")
                return true;

        return false;
    }
};

#endif // CZ_SVNOTIFICATION_H
