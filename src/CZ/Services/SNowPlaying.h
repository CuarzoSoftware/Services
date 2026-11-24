#ifndef CZ_SNOWPLAYING_H
#define CZ_SNOWPLAYING_H

#include <CZ/Services/Services.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZBusWorker.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Core/CZBus.h>
#include <memory>
#include <string>

struct sd_bus_slot;

/**
 * @brief What's playing (MPRIS media players: Spotify, browsers, video players...), e.g. for a
 *        Now Playing card.
 *
 * The player shown is the one playing, else the last one that did (or any). Its artwork is a local
 * file or downloaded once to `$XDG_CACHE_HOME/cz-services/artwork/` (players like Spotify give URLs).
 * Kept up to date from the players' D-Bus signals; reads and commands run off the UI thread.
 */
class CZ::SNowPlaying final : public CZ::CZObject
{
public:
    SNowPlaying() noexcept;
    ~SNowPlaying() noexcept;

    struct State
    {
        bool available { false };   ///< Some player is there
        std::string player;         ///< Its bus name
        std::string identity;       ///< e.g. "Spotify"
        std::string title, artist;
        std::string artwork;        ///< Local file (empty: none or not downloaded yet)
        bool playing { false };
        bool canPrevious { false }, canNext { false }, canPlayPause { false }, canRaise { false };
    };

    const State &state() const noexcept { return m_state; }

    void playPause() noexcept;
    void previous() noexcept;
    void next() noexcept;

    /// Brings the player's window up.
    void raise() noexcept;

    CZ::CZSignal<> onChanged;

private:
    void refresh() noexcept;
    void call(const char *method, bool root = false) noexcept;

    CZ::CZBusWorker m_worker { CZ::CZBusWorker::User, 1000 };
    std::shared_ptr<CZ::CZBus> m_bus;
    sd_bus_slot *m_propertiesMatch {}, *m_ownerMatch {};
    std::unique_ptr<CZ::CZTimer> m_refreshTimer;
    State m_state;
    std::string m_lastPlaying; // The player that played last
    bool m_busy { false }, m_again { false };
};

#endif // CZ_SNOWPLAYING_H
