#ifndef BLOCK_TYPE_SUPPORTS_NGSPICE_SESSION_H
#define BLOCK_TYPE_SUPPORTS_NGSPICE_SESSION_H

#include "NgspiceApi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BlockTypeSupports::NgspiceSupport {

class NgspiceSession {
public:
    NgspiceSession(
        const std::string& netlistPath,
        const std::vector<std::string>& inputSources,
        const std::vector<std::string>& outputVectors)
        : outputVectors_(outputVectors)
    {
        acquireRuntime();
        try {
            configureSources(inputSources);
            configureOutputs();
            const auto path = validateNetlist(netlistPath);

            ngSpice_Reset();
            ngSpice_Init(
                &sendChar, &sendStatus, &controlledExit, &sendData, &sendInitData,
                &backgroundThreadRunning, this);
            ngSpice_Init_Sync(&getVoltageSourceData, &getCurrentSourceData,
                              nullptr, nullptr, this);

            std::string command = "source " + path.string();
            if (ngSpice_Command(command.data()) != 0) {
                throw std::runtime_error(
                    errorWithDiagnostics("ngspice could not load the netlist"));
            }
        } catch (...) {
            ngSpice_Reset();
            releaseRuntime();
            throw;
        }
    }

    ~NgspiceSession()
    {
        stopSimulation();
        ngSpice_Reset();
        releaseRuntime();
    }

    NgspiceSession(const NgspiceSession&) = delete;
    NgspiceSession& operator=(const NgspiceSession&) = delete;

