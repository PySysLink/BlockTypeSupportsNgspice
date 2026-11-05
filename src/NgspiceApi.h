#ifndef BLOCK_TYPE_SUPPORTS_NGSPICE_API_H
#define BLOCK_TYPE_SUPPORTS_NGSPICE_API_H

#include <dlfcn.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace BlockTypeSupports::NgspiceSupport {

struct NgspiceVecValue {
    char* name;
    double real;
    double imaginary;
    int isScale;
    int isComplex;
};

struct NgspiceVecValuesAll {
    int vectorCount;
    int vectorIndex;
    NgspiceVecValue** vectors;
};

using NgspiceSendChar = int (*)(char*, int, void*);
using NgspiceSendStatus = int (*)(char*, int, void*);
using NgspiceControlledExit = int (*)(int, int, int, int, void*);
using NgspiceSendData = int (*)(NgspiceVecValuesAll*, int, int, void*);
using NgspiceSendInitData = int (*)(void*, int, void*);
using NgspiceBackgroundThreadRunning = int (*)(int, int, void*);
using NgspiceGetSourceData = int (*)(double*, double, char*, int, void*);
using NgspiceGetSyncData =
    int (*)(double, double*, double, int, int, int, void*);

class NgspiceApi {
public:
    using InitFunction = int (*)(
        NgspiceSendChar,
        NgspiceSendStatus,
        NgspiceControlledExit,
        NgspiceSendData,
        NgspiceSendInitData,
        NgspiceBackgroundThreadRunning,
        void*);
    using InitSyncFunction = int (*)(
        NgspiceGetSourceData,
        NgspiceGetSourceData,
        NgspiceGetSyncData,
        int*,
        void*);
    using ResetFunction = int (*)();
    using CommandFunction = int (*)(char*);

    explicit NgspiceApi(const std::string& libraryPath)
        : libraryPath_(libraryPath)
    {
        if (libraryPath_.empty()) {
            throw std::invalid_argument("NgspiceLibraryPath must not be empty");
        }
        libraryHandle_ = ::dlopen(libraryPath_.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (libraryHandle_ == nullptr) {
            throw std::runtime_error(
                "Could not load ngspice library '" + libraryPath_ + "': " +
                dlError());
        }
        try {
            init_ = loadSymbol<InitFunction>("ngSpice_Init");
            initSync_ = loadSymbol<InitSyncFunction>("ngSpice_Init_Sync");
            reset_ = loadSymbol<ResetFunction>("ngSpice_Reset");
            command_ = loadSymbol<CommandFunction>("ngSpice_Command");
        } catch (...) {
            ::dlclose(libraryHandle_);
            libraryHandle_ = nullptr;
            throw;
        }
    }

    ~NgspiceApi()
    {
        close();
    }

    NgspiceApi(const NgspiceApi&) = delete;
    NgspiceApi& operator=(const NgspiceApi&) = delete;

    NgspiceApi(NgspiceApi&& other) noexcept
    {
        swap(other);
    }

    NgspiceApi& operator=(NgspiceApi&& other) noexcept
    {
        if (this != &other) {
            close();
            swap(other);
        }
        return *this;
    }

    int init(
        NgspiceSendChar sendChar,
        NgspiceSendStatus sendStatus,
        NgspiceControlledExit controlledExit,
        NgspiceSendData sendData,
        NgspiceSendInitData sendInitData,
        NgspiceBackgroundThreadRunning backgroundThreadRunning,
        void* userData) const
    {
        return init_(sendChar, sendStatus, controlledExit, sendData,
                     sendInitData, backgroundThreadRunning, userData);
    }

    int initSync(
        NgspiceGetSourceData getVoltageSourceData,
        NgspiceGetSourceData getCurrentSourceData,
        NgspiceGetSyncData getSyncData,
        int* identifier,
        void* userData) const
    {
        return initSync_(getVoltageSourceData, getCurrentSourceData, getSyncData,
                         identifier, userData);
    }

    int reset() const
    {
        return reset_();
    }

    int command(char* commandText) const
    {
        return command_(commandText);
    }

    const std::string& libraryPath() const
    {
        return libraryPath_;
    }

private:
    template <typename Function>
    Function loadSymbol(const char* name)
    {
        ::dlerror();
        void* symbol = ::dlsym(libraryHandle_, name);
        const char* error = ::dlerror();
        if (error != nullptr || symbol == nullptr) {
            throw std::runtime_error(
                "Ngspice library '" + libraryPath_ +
                "' is missing required symbol '" + name + "': " +
                (error == nullptr ? "symbol address is null" : error));
        }
        return reinterpret_cast<Function>(symbol);
    }

    static std::string dlError()
    {
        const char* error = ::dlerror();
        return error == nullptr ? "unknown dynamic loader error" : error;
    }

    void close() noexcept
    {
        if (libraryHandle_ != nullptr) {
            ::dlclose(libraryHandle_);
            libraryHandle_ = nullptr;
        }
    }

    void swap(NgspiceApi& other) noexcept
    {
        using std::swap;
        swap(libraryPath_, other.libraryPath_);
        swap(libraryHandle_, other.libraryHandle_);
        swap(init_, other.init_);
        swap(initSync_, other.initSync_);
        swap(reset_, other.reset_);
        swap(command_, other.command_);
    }

    std::string libraryPath_;
    void* libraryHandle_ = nullptr;
    InitFunction init_ = nullptr;
    InitSyncFunction initSync_ = nullptr;
    ResetFunction reset_ = nullptr;
    CommandFunction command_ = nullptr;
};

}  // namespace BlockTypeSupports::NgspiceSupport

#endif
