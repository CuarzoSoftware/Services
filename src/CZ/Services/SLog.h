#ifndef CZ_SLOG_H
#define CZ_SLOG_H

#include <CZ/Core/CZLogger.h>

#define SLog SLogger()

/// The logger of cz-services (level set with CZ_SERVICES_LOG_LEVEL).
const CZ::CZLogger &SLogger() noexcept;

#endif // CZ_SLOG_H
