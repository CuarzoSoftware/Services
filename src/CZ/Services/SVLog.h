#ifndef CZ_SVLOG_H
#define CZ_SVLOG_H

#include <CZ/Core/CZLogger.h>

/// Logs with cz-services' logger, e.g. `SVLog(CZWarning, CZLN, "Failed: {}", error)`.
#define SVLog SVLogger()

/// The logger of cz-services (level set with CZ_SERVICES_LOG_LEVEL).
const CZ::CZLogger &SVLogger() noexcept;

#endif // CZ_SVLOG_H
