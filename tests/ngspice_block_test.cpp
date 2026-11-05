#include "NgspiceApi.h"
#include "NgspiceSession.h"
#include "SimulationBlockNgspice.h"

#include <unistd.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class TemporaryLibraryCopy {
public:
    TemporaryLibraryCopy(const std::filesystem::path& source, int index)
    {
        directory_ = std::filesystem::temp_directory_path() /
            ("pysyslink-ngspice-" + std::to_string(::getpid()) + "-" +
             std::to_string(nextId_++) + "-" + std::to_string(index));
        std::filesystem::create_directories(directory_);
        libraryPath_ = directory_ /
            ("libngspice_" + std::to_string(::getpid()) + "_" +
             std::to_string(index) + ".so");
        std::filesystem::copy_file(source, libraryPath_);
    }

    ~TemporaryLibraryCopy()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    TemporaryLibraryCopy(const TemporaryLibraryCopy&) = delete;
    TemporaryLibraryCopy& operator=(const TemporaryLibraryCopy&) = delete;

    const std::string path() const
    {
        return libraryPath_.string();
    }

private:
    inline static std::atomic<unsigned int> nextId_{0};
    std::filesystem::path directory_;
    std::filesystem::path libraryPath_;
};

double outputValue(
    const std::vector<std::shared_ptr<PySysLinkBase::OutputPort>>& outputs,
    std::size_t index)
{
    return outputs.at(index)
        ->GetValue()
        ->TryCastToTyped<double>()
        ->GetPayload();
}

std::map<std::string, PySysLinkBase::ConfigurationValue> configuration(
    const std::string& libraryPath,
    const std::string& netlistPath,
    const std::string& id)
{
    return {
        {"Id", id},
        {"Name", id},
        {"InputPortNumber", 1},
        {"OutputPortNumber", 2},
        {"InputPortTypes",
         std::vector<std::string>{"FullySupportedSignalValue.double"}},
        {"OutputPortTypes", std::vector<std::string>{
            "FullySupportedSignalValue.double",
            "FullySupportedSignalValue.double"}},
        {"NetlistPath", netlistPath},
        {"NgspiceLibraryPath", libraryPath},
        {"InputSources", std::vector<std::string>{"Vdrive"}},
        {"OutputVectors",
         std::vector<std::string>{"v(out)", "i(Vdrive)"}},
        {"SampleTime", 0.001},
    };
}

void setInput(
    const std::shared_ptr<PySysLinkBase::InputPort>& input, double value)
{
    input->SetValue(
        std::make_shared<PySysLinkBase::SignalValue<double>>(value));
}

void testLibraryLoadErrors(const std::string& missingSymbolsLibrary)
{
    try {
        BlockTypeSupports::NgspiceSupport::NgspiceApi api(
            "/path/that/does/not/exist/libngspice.so");
        throw std::runtime_error("A nonexistent ngspice library was loaded");
    } catch (const std::runtime_error& exception) {
        if (std::string(exception.what()).find(
                "/path/that/does/not/exist/libngspice.so") ==
            std::string::npos) {
            throw;
        }
    }

    try {
        BlockTypeSupports::NgspiceSupport::NgspiceApi api(
            missingSymbolsLibrary);
        throw std::runtime_error("A library without ngspice symbols was loaded");
    } catch (const std::runtime_error& exception) {
        if (std::string(exception.what()).find("ngSpice_Init") ==
            std::string::npos) {
            throw;
        }
    }
}

void testTwoIndependentBlocks(
    const std::string& libraryPath,
    const std::string& netlist,
    const std::string& slowerNetlist)
{
    TemporaryLibraryCopy firstLibrary(libraryPath, 0);
    TemporaryLibraryCopy secondLibrary(libraryPath, 1);
    if (std::filesystem::equivalent(firstLibrary.path(), secondLibrary.path())) {
        throw std::runtime_error(
            "Test library copies unexpectedly refer to the same file");
    }

    BlockTypeSupports::NgspiceSupport::SimulationBlockNgspice first(
        configuration(firstLibrary.path(), netlist, "first"),
        nullptr);
    BlockTypeSupports::NgspiceSupport::SimulationBlockNgspice second(
        configuration(secondLibrary.path(), slowerNetlist, "second"),
        nullptr);

    const auto sampleTime = first.GetSampleTime();
    const auto firstInput = first.GetInputPorts().at(0);
    const auto secondInput = second.GetInputPorts().at(0);
    setInput(firstInput, 0.0);
    setInput(secondInput, 0.0);
    first._ComputeOutputsOfBlock(sampleTime, 0.0);
    second._ComputeOutputsOfBlock(sampleTime, 0.0);

    setInput(firstInput, 1.0);
    setInput(secondInput, 1.0);
    const auto firstOutput =
        first._ComputeOutputsOfBlock(sampleTime, 0.001);
    const auto secondOutput =
        second._ComputeOutputsOfBlock(sampleTime, 0.001);
    const double firstVoltage = outputValue(firstOutput, 0);
    const double secondVoltage = outputValue(secondOutput, 0);
    if (!(firstVoltage > 0.3 && firstVoltage < 0.9) ||
        !(secondVoltage > 0.2 && secondVoltage < 0.7) ||
        std::abs(firstVoltage - secondVoltage) < 0.1 ||
        !std::isfinite(outputValue(firstOutput, 1)) ||
        !std::isfinite(outputValue(secondOutput, 1))) {
        throw std::runtime_error(
            "Independent ngspice blocks returned incorrect or coupled outputs");
    }
}

