#include <CZ/Services/SVNotificationsServer.h>
#include <CZ/Services/SVLog.h>
#include <CZ/Core/CZBus.h>
#include <systemd/sd-bus.h>
#include <algorithm>
#include <cstring>

using namespace CZ;

static std::weak_ptr<SVNotificationsServer> s_instance;

std::shared_ptr<SVNotificationsServer> SVNotificationsServer::GetOrMake() noexcept
{
    auto instance { s_instance.lock() };

    if (!instance)
        s_instance = instance = std::shared_ptr<SVNotificationsServer>(new SVNotificationsServer());

    return instance;
}

std::shared_ptr<SVNotificationsServer> SVNotificationsServer::Get() noexcept
{
    return s_instance.lock();
}

/* ------------------------------------------ Helpers ------------------------------------------ */

std::string SVNotificationsServer::PlainText(const std::string &markup) noexcept
{
    std::string out;
    out.reserve(markup.size());

    for (size_t i = 0; i < markup.size(); i++)
    {
        const char c { markup[i] };

        // A tag: <b>, </i>, <a href="...">, <img .../>, <br/> (a line break)
        if (c == '<')
        {
            const size_t end { markup.find('>', i) };

            if (end == std::string::npos)
            {
                out.push_back(c);
                continue;
            }

            const std::string tag { markup.substr(i + 1, end - i - 1) };

            if (tag.starts_with("br"))
                out.push_back('\n');

            i = end;
            continue;
        }

        if (c == '&')
        {
            static const std::pair<const char*, const char*> entities[] {
                { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }, { "&#39;", "'" }, { "&nbsp;", " " } };
            bool decoded { false };

            for (const auto &[entity, value] : entities)
            {
                if (markup.compare(i, std::strlen(entity), entity) == 0)
                {
                    out.append(value);
                    i += std::strlen(entity) - 1;
                    decoded = true;
                    break;
                }
            }

            if (decoded)
                continue;
        }

        out.push_back(c);
    }

    // Trailing blank lines / spaces
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r'))
        out.pop_back();

    return out;
}

// image-data: (iiibiiay) width, height, rowstride, has_alpha, bits_per_sample, channels, data (RGB(A))
static SVNotification::Image ReadImageData(sd_bus_message *m) noexcept
{
    Int32 width, height, stride, hasAlpha, bits, channels;
    const void *data {};
    size_t size {};

    if (sd_bus_message_enter_container(m, 'r', "iiibiiay") <= 0)
        return {};

    const bool ok { sd_bus_message_read(m, "iiibii", &width, &height, &stride, &hasAlpha, &bits, &channels) >= 0 &&
                    sd_bus_message_read_array(m, 'y', &data, &size) >= 0 };
    sd_bus_message_exit_container(m);

    if (!ok || width <= 0 || height <= 0 || width > 4096 || height > 4096 || bits != 8 ||
        (channels != 3 && channels != 4) || stride < width * channels || size < size_t(stride) * size_t(height - 1) + size_t(width * channels))
        return {};

    SVNotification::Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(size_t(width) * size_t(height) * 4);
    const auto *src { static_cast<const UInt8*>(data) };

    for (Int32 y = 0; y < height; y++)
    {
        const UInt8 *row { src + size_t(y) * size_t(stride) };
        UInt8 *dst { &image.rgba[size_t(y) * size_t(width) * 4] };

        for (Int32 x = 0; x < width; x++)
        {
            const UInt8 *p { row + x * channels };
            dst[x * 4 + 0] = p[0];
            dst[x * 4 + 1] = p[1];
            dst[x * 4 + 2] = p[2];
            dst[x * 4 + 3] = channels == 4 && hasAlpha ? p[3] : UInt8(255);
        }
    }

    return image;
}

/* -------------------------------- org.freedesktop.Notifications -------------------------------- */

