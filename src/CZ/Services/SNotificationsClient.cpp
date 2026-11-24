#include <CZ/Services/SNotificationsClient.h>
#include <CZ/Services/SLog.h>
#include <CZ/Core/CZBus.h>
#include <systemd/sd-bus.h>
#include <cerrno>
#include <cstring>

using namespace CZ;

static std::weak_ptr<SNotificationsClient> s_instance;

std::shared_ptr<SNotificationsClient> SNotificationsClient::GetOrMake() noexcept
{
    auto instance { s_instance.lock() };

    if (!instance)
        s_instance = instance = std::shared_ptr<SNotificationsClient>(new SNotificationsClient());

    return instance;
}

std::shared_ptr<SNotificationsClient> SNotificationsClient::Get() noexcept
{
    return s_instance.lock();
}

SNotificationsClient::SNotificationsClient() noexcept
{
    m_bus = CZBus::GetOrMakeUser();

    if (!m_bus)
    {
        SLog(CZWarning, CZLN, "No user bus: notifications unavailable");
        return;
    }

    sd_bus *bus { m_bus->bus() };

    // Gazette's state
    sd_bus_match_signal_async(bus, &m_stateMatch, nullptr, SNotification::CenterPath, SNotification::CenterName, "StateChanged",
        [](sd_bus_message *m, void *data, sd_bus_error *) -> int {
            int shown { 0 }, dnd { 0 };
            UInt32 count { 0 };

            if (sd_bus_message_read(m, "bbu", &shown, &dnd, &count) >= 0)
                static_cast<SNotificationsClient*>(data)->setState(true, shown, dnd, count);

            return 0;
        }, nullptr, this);

    // Gazette started or quit
    sd_bus_match_signal_async(bus, &m_ownerMatch, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameOwnerChanged",
        [](sd_bus_message *m, void *data, sd_bus_error *) -> int {
            const char *name {}, *oldOwner {}, *newOwner {};

            if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || !name || std::strcmp(name, SNotification::CenterName) != 0)
                return 0;

            auto *self { static_cast<SNotificationsClient*>(data) };

            if (newOwner && newOwner[0])
                self->fetchState();
            else
                self->setState(false, false, false, 0);

            return 0;
        }, nullptr, this);

    // The notifications sent from here: their actions and closing
    sd_bus_match_signal_async(bus, &m_actionMatch, nullptr, SNotification::FreedesktopPath, SNotification::FreedesktopName, "ActionInvoked",
        [](sd_bus_message *m, void *data, sd_bus_error *) -> int {
            auto *self { static_cast<SNotificationsClient*>(data) };
            UInt32 id { 0 };
            const char *action {};

            if (sd_bus_message_read(m, "us", &id, &action) >= 0 && self->m_sent.contains(id))
                self->onActionInvoked.notify(id, action ? action : "");

            return 0;
        }, nullptr, this);

    sd_bus_match_signal_async(bus, &m_closedMatch, nullptr, SNotification::FreedesktopPath, SNotification::FreedesktopName, "NotificationClosed",
        [](sd_bus_message *m, void *data, sd_bus_error *) -> int {
            auto *self { static_cast<SNotificationsClient*>(data) };
            UInt32 id { 0 }, reason { 0 };

            if (sd_bus_message_read(m, "uu", &id, &reason) >= 0 && self->m_sent.erase(id))
                self->onClosed.notify(id, reason >= 1 && reason <= 3 ? SNotification::CloseReason(reason) : SNotification::CloseReason::Undefined);

            return 0;
        }, nullptr, this);

    fetchState();
}

SNotificationsClient::~SNotificationsClient() noexcept
{
    for (auto &[key, pending] : m_pending)
        sd_bus_slot_unref(pending->slot);

    sd_bus_slot_unref(m_fetchSlot);
    sd_bus_slot_unref(m_stateMatch);
    sd_bus_slot_unref(m_ownerMatch);
    sd_bus_slot_unref(m_actionMatch);
    sd_bus_slot_unref(m_closedMatch);
}

/* ------------------------------------------ Sending ------------------------------------------ */

// Appends a hint: {name, variant of the basic type @p type}
template <class T>
static void AppendHint(sd_bus_message *m, const char *name, const char *type, T value) noexcept
{
    sd_bus_message_open_container(m, 'e', "sv");
    sd_bus_message_append(m, "s", name);
    sd_bus_message_open_container(m, 'v', type);
    sd_bus_message_append(m, type, value);
    sd_bus_message_close_container(m);
    sd_bus_message_close_container(m);
}

