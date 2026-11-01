#ifndef BLOCK_TYPE_SUPPORTS_NGSPICE_SIMULATION_BLOCK_H
#define BLOCK_TYPE_SUPPORTS_NGSPICE_SIMULATION_BLOCK_H

#include <PySysLinkBase/ConfigurationValue.h>
#include <PySysLinkBase/IBlockEventsHandler.h>
#include <PySysLinkBase/ISimulationBlock.h>
#include <PySysLinkBase/PortsAndSignalValues/InputPort.h>
#include <PySysLinkBase/PortsAndSignalValues/OutputPort.h>
#include <PySysLinkBase/PortsAndSignalValues/SignalValue.h>
#include <PySysLinkBase/SampleTime.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "NgspiceSession.h"

namespace BlockTypeSupports::NgspiceSupport {

class SimulationBlockNgspice : public PySysLinkBase::ISimulationBlock {
public:
    SimulationBlockNgspice(
        std::map<std::string, PySysLinkBase::ConfigurationValue> configuration,
        std::shared_ptr<PySysLinkBase::IBlockEventsHandler> eventHandler)
        : ISimulationBlock(configuration, std::move(eventHandler)),
          inputSources_(getNames(configuration, "InputSources")),
          outputVectors_(getNames(configuration, "OutputVectors"))
    {
        for (std::size_t index = 0; index < inputSources_.size(); ++index) {
            inputPorts_.push_back(std::make_shared<PySysLinkBase::InputPort>(
                false,
                std::make_shared<PySysLinkBase::SignalValue<double>>(0.0)));
        }
        for (std::size_t index = 0; index < outputVectors_.size(); ++index) {
            outputPorts_.push_back(std::make_shared<PySysLinkBase::OutputPort>(
                std::make_shared<PySysLinkBase::SignalValue<double>>(0.0)));
        }

        const auto netlistPath =
            PySysLinkBase::ConfigurationValueManager::TryGetConfigurationValue<
                std::string>("NetlistPath", configuration);
        session_ = std::make_unique<NgspiceSession>(
            netlistPath, inputSources_, outputVectors_);

        const double sampleTime =
            PySysLinkBase::ConfigurationValueManager::TryGetConfigurationValue<
                double>("SampleTime", configuration);
        if (!std::isfinite(sampleTime) ||
            (sampleTime <= 0.0 && sampleTime != -1.0)) {
            throw std::invalid_argument(
                "SampleTime must be greater than 0 or equal to -1 for inherited "
                "sample time");
        }
        if (sampleTime == -1.0) {
            sampleTime_ = std::make_shared<PySysLinkBase::SampleTime>(
                PySysLinkBase::SampleTimeType::inherited,
                std::vector<PySysLinkBase::SampleTimeType>{
                    PySysLinkBase::SampleTimeType::discrete});
        } else {
            sampleTime_ = std::make_shared<PySysLinkBase::SampleTime>(
                PySysLinkBase::SampleTimeType::discrete, sampleTime);
        }
    }

    const std::shared_ptr<PySysLinkBase::SampleTime> GetSampleTime() const override
    {
        return sampleTime_;
    }

    void SetSampleTime(
        std::shared_ptr<PySysLinkBase::SampleTime> sampleTime) override
    {
        sampleTime_ = std::move(sampleTime);
    }

    std::vector<std::shared_ptr<PySysLinkBase::InputPort>> GetInputPorts() const override
    {
        return inputPorts_;
    }

    const std::vector<std::shared_ptr<PySysLinkBase::OutputPort>> GetOutputPorts()
        const override
    {
        return outputPorts_;
    }

    const std::vector<std::shared_ptr<PySysLinkBase::OutputPort>>
    _ComputeOutputsOfBlock(
        const std::shared_ptr<PySysLinkBase::SampleTime> sampleTime,
        double currentTime,
        bool = false) override
    {
        if (sampleTime == nullptr ||
            sampleTime->GetSampleTimeType() !=
                PySysLinkBase::SampleTimeType::discrete) {
            throw std::invalid_argument(
                "Ngspice block requires a discrete sample time");
        }
        const double stepSize = sampleTime->GetDiscreteSampleTime();
        if (!std::isfinite(currentTime) || !std::isfinite(stepSize) ||
            stepSize <= 0.0) {
            throw std::invalid_argument(
                "Ngspice block requires finite time and a positive sample time");
        }

        if (hasStepped_) {
            const double tolerance =
                std::max(1e-15, 1e-9 * std::max(std::abs(currentTime), stepSize));
            if (std::abs(currentTime - lastStepStartTime_) <= tolerance) {
                return outputPorts_;
            }
            if (std::abs(currentTime - lastStepEndTime_) > tolerance) {
                throw std::runtime_error(
                    "Ngspice block does not support time rollback or skipped "
                    "discrete steps");
            }
        }

        std::vector<double> inputValues;
        inputValues.reserve(inputPorts_.size());
        for (const auto& inputPort : inputPorts_) {
            inputValues.push_back(
                inputPort->GetValue()->TryCastToTyped<double>()->GetPayload());
        }

        const auto outputValues =
            session_->advance(currentTime, stepSize, inputValues);
        for (std::size_t index = 0; index < outputPorts_.size(); ++index) {
            auto value =
                outputPorts_[index]->GetValue()->TryCastToTyped<double>();
            value->SetPayload(outputValues[index]);
            outputPorts_[index]->SetValue(
                std::make_shared<PySysLinkBase::SignalValue<double>>(*value));
        }

        lastStepStartTime_ = currentTime;
        lastStepEndTime_ = currentTime + stepSize;
        hasStepped_ = true;
        return outputPorts_;
    }

    bool _TryUpdateConfigurationValue(
        std::string, PySysLinkBase::ConfigurationValue) override
    {
        return false;
    }

private:
    static std::vector<std::string> getNames(
        const std::map<std::string, PySysLinkBase::ConfigurationValue>& configuration,
        const std::string& key)
    {
        if (configuration.find(key) == configuration.end()) {
            return {};
        }
        return PySysLinkBase::ConfigurationValueManager::
            TryGetConfigurationValue<std::vector<std::string>>(key, configuration);
    }

    std::vector<std::string> inputSources_;
    std::vector<std::string> outputVectors_;
    std::vector<std::shared_ptr<PySysLinkBase::InputPort>> inputPorts_;
    std::vector<std::shared_ptr<PySysLinkBase::OutputPort>> outputPorts_;
    std::unique_ptr<NgspiceSession> session_;
    std::shared_ptr<PySysLinkBase::SampleTime> sampleTime_;
    double lastStepStartTime_ = 0.0;
    double lastStepEndTime_ = 0.0;
    bool hasStepped_ = false;
};

}  // namespace BlockTypeSupports::NgspiceSupport

#endif