static int Notify(sd_bus_message *m, void *data, sd_bus_error *)
{
    auto *server { static_cast<SVNotificationsServer*>(data) };
    const char *appName {}, *appIcon {}, *summary {}, *body {};
    SVNotification n;

    int r { sd_bus_message_read(m, "susss", &appName, &n.replaces, &appIcon, &summary, &body) };

    if (r < 0)
        return r;

    n.appName = appName ? appName : "";
    n.appIcon = appIcon ? appIcon : "";
    n.summary = summary ? summary : "";
    n.body = body ? body : "";

    // Actions: id, label, id, label...
    if ((r = sd_bus_message_enter_container(m, 'a', "s")) < 0)
        return r;

    std::vector<std::string> actions;
    const char *str {};

    while (sd_bus_message_read(m, "s", &str) > 0)
        actions.emplace_back(str ? str : "");

    sd_bus_message_exit_container(m);

    for (size_t i = 0; i + 1 < actions.size(); i += 2)
        n.actions.emplace_back(actions[i], actions[i + 1]);

    // Hints
    if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0)
        return r;

    bool imageData { false }; // From image-data (wins over the older names)

    while (sd_bus_message_enter_container(m, 'e', "sv") > 0)
    {
        const char *key {};
        sd_bus_message_read(m, "s", &key);
        const std::string k { key ? key : "" };
        const char *contents {};
        char type {};
        sd_bus_message_peek_type(m, &type, &contents);
        const std::string sig { contents ? contents : "" };
        bool read { false };

        if (sd_bus_message_enter_container(m, 'v', contents) > 0)
        {
            if (k == "urgency" && sig == "y")
            {
                UInt8 u { 1 };
                sd_bus_message_read(m, "y", &u);
                n.urgency = SVNotification::Urgency(std::min<UInt8>(u, 2));
                read = true;
            }
            else if ((k == "desktop-entry" || k == "image-path" || k == "image_path") && sig == "s")
            {
                sd_bus_message_read(m, "s", &str);
                (k == "desktop-entry" ? n.desktopEntry : n.imagePath) = str ? str : "";
                read = true;
            }
            else if ((k == "transient" || k == "resident") && sig == "b")
            {
                int b { 0 };
                sd_bus_message_read(m, "b", &b);
                (k == "transient" ? n.transient : n.resident) = b;
                read = true;
            }
            else if ((k == "image-data" || k == "image_data" || k == "icon_data") && sig == "(iiibiiay)")
            {
                if (auto image { ReadImageData(m) }; image.valid() && (!imageData || k == "image-data"))
                {
                    imageData = k == "image-data";
                    n.image = std::move(image);
                }

                read = true;
            }

            if (!read)
                sd_bus_message_skip(m, contents);

            sd_bus_message_exit_container(m);
        }

        sd_bus_message_exit_container(m);
    }

    sd_bus_message_exit_container(m);
    sd_bus_message_read(m, "i", &n.timeoutMs);

    const UInt32 id { server->handlers().notify ? server->handlers().notify(n) : 0 };
    return sd_bus_reply_method_return(m, "u", id);
}

static int CloseNotification(sd_bus_message *m, void *data, sd_bus_error *)
{
    auto *server { static_cast<SVNotificationsServer*>(data) };
    UInt32 id { 0 };

    if (int r = sd_bus_message_read(m, "u", &id); r < 0)
        return r;

    if (server->handlers().close)
        server->handlers().close(id);

    return sd_bus_reply_method_return(m, "");
}

static int GetCapabilities(sd_bus_message *m, void *data, sd_bus_error *)
{
    auto *server { static_cast<SVNotificationsServer*>(data) };
    sd_bus_message *reply {};

    if (int r = sd_bus_message_new_method_return(m, &reply); r < 0)
        return r;

    std::vector<char*> caps;

    for (const auto &c : server->info().capabilities)
        caps.push_back(const_cast<char*>(c.c_str()));

    caps.push_back(nullptr);
    sd_bus_message_append_strv(reply, caps.data());
    const int r { sd_bus_send(nullptr, reply, nullptr) };
    sd_bus_message_unref(reply);
    return r;
}

static int GetServerInformation(sd_bus_message *m, void *data, sd_bus_error *)
{
    const auto &info { static_cast<SVNotificationsServer*>(data)->info() };
    return sd_bus_reply_method_return(m, "ssss", info.name.c_str(), info.vendor.c_str(), info.version.c_str(), "1.2");
}

static const sd_bus_vtable NotificationsVTable[]
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", Notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", CloseNotification, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetCapabilities", "", "as", GetCapabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", GetServerInformation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
    SD_BUS_SIGNAL("ActionInvoked", "us", 0),
    SD_BUS_VTABLE_END
};

/* ------------------------------------ org.cuarzo.Gazette ------------------------------------ */

static int ToggleCenter(sd_bus_message *m, void *data, sd_bus_error *)
{
    const auto &h { static_cast<SVNotificationsServer*>(data)->handlers() };
    const char *screen {};
    sd_bus_message_read(m, "s", &screen);

    if (h.toggleCenter)
        h.toggleCenter(screen ? screen : "");

    return sd_bus_reply_method_return(m, "");
}

static int ShowCenter(sd_bus_message *m, void *data, sd_bus_error *)
{
    const auto &h { static_cast<SVNotificationsServer*>(data)->handlers() };
    const char *screen {};
    sd_bus_message_read(m, "s", &screen);

    if (h.showCenter)
        h.showCenter(screen ? screen : "");

    return sd_bus_reply_method_return(m, "");
}