    std::vector<double> advance(
        double startTime,
        double stepSize,
        const std::vector<double>& inputs)
    {
        if (!std::isfinite(startTime) || !std::isfinite(stepSize) ||
            stepSize <= 0.0) {
            throw std::invalid_argument(
                "Ngspice simulation time and step size must be finite, and "
                "the step size must be positive");
        }
        if (inputs.size() != inputSources_.size()) {
            throw std::invalid_argument(
                "Ngspice input value count does not match configured sources");
        }
        for (const double input : inputs) {
            if (!std::isfinite(input)) {
                throw std::invalid_argument(
                    "Ngspice input values must be finite");
            }
        }
        const double targetTime = startTime + stepSize;
        if (!std::isfinite(targetTime)) {
            throw std::invalid_argument("Ngspice target time is not finite");
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (!failure_.empty()) {
            throw std::runtime_error(errorWithDiagnostics(failure_));
        }
        if (analysisComplete_) {
            throw std::runtime_error(errorWithDiagnostics(
                "ngspice transient analysis ended before requested endpoint " +
                std::to_string(targetTime) +
                " s; increase the .tran stop time to cover the PySysLink "
                "simulation"));
        }

        if (!started_) {
            if (std::abs(startTime) > timeTolerance(targetTime, stepSize)) {
                throw std::runtime_error(
                    "Ngspice transient simulation must start at time zero");
            }
            inputs_ = inputs;
            targetTime_ = targetTime;
            boundaryReached_ = false;
            started_ = true;
            backgroundRunning_ = true;
            lock.unlock();

            if (ngSpice_SetBkpt(targetTime) == 0) {
                lock.lock();
                backgroundRunning_ = false;
                failure_ = "ngspice could not set the first transient breakpoint";
                condition_.notify_all();
                throw std::runtime_error(errorWithDiagnostics(failure_));
            }
            std::string command = "bg_run";
            if (ngSpice_Command(command.data()) != 0) {
                lock.lock();
                backgroundRunning_ = false;
                failure_ = "ngspice could not start the transient simulation";
                condition_.notify_all();
                throw std::runtime_error(errorWithDiagnostics(failure_));
            }
            lock.lock();
        } else {
            const double tolerance = timeTolerance(lastTargetTime_, stepSize);
            if (std::abs(startTime - lastTargetTime_) > tolerance) {
                throw std::runtime_error(
                    "Ngspice block calls must be consecutive discrete steps");
            }
            if (!boundaryReached_) {
                throw std::runtime_error(
                    "Ngspice is not waiting at a discrete simulation boundary");
            }
            inputs_ = inputs;
            targetTime_ = targetTime;
            lock.unlock();

            if (ngSpice_SetBkpt(targetTime) == 0) {
                throw std::runtime_error(errorWithDiagnostics(
                    "ngspice could not set the transient breakpoint"));
            }
            lock.lock();
            boundaryReached_ = false;
            continueSimulation_ = true;
            condition_.notify_all();
        }

        condition_.wait(lock, [this] {
            return boundaryReached_ || !backgroundRunning_ || !failure_.empty();
        });

        if (!failure_.empty()) {
            throw std::runtime_error(errorWithDiagnostics(failure_));
        }
        if (!boundaryReached_) {
            throw std::runtime_error(errorWithDiagnostics(
                "ngspice ended before reaching requested endpoint " +
                std::to_string(targetTime) +
                " s; increase the .tran stop time to cover the PySysLink "
                "simulation"));
        }

        lastTargetTime_ = targetTime;
        return latestOutputs_;
    }

private:
    struct InputSource {
        char type;
        std::size_t portIndex;
    };

    static std::string normalize(const std::string& value)
    {
        std::string result = value;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        return result;
    }

    static std::string outputVectorKey(const std::string& vector)
    {
        const std::string normalized = normalize(vector);
        if (normalized.size() > 3 && normalized.rfind("v(", 0) == 0 &&
            normalized.back() == ')') {
            return normalized.substr(2, normalized.size() - 3);
        }
        if (normalized.size() > 3 && normalized.rfind("i(", 0) == 0 &&
            normalized.back() == ')') {
            return normalized.substr(2, normalized.size() - 3) + "#branch";
        }
        return normalized;
    }

    static double timeTolerance(double time, double stepSize)
    {
        return std::max(1e-15, 1e-9 * std::max(std::abs(time), stepSize));
    }

    void acquireRuntime()
    {
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        if (runtimeInUse_) {
            throw std::runtime_error(
                "The ngspice shared library supports one active Ngspice block "
                "per process");
        }
        runtimeInUse_ = true;
        ownsRuntime_ = true;
    }

    void releaseRuntime()
    {
        if (!ownsRuntime_) {
            return;
        }
        std::lock_guard<std::mutex> lock(runtimeMutex_);
        runtimeInUse_ = false;
        ownsRuntime_ = false;
    }

    void configureSources(const std::vector<std::string>& inputSources)
    {
        inputSources_.reserve(inputSources.size());
        for (std::size_t index = 0; index < inputSources.size(); ++index) {
            const auto& name = inputSources[index];
            if (name.empty()) {
                throw std::invalid_argument(
                    "Ngspice input source names must not be empty");
            }
            const char type = static_cast<char>(
                std::tolower(static_cast<unsigned char>(name.front())));
            if (type != 'v' && type != 'i') {
                throw std::invalid_argument(
                    "Ngspice input source names must identify voltage (V...) "
                    "or current (I...) sources: " + name);
            }
            if (!sources_.emplace(normalize(name), InputSource{type, index})
                     .second) {
                throw std::invalid_argument(
                    "Duplicate Ngspice input source: " + name);
            }
            inputSources_.push_back(name);
        }
        inputs_.resize(inputSources_.size(), 0.0);
    }

    void configureOutputs()
    {
        std::unordered_map<std::string, bool> seen;
        latestOutputs_.resize(outputVectors_.size(), 0.0);
        for (const auto& vector : outputVectors_) {
            if (vector.empty()) {
                throw std::invalid_argument(
                    "Ngspice output vector names must not be empty");
            }
            if (!seen.emplace(outputVectorKey(vector), true).second) {
                throw std::invalid_argument(
                    "Duplicate Ngspice output vector: " + vector);
            }
        }
    }

    static std::filesystem::path validateNetlist(const std::string& pathString)
    {
        if (pathString.empty()) {
            throw std::invalid_argument("Ngspice NetlistPath must not be empty");
        }
        if (pathString.find_first_of("\" \t\r\n") != std::string::npos) {
            throw std::invalid_argument(
                "Ngspice netlist paths must not contain whitespace or quotes");
        }
        const std::filesystem::path path =
            std::filesystem::absolute(pathString);
        if (!std::filesystem::is_regular_file(path)) {
            throw std::invalid_argument(
                "Ngspice netlist file does not exist: " + path.string());
        }

        std::ifstream netlist(path);
        if (!netlist) {
            throw std::runtime_error(
                "Could not read Ngspice netlist: " + path.string());
        }

        bool hasTransientAnalysis = false;
        std::string line;
        while (std::getline(netlist, line)) {
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '*') {
                continue;
            }
            std::string directive = normalize(line.substr(first));
            if (directive.rfind(".control", 0) == 0) {
                throw std::invalid_argument(
                    "Ngspice netlists used by this block must not contain a "
                    ".control section");
            }
            if (directive.rfind(".tran", 0) == 0 &&
                (directive.size() == 5 ||
                 std::isspace(static_cast<unsigned char>(directive[5])))) {
                hasTransientAnalysis = true;
            }
        }
        if (!hasTransientAnalysis) {
            throw std::invalid_argument(
                "Ngspice netlist must contain a .tran analysis");
        }
        return path;
    }

