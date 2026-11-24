#include <CZ/Services/SLog.h>

using namespace CZ;

const CZ::CZLogger &SLogger() noexcept
{
    static CZLogger logger { "Services", "CZ_SERVICES_LOG_LEVEL" };
    return logger;
}
