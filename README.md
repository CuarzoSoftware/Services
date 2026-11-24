# Cuarzo Services

Clients of the system and session services a desktop shell needs, shared by every
[Cuarzo](https://github.com/CuarzoSoftware/Cuarzo) app (the bar's control panel, Settings, ...).

Each service is a class of its own: it talks to the service over D-Bus off the UI thread and
tells when its state changed with signals.

| Class | Service |
|-------|---------|
| `CZ::SWiFi` | Wi-Fi (NetworkManager): turning it on/off, the networks in range, joining and forgetting them |

## Building

Built as part of the [Cuarzo Framework](https://github.com/CuarzoSoftware/Cuarzo). Depends on
`cz-core` and `libsystemd` (or `libelogind`/`basu`).

```meson
cz_services_dep = dependency('cz-services')
```

The log level is set with `CZ_SERVICES_LOG_LEVEL`.
