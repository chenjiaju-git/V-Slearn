#pragma once
#include <windows.h>
#undef ERROR
#include "IModelBlock.h"
#include "fmi2Functions.h"
#include <string>
#include <unordered_map>
#include "json.hpp"                                                  
class BouncingBallAdapter : public IModelBlock {
private:
    std::unordered_map<std::string, fmi2ValueReference> m_varNameToVR;
    std::string m_fmuDllPath;
    HMODULE m_fmuDllHandle = nullptr;
    fmi2Component m_fmiComponent = nullptr;
    std::string m_lastErrorMsg;
    bool m_isInitialized = false;
    std::string m_lastConfigData;

    // FMI 2.0 标准函数指针类型
    typedef fmi2Component(*fmi2InstantiateType)(fmi2String, fmi2Type, fmi2String, fmi2String, const fmi2CallbackFunctions*, fmi2Boolean, fmi2Boolean);
    typedef fmi2Status(*fmi2SetupExperimentType)(fmi2Component, fmi2Boolean, fmi2Real, fmi2Real, fmi2Boolean, fmi2Real);
    typedef fmi2Status(*fmi2EnterInitializationModeType)(fmi2Component);
    typedef fmi2Status(*fmi2ExitInitializationModeType)(fmi2Component);
    typedef fmi2Status(*fmi2DoStepType)(fmi2Component, fmi2Real, fmi2Real, fmi2Boolean);
    typedef fmi2Status(*fmi2SetRealType)(fmi2Component, const fmi2ValueReference[], size_t, const fmi2Real[]);
    typedef fmi2Status(*fmi2GetRealType)(fmi2Component, const fmi2ValueReference[], size_t, fmi2Real[]);
    typedef fmi2Status(*fmi2TerminateType)(fmi2Component);
    typedef fmi2Status(*fmi2FreeInstanceType)(fmi2Component);
    typedef fmi2Status(*fmi2ResetType)(fmi2Component);

    // FMI函数指针
    fmi2InstantiateType m_fmi2Instantiate = nullptr;
    fmi2SetupExperimentType m_fmi2SetupExperiment = nullptr;
    fmi2EnterInitializationModeType m_fmi2EnterInitializationMode = nullptr;
    fmi2ExitInitializationModeType m_fmi2ExitInitializationMode = nullptr;
    fmi2DoStepType m_fmi2DoStep = nullptr;
    fmi2SetRealType m_fmi2SetReal = nullptr;
    fmi2GetRealType m_fmi2GetReal = nullptr;
    fmi2TerminateType m_fmi2Terminate = nullptr;
    fmi2FreeInstanceType m_fmi2FreeInstance = nullptr;
    fmi2ResetType m_fmi2Reset = nullptr;
    void setError(const std::string& msg) { m_lastErrorMsg = msg; }
public:
    explicit BouncingBallAdapter(const std::string& fmuExtractDir);
    ~BouncingBallAdapter() override;
    ModelStatus init() override;
    ModelStatus configure(const std::string& configData) override;
    ModelStatus reset() override;
    ModelStatus step(double time, double stepSize) override;
    void setRealInput(const std::string& portName, double value) override;
    double getRealOutput(const std::string& portName) override;
    ModelStatus terminate() override;
    std::string getLastError() const override;
};