void testIndependentReset(
    const std::string& libraryPath,
    const std::string& netlist,
    const std::string& slowerNetlist)
{
    TemporaryLibraryCopy firstLibrary(libraryPath, 2);
    TemporaryLibraryCopy secondLibrary(libraryPath, 3);
    BlockTypeSupports::NgspiceSupport::NgspiceSession first(
        firstLibrary.path(), netlist, {"Vdrive"}, {"v(out)"});
    BlockTypeSupports::NgspiceSupport::NgspiceSession second(
        secondLibrary.path(), slowerNetlist, {"Vdrive"}, {"v(out)"});

    const auto firstOutput = first.advance(0.0, 0.001, {0.0});
    const auto secondOutput = second.advance(0.0, 0.001, {0.0});
    if (std::abs(firstOutput.at(0)) > 1e-8 ||
        std::abs(secondOutput.at(0)) > 1e-8) {
        throw std::runtime_error("Unexpected outputs before independent reset");
    }

    first.reset();
    const auto secondContinued = second.advance(0.001, 0.001, {1.0});
    const auto firstRestarted = first.advance(0.0, 0.001, {1.0});
    if (!(secondContinued.at(0) > 0.2 && secondContinued.at(0) < 0.7) ||
        !(firstRestarted.at(0) > 0.3 && firstRestarted.at(0) < 0.7)) {
        throw std::runtime_error(
            "Resetting one ngspice instance affected another instance");
    }
}

void testIndependentDestruction(
    const std::string& libraryPath,
    const std::string& netlist,
    const std::string& slowerNetlist)
{
    TemporaryLibraryCopy firstLibrary(libraryPath, 4);
    TemporaryLibraryCopy secondLibrary(libraryPath, 5);
    BlockTypeSupports::NgspiceSupport::SimulationBlockNgspice second(
        configuration(secondLibrary.path(), slowerNetlist, "survivor"),
        nullptr);
    auto sampleTime = second.GetSampleTime();
    setInput(second.GetInputPorts().at(0), 0.0);
    second._ComputeOutputsOfBlock(sampleTime, 0.0);

    {
        BlockTypeSupports::NgspiceSupport::SimulationBlockNgspice first(
            configuration(firstLibrary.path(), netlist, "destroyed"),
            nullptr);
        setInput(first.GetInputPorts().at(0), 0.0);
        first._ComputeOutputsOfBlock(sampleTime, 0.0);
    }

    setInput(second.GetInputPorts().at(0), 1.0);
    const auto outputs = second._ComputeOutputsOfBlock(sampleTime, 0.001);
    if (!(outputValue(outputs, 0) > 0.2 && outputValue(outputs, 0) < 0.7)) {
        throw std::runtime_error(
            "Destroying one ngspice instance affected another instance");
    }
}

void testStepLimitAndInputChanges(
    const std::string& libraryPath, const std::string& netlist)
{
    TemporaryLibraryCopy library(libraryPath, 6);
    BlockTypeSupports::NgspiceSupport::SimulationBlockNgspice block(
        configuration(library.path(), netlist, "step-limit"), nullptr);
    const auto sampleTime = block.GetSampleTime();
    const auto input = block.GetInputPorts().at(0);

    setInput(input, 0.0);
    const auto first = block._ComputeOutputsOfBlock(sampleTime, 0.0);
    if (std::abs(outputValue(first, 0)) > 1e-8) {
        throw std::runtime_error("Expected zero initial RC output");
    }

    setInput(input, 1.0);
    const auto second = block._ComputeOutputsOfBlock(sampleTime, 0.001);
    const double response = outputValue(second, 0);
    if (!(response > 0.3 && response < 0.9) ||
        !std::isfinite(outputValue(second, 1))) {
        throw std::runtime_error("Unexpected RC output after input step");
    }

    for (int step = 2; step <= 20; ++step) {
        block._ComputeOutputsOfBlock(
            sampleTime, static_cast<double>(step) * 0.001);
    }

    try {
        block._ComputeOutputsOfBlock(sampleTime, 0.021);
        throw std::runtime_error(
            "Expected the finite .tran stop time to end simulation");
    } catch (const std::runtime_error& exception) {
        if (std::string(exception.what()).find(
                "increase the .tran stop time") == std::string::npos) {
            throw;
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "Expected ngspice library, two netlists, and a library "
                     "without ngspice symbols\n";
        return 2;
    }

    try {
        testLibraryLoadErrors(argv[4]);
        testTwoIndependentBlocks(argv[1], argv[2], argv[3]);
        testIndependentReset(argv[1], argv[2], argv[3]);
        testIndependentDestruction(argv[1], argv[2], argv[3]);
        testStepLimitAndInputChanges(argv[1], argv[2]);
    } catch (const std::exception& exception) {
        std::cerr << "Ngspice integration test failed: " << exception.what()
                  << '\n';
        return 1;
    }
    return 0;
}
