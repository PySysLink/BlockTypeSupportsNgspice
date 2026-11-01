#include "LoggerInstance.h"

namespace BlockTypeSupports::NgspiceSupport
{
    std::shared_ptr<spdlog::logger> LoggerInstance::s_logger = nullptr;
} // namespace BlockTypeSupports::NgspiceSupport
