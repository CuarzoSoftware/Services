#ifndef CZ_SAUDIO_H
#define CZ_SAUDIO_H

#include <CZ/Services/Services.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZBusWorker.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Core/CZEventSource.h>
#include <memory>
#include <string>
#include <vector>
#include <sys/types.h>

/// An audio output (sink) or input (source).
struct CZ::SAudioDevice
{
    std::string name;        ///< PipeWire/PulseAudio name (what pactl takes)
    std::string displayName; ///< Shown: e.g. "Speaker" for built-in ones, the product otherwise
    std::string type;        ///< "Built-in", "USB", "Bluetooth", "HDMI" or "Virtual"
    float volume { 0.f };    ///< 0..1 (average of its channels; can be more if boosted elsewhere)
    bool muted { false };
    std::vector<std::string> channels; ///< Its channel map (e.g. front-left, front-right, aux0...)

    /// @cond INTERNAL
    std::string remapMaster;            // One of ours (see SAudio::select()): the device it routes to
    std::vector<CZ::UInt32> remapChannels;  // and which of its channels
    /// @endcond
};

/// The default output (or input): a device, optionally just some of its channels.
struct CZ::SAudioSelection
{
    std::string device;                 ///< Its name
    std::vector<CZ::UInt32> channels;   ///< 0-based, empty: all of them
    float volume { -1.f };              ///< 0..1, -1 if none
    bool muted { false };
    bool valid { false };               ///< There's a default one
    std::string node;                   ///< The default node: the device, or the one routing to its channels
};

/// The audio devices and the default ones.
struct CZ::SAudioState
{
    bool loaded { false };              ///< Read at least once
    bool available { false };           ///< pactl works (PipeWire or PulseAudio running)
    std::vector<SAudioDevice> outputs, inputs; ///< The devices (not the ones routing to channels of them)
    SAudioSelection output, input;      ///< The default ones

    const SAudioDevice *find(bool input, const std::string &name) const noexcept;

    /// "Channels 1+2", "Channel 3"... (empty for all of them).
    static std::string ChannelsName(bool input, const std::vector<CZ::UInt32> &channels) noexcept;
};

/**
 * @brief The audio devices (through `pactl`, so PipeWire or PulseAudio): choosing the default output
 *        and input (and which of their channels), their volume, the input level and playing sounds.
 *
 * Commands run off the UI thread; onChanged tells when state() changed (watched with
 * `pactl subscribe`, so changes from anywhere show up).
 *
 * Some channels of a device (e.g. outputs 3+4 of a multichannel interface, or one input of it) are
 * used through a virtual device routing to them (module-remap-sink/source), made the default one.
 * There's one per direction, shared by every app: the state shows the device and the channels, not
 * the virtual device. The channels chosen are remembered (`$XDG_CONFIG_HOME/cz-services/audio-channels`)
 * and restoreChannels() recreates them in a new session.
 */
class CZ::SAudio final : public CZ::CZObject
{
public:
    SAudio() noexcept;
    ~SAudio() noexcept;

    const SAudioState &state() const noexcept { return m_state; }

    /// Makes @p device the default output (or input), only @p channels of it if not empty.
    void select(bool input, const std::string &device, std::vector<CZ::UInt32> channels = {}) noexcept;

    /**
     * @brief Once loaded, selects again the channels chosen last time if they aren't (e.g. a new
     *        session: the virtual device is gone) and their device is there.
     *
     * Called by the session's shell when it starts, not by every app.
     */
    void restoreChannels() noexcept;

    /// The default output's (input's) volume, muted at 0 and unmuted above. Coalesced.
    void setVolume(bool input, float volume) noexcept;

    /// Mutes or unmutes the default output (input).
    void setMuted(bool input, bool muted) noexcept;

    /**
     * @brief Measures the level of the node @p name (onInputLevel) until stopInputLevel().
     *
     * Records from it (like any app doing so, it shows as in use meanwhile).
     */
    void startInputLevel(const std::string &name) noexcept;
    void stopInputLevel() noexcept;

    /// Plays the sound file @p path (`pw-play`) through @p device (empty: the default output) at @p volume.
    void play(const std::string &path, const std::string &device, float volume) noexcept;

    CZ::CZSignal<> onChanged;
    CZ::CZSignal<float> onInputLevel; ///< 0..1, a few times per second while measuring

private:
    void refresh() noexcept;
    void restoreSaved() noexcept;
    void save() noexcept;
    void applyVolume() noexcept;
    void startMonitor() noexcept;
    void stopMonitor() noexcept;
    void handleLevelData(int fd) noexcept;

    struct Saved { std::string device; std::vector<CZ::UInt32> channels; };
    Saved m_savedOutput, m_savedInput;
    bool m_restore { false };

    CZ::CZBusWorker m_worker { CZ::CZBusWorker::User };
    SAudioState m_state;
    bool m_busy { false }, m_again { false };
    std::unique_ptr<CZ::CZTimer> m_refreshTimer, m_retryTimer;

    // Changes waiting to be applied to the default output and input (-1: none)
    float m_pendingVolume[2] { -1.f, -1.f };
    int m_pendingMute[2] { -1, -1 };
    bool m_volumeBusy { false };

    // pactl subscribe
    pid_t m_monitorPid { -1 };
    std::shared_ptr<CZ::CZEventSource> m_monitorSource;
    std::string m_monitorBuffer;

    // parec (input level)
    pid_t m_levelPid { -1 };
    std::shared_ptr<CZ::CZEventSource> m_levelSource;
    float m_level { 0.f };
};

#endif // CZ_SAUDIO_H
