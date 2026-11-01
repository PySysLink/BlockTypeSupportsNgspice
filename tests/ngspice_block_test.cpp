#include "SimulationBlockNgspice.h"

#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace {

double outputValue(
    const std::vector<std::shared_ptr<PySysLinkBase::OutputPort>>& outputs,
    std::size_t index)
{
    return outputs.at(index)
        ->GetValue()
        ->TryCastToTyped<double>()
        ->GetPayload();
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Expected the test netlist path\n";
        return 2;
    }

    std::map<std::string, PySysLinkBase::ConfigurationValue> configuration{
        {"Id", std::string("ngspice-test")},
        {"Name", std::string("ngspice-test")},
        {"InputPortNumber", 1},
        {"OutputPortNumber", 2},
        {"InputPortTypes",
         std::vector<std::string>{"FullySupportedSignalValue.double"}},
        {"OutputPortTypes", std::vector<std::string>{
            "FullySupportedSignalValue.double",
            "FullySupportedSignalValue.double"}},
        {"NetlistPath", std::string(argv[1])},
        {"InputSources", std::vector<std::string>{"Vdrive"}},
        {"OutputVectors",
         std::vector<std::string>{"v(out)", "i(Vdrive)"}},
        {"SampleTime", 0.001},
    };

    try {
        BlockTypeSupports::NgspiceSupport::SimulationBlockNgspice block(
            std::move(configuration), nullptr);
        auto sampleTime = block.GetSampleTime();
        auto input = block.GetInputPorts().at(0);

        input->SetValue(
            std::make_shared<PySysLinkBase::SignalValue<double>>(0.0));
        const auto first = block._ComputeOutputsOfBlock(sampleTime, 0.0);
        if (std::abs(outputValue(first, 0)) > 1e-8) {
            std::cerr << "Expected zero output before the input step\n";
            return 1;
        }

        input->SetValue(
            std::make_shared<PySysLinkBase::SignalValue<double>>(1.0));
        const auto second = block._ComputeOutputsOfBlock(sampleTime, 0.001);
        const double response = outputValue(second, 0);
        if (!(response > 0.3 && response < 0.9)) {
            std::cerr << "Unexpected RC output after one discrete step: "
                      << response << '\n';
            return 1;
        }
        if (!std::isfinite(outputValue(second, 1))) {
            std::cerr << "Expected a finite source current output\n";
            return 1;
        }

        for (int step = 2; step <= 20; ++step) {
            block._ComputeOutputsOfBlock(
                sampleTime, static_cast<double>(step) * 0.001);
        }

        try {
            block._ComputeOutputsOfBlock(sampleTime, 0.021);
            std::cerr << "Expected the finite .tran stop time to end simulation\n";
            return 1;
        } catch (const std::runtime_error& exception) {
            if (std::string(exception.what()).find(
                    "increase the .tran stop time") == std::string::npos) {
                throw;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << "Ngspice block test failed: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
