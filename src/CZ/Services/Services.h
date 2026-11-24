#ifndef CZ_SERVICES_H
#define CZ_SERVICES_H

#include <CZ/Core/Cuarzo.h>

/**
 * @brief Clients of the system and session services a desktop shell needs (NetworkManager, PipeWire, UPower, MPRIS, notifications...),
 *        shared by every Cuarzo app.
 *
 * Each service is its own class, independent from the others: it talks to its service over
 * D-Bus off the UI thread (see CZBusWorker) and tells when its state changed with signals.
 */
namespace CZ
{
    struct SVWiFiNetwork;
    struct SVWiFiState;
    class SVWiFi;

    struct SVAudioDevice;
    struct SVAudioSelection;
    struct SVAudioState;
    class SVAudio;

    class SVStatus;
    class SVNowPlaying;

    struct SVNotification;
    class SVNotificationsClient;
    class SVNotificationsServer;
};

#endif // CZ_SERVICES_H