void SNotificationsClient::send(const SNotification &n, std::function<void(UInt32 id)> sent) noexcept
{
    if (!m_bus)
    {
        if (sent)
            sent(0);
        return;
    }

    sd_bus_message *m {};

    if (sd_bus_message_new_method_call(m_bus->bus(), &m, SNotification::FreedesktopName, SNotification::FreedesktopPath, SNotification::FreedesktopName, "Notify") < 0)
    {
        if (sent)
            sent(0);
        return;
    }

    const std::string appName { n.appName.empty() ? program_invocation_short_name : n.appName };
    sd_bus_message_append(m, "susss", appName.c_str(), n.replaces, n.appIcon.c_str(), n.summary.c_str(), n.body.c_str());

    // Actions: id, label, id, label...
    sd_bus_message_open_container(m, 'a', "s");

    for (const auto &[id, label] : n.actions)
        sd_bus_message_append(m, "ss", id.c_str(), label.c_str());

    sd_bus_message_close_container(m);

    // Hints
    sd_bus_message_open_container(m, 'a', "{sv}");
    AppendHint(m, "urgency", "y", UInt8(n.urgency));

    if (!n.desktopEntry.empty())
        AppendHint(m, "desktop-entry", "s", n.desktopEntry.c_str());

    if (n.image.valid())
    {
        // (iiibiiay): width, height, rowstride, has alpha, bits per sample, channels, the bytes
        sd_bus_message_open_container(m, 'e', "sv");
        sd_bus_message_append(m, "s", "image-data");
        sd_bus_message_open_container(m, 'v', "(iiibiiay)");
        sd_bus_message_open_container(m, 'r', "iiibiiay");
        sd_bus_message_append(m, "iiibii", n.image.width, n.image.height, n.image.width * 4, 1, 8, 4);
        sd_bus_message_append_array(m, 'y', n.image.rgba.data(), n.image.rgba.size());
        sd_bus_message_close_container(m);
        sd_bus_message_close_container(m);
        sd_bus_message_close_container(m);
    }
    else if (!n.imagePath.empty())
        AppendHint(m, "image-path", "s", n.imagePath.c_str());

    if (n.transient)
        AppendHint(m, "transient", "b", 1);

    if (n.resident)
        AppendHint(m, "resident", "b", 1);

    sd_bus_message_close_container(m);
    sd_bus_message_append(m, "i", n.timeoutMs);

    auto pending { std::make_unique<Pending>() };
    pending->self = this;
    pending->key = m_nextKey++;
    pending->sent = std::move(sent);
    Pending *p { pending.get() };

    const int ret { sd_bus_call_async(m_bus->bus(), &p->slot, m,
        [](sd_bus_message *reply, void *data, sd_bus_error *) -> int {
            auto *p { static_cast<Pending*>(data) };
            SNotificationsClient *self { p->self };
            UInt32 id { 0 };

            if (sd_bus_message_is_method_error(reply, nullptr))
                SLog(CZWarning, CZLN, "Notification not sent: {}", sd_bus_message_get_error(reply)->message ? sd_bus_message_get_error(reply)->message : "");
            else
                sd_bus_message_read(reply, "u", &id);

            if (id)
                self->m_sent.insert(id);

            auto sent { std::move(p->sent) };
            self->m_pending.erase(p->key); // Releases the slot (sd-bus keeps it while this runs)

            if (sent)
                sent(id);

            return 0;
        }, p, 0) };

    sd_bus_message_unref(m);

    if (ret < 0)
    {
        SLog(CZWarning, CZLN, "Notification not sent: {}", strerror(-ret));

        if (pending->sent)
            pending->sent(0);

        return;
    }

    m_pending.emplace(p->key, std::move(pending));
}

void SNotificationsClient::close(UInt32 id) noexcept
{
    if (!m_bus || id == 0)
        return;

    sd_bus_call_method_async(m_bus->bus(), nullptr, SNotification::FreedesktopName, SNotification::FreedesktopPath, SNotification::FreedesktopName,
        "CloseNotification", nullptr, nullptr, "u", id);
}

/* ------------------------------------------ Gazette ------------------------------------------ */

void SNotificationsClient::fetchState() noexcept
{
    if (!m_bus)
        return;

    sd_bus_slot_unref(m_fetchSlot); // A newer one replaces it
    m_fetchSlot = nullptr;

    sd_bus_call_method_async(m_bus->bus(), &m_fetchSlot, SNotification::CenterName, SNotification::CenterPath, SNotification::CenterName, "GetState",
        [](sd_bus_message *m, void *data, sd_bus_error *) -> int {
            auto *self { static_cast<SNotificationsClient*>(data) };
            int shown { 0 }, dnd { 0 };
            UInt32 count { 0 };

            // Not running
            if (sd_bus_message_is_method_error(m, nullptr) || sd_bus_message_read(m, "bbu", &shown, &dnd, &count) < 0)
                self->setState(false, false, false, 0);
            else
                self->setState(true, shown, dnd, count);

            return 0;
        }, this, "");
}

void SNotificationsClient::setState(bool available, bool centerShown, bool dnd, UInt32 count) noexcept
{
    if (m_available == available && m_centerShown == centerShown && m_dnd == dnd && m_count == count)
        return;

    m_available = available;
    m_centerShown = centerShown;
    m_dnd = dnd;
    m_count = count;
    onChanged.notify();
}

void SNotificationsClient::toggleCenter(const std::string &screen) noexcept
{
    if (m_bus)
        sd_bus_call_method_async(m_bus->bus(), nullptr, SNotification::CenterName, SNotification::CenterPath, SNotification::CenterName, "ToggleCenter",
            nullptr, nullptr, "s", screen.c_str());
}

void SNotificationsClient::showCenter(const std::string &screen) noexcept
{
    if (m_bus)
        sd_bus_call_method_async(m_bus->bus(), nullptr, SNotification::CenterName, SNotification::CenterPath, SNotification::CenterName, "ShowCenter",
            nullptr, nullptr, "s", screen.c_str());
}

void SNotificationsClient::hideCenter() noexcept
{
    if (m_bus)
        sd_bus_call_method_async(m_bus->bus(), nullptr, SNotification::CenterName, SNotification::CenterPath, SNotification::CenterName, "HideCenter",
            nullptr, nullptr, "");
}

void SNotificationsClient::setDoNotDisturb(bool enabled) noexcept
{
    if (m_bus)
        sd_bus_call_method_async(m_bus->bus(), nullptr, SNotification::CenterName, SNotification::CenterPath, SNotification::CenterName, "SetDoNotDisturb",
            nullptr, nullptr, "b", int(enabled));
}