    std::string errorWithDiagnostics(const std::string& message) const
    {
        if (diagnostics_.empty()) {
            return message;
        }
        return message + "\nngspice: " + diagnostics_;
    }

    void stopSimulation() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shuttingDown_ = true;
            continueSimulation_ = true;
            condition_.notify_all();
        }

        bool shouldWait = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shouldWait = started_ && backgroundRunning_;
        }
        if (shouldWait) {
            std::string command = "bg_halt";
            if (ngSpice_Command(command.data()) != 0) {
                std::cerr << "Could not halt the ngspice background simulation\n";
            }
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return !backgroundRunning_; });
        }
    }

    void recordCallbackFailure(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_.empty()) {
            failure_ = message;
        }
        condition_.notify_all();
    }

    void onData(NgspiceVecValuesAll* values, int vectorCount)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (values == nullptr || values->vectors == nullptr ||
            vectorCount <= 0) {
            failure_ = "ngspice returned an empty transient data record";
            condition_.notify_all();
            return;
        }

        std::unordered_map<std::string, double> record;
        bool hasTime = false;
        double time = 0.0;
        for (int index = 0; index < vectorCount; ++index) {
            const auto* vector = values->vectors[index];
            if (vector == nullptr || vector->name == nullptr) {
                continue;
            }
            const std::string name = normalize(vector->name);
            record[name] = vector->real;
            if (vector->isScale || name == "time") {
                time = vector->real;
                hasTime = true;
            }
        }
        if (!hasTime) {
            failure_ = "ngspice transient data did not include its time vector";
            condition_.notify_all();
            return;
        }

        for (std::size_t index = 0; index < outputVectors_.size(); ++index) {
            const auto found = record.find(outputVectorKey(outputVectors_[index]));
            if (found == record.end()) {
                failure_ = "Ngspice output vector was not found: " +
                           outputVectors_[index];
                condition_.notify_all();
                return;
            }
            latestOutputs_[index] = found->second;
        }

        if (!started_ || time < targetTime_ -
                                  timeTolerance(targetTime_, targetTime_)) {
            return;
        }

        const double tolerance =
            timeTolerance(targetTime_, targetTime_ - lastTargetTime_);
        if (std::abs(time - targetTime_) > tolerance) {
            failure_ = "ngspice transient output time " + std::to_string(time) +
                       " does not match requested discrete time " +
                       std::to_string(targetTime_) +
                       "; ngspice did not reach the requested breakpoint";
            condition_.notify_all();
            return;
        }

        boundaryReached_ = true;
        condition_.notify_all();
        condition_.wait(lock, [this] {
            return continueSimulation_ || shuttingDown_;
        });
        continueSimulation_ = false;
    }

    double sourceValue(char* name, char expectedType)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = name == nullptr ? sources_.end()
                                           : sources_.find(normalize(name));
        if (found == sources_.end() || found->second.type != expectedType) {
            if (failure_.empty()) {
                failure_ = "ngspice requested an unconfigured external " +
                           std::string(expectedType == 'v' ? "voltage" : "current") +
                           " source: " + (name == nullptr ? "" : name);
            }
            condition_.notify_all();
            return 0.0;
        }
        return inputs_[found->second.portIndex];
    }

    static int sendChar(char* text, int, void* userData) noexcept
    {
        auto* session = static_cast<NgspiceSession*>(userData);
        if (session == nullptr || text == nullptr) {
            return 0;
        }
        try {
            std::lock_guard<std::mutex> lock(session->mutex_);
            const std::string message(text);
            if (session->diagnostics_.size() < 4096) {
                session->diagnostics_.append(
                    message.substr(0, 4095 - session->diagnostics_.size()));
                session->diagnostics_.push_back('\n');
            }
            if (message.find("No. of Data Rows") != std::string::npos) {
                session->analysisComplete_ = true;
                session->condition_.notify_all();
            }
        } catch (...) {
        }
        return 0;
    }

    static int sendStatus(char*, int, void*) noexcept
    {
        return 0;
    }

    static int controlledExit(int status, int, int, int, void* userData) noexcept
    {
        auto* session = static_cast<NgspiceSession*>(userData);
        if (session != nullptr) {
            try {
                std::lock_guard<std::mutex> lock(session->mutex_);
                session->failure_ =
                    "ngspice terminated unexpectedly with status " +
                    std::to_string(status);
                session->analysisComplete_ = true;
                session->backgroundRunning_ = false;
                session->condition_.notify_all();
            } catch (...) {
            }
        }
        return 0;
    }

    static int sendData(
        NgspiceVecValuesAll* values, int vectorCount, int, void* userData) noexcept
    {
        auto* session = static_cast<NgspiceSession*>(userData);
        if (session != nullptr) {
            try {
                session->onData(values, vectorCount);
            } catch (...) {
                session->recordCallbackFailure(
                    "An exception occurred while reading ngspice output data");
            }
        }
        return 0;
    }

    static int sendInitData(void*, int, void*) noexcept
    {
        return 0;
    }

    static int backgroundThreadRunning(int notRunning, int, void* userData) noexcept
    {
        auto* session = static_cast<NgspiceSession*>(userData);
        if (session != nullptr) {
            try {
                std::lock_guard<std::mutex> lock(session->mutex_);
                session->backgroundRunning_ = (notRunning == 0);
                session->condition_.notify_all();
            } catch (...) {
            }
        }
        return 0;
    }

    static int getVoltageSourceData(
        double* value, double, char* name, int, void* userData) noexcept
    {
        auto* session = static_cast<NgspiceSession*>(userData);
        if (session == nullptr || value == nullptr) {
            return 0;
        }
        try {
            *value = session->sourceValue(name, 'v');
        } catch (...) {
            session->recordCallbackFailure(
                "Could not provide an ngspice external voltage source value");
        }
        return 0;
    }

    static int getCurrentSourceData(
        double* value, double, char* name, int, void* userData) noexcept
    {
        auto* session = static_cast<NgspiceSession*>(userData);
        if (session == nullptr || value == nullptr) {
            return 0;
        }
        try {
            *value = session->sourceValue(name, 'i');
        } catch (...) {
            session->recordCallbackFailure(
                "Could not provide an ngspice external current source value");
        }
        return 0;
    }

    inline static std::mutex runtimeMutex_;
    inline static bool runtimeInUse_ = false;

    bool ownsRuntime_ = false;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::string> inputSources_;
    std::vector<std::string> outputVectors_;
    std::vector<double> inputs_;
    std::vector<double> latestOutputs_;
    std::unordered_map<std::string, InputSource> sources_;
    std::string diagnostics_;
    std::string failure_;
    double targetTime_ = 0.0;
    double lastTargetTime_ = 0.0;
    bool started_ = false;
    bool backgroundRunning_ = false;
    bool analysisComplete_ = false;
    bool boundaryReached_ = false;
    bool continueSimulation_ = false;
    bool shuttingDown_ = false;
};

}  // namespace BlockTypeSupports::NgspiceSupport

#endif