static int HideCenter(sd_bus_message *m, void *data, sd_bus_error *)
{
    const auto &h { static_cast<SVNotificationsServer*>(data)->handlers() };

    if (h.hideCenter)
        h.hideCenter();

    return sd_bus_reply_method_return(m, "");
}

static int SetDoNotDisturb(sd_bus_message *m, void *data, sd_bus_error *)
{
    const auto &h { static_cast<SVNotificationsServer*>(data)->handlers() };
    int enabled { 0 };

    if (int r = sd_bus_message_read(m, "b", &enabled); r < 0)
        return r;

    if (h.setDoNotDisturb)
        h.setDoNotDisturb(enabled);

    return sd_bus_reply_method_return(m, "");
}

static int GetState(sd_bus_message *m, void *data, sd_bus_error *)
{
    const auto state { static_cast<SVNotificationsServer*>(data)->handlers().centerState() };
    return sd_bus_reply_method_return(m, "bbu", int(state.shown), int(state.doNotDisturb), state.count);
}

static const sd_bus_vtable CenterVTable[]
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ToggleCenter", "s", "", ToggleCenter, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ShowCenter", "s", "", ShowCenter, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("HideCenter", "", "", HideCenter, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetDoNotDisturb", "b", "", SetDoNotDisturb, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetState", "", "bbu", GetState, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("StateChanged", "bbu", 0),
    SD_BUS_VTABLE_END
};

/* ------------------------------------ SVNotificationsServer ----------------------------------- */

SVNotificationsServer::SVNotificationsServer() noexcept {}

SVNotificationsServer::~SVNotificationsServer() noexcept
{
    if (m_bus && m_running)
    {
        sd_bus_release_name(m_bus->bus(), SVNotification::FreedesktopName);

        if (m_centerSlot)
            sd_bus_release_name(m_bus->bus(), SVNotification::CenterName);
    }

    sd_bus_slot_unref(m_notificationsSlot);
    sd_bus_slot_unref(m_centerSlot);
}

bool SVNotificationsServer::start(const Handlers &handlers) noexcept
{
    return start(handlers, Info());
}

bool SVNotificationsServer::start(const Handlers &handlers, const Info &info) noexcept
{
    if (m_running)
        return true;

    m_handlers = handlers;
    m_info = info;
    m_bus = CZBus::GetOrMakeUser();

    if (!m_bus)
    {
        SVLog(CZError, CZLN, "No user bus: no notifications");
        return false;
    }

    sd_bus *bus { m_bus->bus() };
    sd_bus_add_object_vtable(bus, &m_notificationsSlot, SVNotification::FreedesktopPath, SVNotification::FreedesktopName, NotificationsVTable, this);

    if (m_handlers.centerState)
        sd_bus_add_object_vtable(bus, &m_centerSlot, SVNotification::CenterPath, SVNotification::CenterName, CenterVTable, this);

    // Taken from another daemon if it allows it (most do)
    if (int r = sd_bus_request_name(bus, SVNotification::FreedesktopName, SD_BUS_NAME_REPLACE_EXISTING); r < 0)
    {
        SVLog(CZError, CZLN, "Couldn't own {} (another notifications daemon running?): {}", SVNotification::FreedesktopName, strerror(-r));
        sd_bus_slot_unref(m_notificationsSlot);
        sd_bus_slot_unref(m_centerSlot);
        m_notificationsSlot = m_centerSlot = nullptr;
        return false;
    }

    if (m_centerSlot)
        if (int r = sd_bus_request_name(bus, SVNotification::CenterName, 0); r < 0)
            SVLog(CZError, CZLN, "Couldn't own {}: {}", SVNotification::CenterName, strerror(-r));

    m_running = true;
    return true;
}

void SVNotificationsServer::emitClosed(UInt32 id, SVNotification::CloseReason reason) noexcept
{
    if (m_running)
        sd_bus_emit_signal(m_bus->bus(), SVNotification::FreedesktopPath, SVNotification::FreedesktopName,
            "NotificationClosed", "uu", id, UInt32(reason));
}

void SVNotificationsServer::emitActionInvoked(UInt32 id, const std::string &action) noexcept
{
    if (m_running)
        sd_bus_emit_signal(m_bus->bus(), SVNotification::FreedesktopPath, SVNotification::FreedesktopName,
            "ActionInvoked", "us", id, action.c_str());
}

void SVNotificationsServer::emitCenterState() noexcept
{
    if (!m_running || !m_centerSlot)
        return;

    const auto state { m_handlers.centerState() };
    sd_bus_emit_signal(m_bus->bus(), SVNotification::CenterPath, SVNotification::CenterName,
        "StateChanged", "bbu", int(state.shown), int(state.doNotDisturb), state.count);
}
