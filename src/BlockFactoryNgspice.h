#ifndef BLOCK_TYPE_SUPPORTS_NGSPICE_BLOCK_FACTORY_H
#define BLOCK_TYPE_SUPPORTS_NGSPICE_BLOCK_FACTORY_H

#include <PySysLinkBase/IBlockFactory.h>

#include <memory>
#include <string>
#include <utility>

#include "SimulationBlockNgspice.h"

namespace BlockTypeSupports::NgspiceSupport {

class BlockFactoryNgspice : public PySysLinkBase::IBlockFactory {
public:
    explicit BlockFactoryNgspice(
        const std::map<std::string, PySysLinkBase::ConfigurationValue>&)
    {}

    std::shared_ptr<PySysLinkBase::ISimulationBlock> CreateBlock(
        std::map<std::string, PySysLinkBase::ConfigurationValue> configuration,
        std::shared_ptr<PySysLinkBase::IBlockEventsHandler> eventHandler) override
    {
        return std::make_shared<SimulationBlockNgspice>(
            std::move(configuration), std::move(eventHandler));
    }
};

}  // namespace BlockTypeSupports::NgspiceSupport

#endif
