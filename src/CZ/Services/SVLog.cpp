#include <CZ/Services/SVLog.h>

using namespace CZ;

const CZ::CZLogger &SVLogger() noexcept
{
    static CZLogger logger { "Services", "CZ_SERVICES_LOG_LEVEL" };
    return logger;
}
