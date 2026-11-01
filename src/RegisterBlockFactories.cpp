#include <PySysLinkBase/IBlockFactory.h>

#include "BlockFactoryNgspice.h"
#include "LoggerInstance.h"
#include "spdlog/spdlog.h"

extern "C" void RegisterBlockFactories(
    std::map<std::string, std::shared_ptr<PySysLinkBase::IBlockFactory>>& registry,
    std::map<std::string, PySysLinkBase::ConfigurationValue>)
{
    registry["Ngspice"] =
        std::make_shared<BlockTypeSupports::NgspiceSupport::BlockFactoryNgspice>(
            std::map<std::string, PySysLinkBase::ConfigurationValue>{});
}


extern "C" void RegisterSpdlogLogger(std::shared_ptr<spdlog::logger> logger) {
    BlockTypeSupports::NgspiceSupport::LoggerInstance::SetLogger(logger);
    BlockTypeSupports::NgspiceSupport::LoggerInstance::GetLogger()->debug("Logger from plugin BlockTypeSupportsNgspice!");
}
