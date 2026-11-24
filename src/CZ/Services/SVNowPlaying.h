#ifndef CZ_SVNOWPLAYING_H
#define CZ_SVNOWPLAYING_H

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
 *
 * Shared by the process: see GetOrMake().
 */
class CZ::SVNowPlaying final : public CZ::CZObject
{
public:
    /// The instance, made if there's none (it lives while some shared pointer to it does).
    static std::shared_ptr<SVNowPlaying> GetOrMake() noexcept;

    /// The instance, nullptr if there's none.
    static std::shared_ptr<SVNowPlaying> Get() noexcept;

    ~SVNowPlaying() noexcept;

    /// The player shown and what it plays.
    struct State
    {
        bool available { false };   ///< Some player is there
        std::string player;         ///< Its bus name
        std::string identity;       ///< e.g. "Spotify"
        std::string title;          ///< The track's title
        std::string artist;         ///< Its artists (comma separated)
        std::string artwork;        ///< Local file (empty: none or not downloaded yet)
        bool playing { false };     ///< Playing (not paused or stopped)
        bool canPrevious { false }; ///< previous() works
        bool canNext { false };     ///< next() works
        bool canPlayPause { false };///< playPause() works
        bool canRaise { false };    ///< raise() works
    };

    /// The current state (see onChanged).
    const State &state() const noexcept { return m_state; }

    /// Plays or pauses (shown in state() right away).
    void playPause() noexcept;

    /// The previous track.
    void previous() noexcept;

    /// The next track.
    void next() noexcept;

    /// Brings the player's window up.
    void raise() noexcept;

    /// state() changed (a player appeared or went away, the track, playing, the artwork downloaded...).
    CZ::CZSignal<> onChanged;

private:
    SVNowPlaying() noexcept;
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

#endif // CZ_SVNOWPLAYING_H
