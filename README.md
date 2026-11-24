# Services

<p align="left">
  <a href="https://github.com/CuarzoSoftware/Services/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-LGPLv2.1-blue.svg" alt="Services is released under the LGPLv2.1 license." />
  </a>
  <a href="https://github.com/CuarzoSoftware/Services">
    <img src="https://img.shields.io/badge/version-1.0.0-brightgreen" alt="Current Services version." />
  </a>
</p>

> This project is part of the [Cuarzo Framework](https://github.com/CuarzoSoftware/Cuarzo). Follow the instructions there to build and install it.

Clients of the system and session services a desktop shell needs, shared by every Cuarzo app
(the bar's control panel, Settings, ...).

Each service is a class of its own: it talks to the service over D-Bus (or its command line tools)
off the UI thread and tells when its state changed with signals. Like `CZCore`, each one is shared by
the process: `GetOrMake()` returns the instance (made on first use), which lives while some shared
pointer to it does.

```cpp
auto audio { CZ::SAudio::GetOrMake() };
audio->onChanged.subscribe(this, [audio]{ /* audio->state() */ });
```

| Class | Service |
|-------|---------|
| `CZ::SWiFi` | Wi-Fi (NetworkManager): turning it on/off, the networks in range, joining and forgetting them |
| `CZ::SAudio` | Audio (PipeWire or PulseAudio, through `pactl`): the default output and input (or some of their channels), their volume, the input level, playing sounds |
| `CZ::SStatus` | The status for a bar's indicators: network (NetworkManager), volume (from `SAudio`) and battery (UPower), as symbolic icon names |
| `CZ::SNowPlaying` | What's playing (MPRIS media players): title, artist, artwork, play/pause, previous/next |
| `CZ::SNotificationsClient` | Desktop notifications from an app: sending them (to any daemon), their actions; controlling [Gazette](https://github.com/CuarzoSoftware/Gazette)'s notification center and Do Not Disturb |
| `CZ::SNotificationsServer` | The protocol side of a notifications daemon (Gazette uses it): reads what's sent into an `SNotification`, emits its signals, serves the notification center's interface |

## Dependencies

`cz-core` and `libsystemd` (or `libelogind`/`basu`). At runtime: `pactl`, `parec` and `pw-play`
(SAudio) and `curl` (SNowPlaying, for artwork given as URLs).

```meson
cz_services_dep = dependency('cz-services')
```

The log level is set with `CZ_SERVICES_LOG_LEVEL`.
