#include "BouncingBallAdapter.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <cstdarg>
#include <cmath>
namespace fs = std::filesystem;
using json = nlohmann::json;

// FMI回调：打印FMU原生日志
void fmi2Logger(fmi2ComponentEnvironment, fmi2String instanceName, fmi2Status status, fmi2String category, fmi2String message, ...) {
    va_list args;
    va_start(args, message);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);
    std::cout << "[FMU-LOG] " << instanceName << " [" << category << "] " << buffer
        << " (FMI状态: " << static_cast<int>(status) << ")" << std::endl;
}

BouncingBallAdapter::BouncingBallAdapter(const std::string& fmuExtractDir) {
    // 仅初始化：变量名→ValueReference映射（无其他属性/默认值）
    m_varNameToVR["time"] = 0;
    m_varNameToVR["h"] = 1;
    m_varNameToVR["der(h)"] = 2;
    m_varNameToVR["v"] = 3;
    m_varNameToVR["der(v)"] = 4;
    m_varNameToVR["g"] = 5;
    m_varNameToVR["e"] = 6;
    m_varNameToVR["v_min"] = 7;

    // 递归查找win64目录下的FMU DLL
    bool dllFound = false;
    for (const auto& entry : fs::recursive_directory_iterator(fmuExtractDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
            if (entry.path().parent_path().filename() == "win64") {
                m_fmuDllPath = entry.path().string();
                dllFound = true;
                break;
            }
            if (m_fmuDllPath.empty()) m_fmuDllPath = entry.path().string();
        }
    }
    if (!dllFound) throw std::runtime_error("未找到FMU DLL，路径：" + fmuExtractDir);
    std::cout << "[BouncingBallAdapter] 找到FMU DLL: " << m_fmuDllPath << std::endl;
}

BouncingBallAdapter::~BouncingBallAdapter() {
    terminate();
}

ModelStatus BouncingBallAdapter::init() {
    // 重复初始化校验
    if (m_isInitialized) {
        setError("重复调用init()，FMU已初始化");
        return ModelStatus::WARNING;
    }
    // 1. 加载FMU DLL
    m_fmuDllHandle = LoadLibraryA(m_fmuDllPath.c_str());
    if (!m_fmuDllHandle) {
        setError("加载DLL失败，错误码：" + std::to_string(GetLastError()));
        return ModelStatus::FATAL;
    }
    // 2. 获取FMI标准函数指针（含reset）
    m_fmi2Instantiate = (fmi2InstantiateType)GetProcAddress(m_fmuDllHandle, "fmi2Instantiate");
    m_fmi2SetupExperiment = (fmi2SetupExperimentType)GetProcAddress(m_fmuDllHandle, "fmi2SetupExperiment");
    m_fmi2EnterInitializationMode = (fmi2EnterInitializationModeType)GetProcAddress(m_fmuDllHandle, "fmi2EnterInitializationMode");
    m_fmi2ExitInitializationMode = (fmi2ExitInitializationModeType)GetProcAddress(m_fmuDllHandle, "fmi2ExitInitializationMode");
    m_fmi2DoStep = (fmi2DoStepType)GetProcAddress(m_fmuDllHandle, "fmi2DoStep");
    m_fmi2SetReal = (fmi2SetRealType)GetProcAddress(m_fmuDllHandle, "fmi2SetReal");
    m_fmi2GetReal = (fmi2GetRealType)GetProcAddress(m_fmuDllHandle, "fmi2GetReal");
    m_fmi2Terminate = (fmi2TerminateType)GetProcAddress(m_fmuDllHandle, "fmi2Terminate");
    m_fmi2FreeInstance = (fmi2FreeInstanceType)GetProcAddress(m_fmuDllHandle, "fmi2FreeInstance");
    m_fmi2Reset = (fmi2ResetType)GetProcAddress(m_fmuDllHandle, "fmi2Reset");
    // 核心函数缺失校验
    if (!m_fmi2Instantiate || !m_fmi2DoStep || !m_fmi2SetReal || !m_fmi2GetReal) {
        setError("缺失核心FMI函数指针");
        terminate();
        return ModelStatus::FATAL;
    }
    // 3. 实例化FMU（CoSimulation模式）
    const char* fmuGUID = "{1AE5E10D-9521-4DE3-80B9-D0EAAA7D5AF1}";
    std::string rootDir = m_fmuDllPath.substr(0, m_fmuDllPath.find("\\binaries\\win64"));
    std::replace(rootDir.begin(), rootDir.end(), '\\', '/');
    std::string resourceDir = "file:///" + rootDir + "/sources";
    fmi2CallbackFunctions callbacks{};
    callbacks.logger = fmi2Logger;
    m_fmiComponent = m_fmi2Instantiate("BouncingBall_Instance", fmi2CoSimulation, fmuGUID,
        resourceDir.c_str(), &callbacks, fmi2False, fmi2False);
    if (!m_fmiComponent) {
        setError("FMU实例化失败");
        terminate();
        return ModelStatus::FATAL;
    }
    std::cout << "[BouncingBallAdapter] init()完成，FMU实例化成功" << std::endl;
    return ModelStatus::OK;
}

ModelStatus BouncingBallAdapter::configure(const std::string& configData) {
    // 状态校验：仅未初始化阶段可调用
    if (m_isInitialized) {
        setError("configure()仅可在init后、step前调用");
        return ModelStatus::ERROR;
    }
    // 解析JSON配置（空JSON则用XML默认值）
    json configJson;
    try {
        configJson = configData.empty() ? json::object() : json::parse(configData);
    }
    catch (const json::parse_error& e) {
        setError("JSON解析失败: " + std::string(e.what()));
        return ModelStatus::ERROR;
    }
    m_lastConfigData = configData;
    // 核心：优先用JSON配置，无则用XML默认实验配置
    double startTime = configJson.value("startTime", 0);
    double stopTime = configJson.value("stopTime", 3);
    double tolerance = configJson.value("tolerance", 1e-6);
    // 执行FMI实验配置
    fmi2Status fmiStatus = m_fmi2SetupExperiment(m_fmiComponent, fmi2False, tolerance, startTime, fmi2True, stopTime);
    if (fmiStatus != fmi2OK) {
        setError("SetupExperiment失败: 状态码=" + std::to_string(static_cast<int>(fmiStatus)));
        return ModelStatus::ERROR;
    }
    // 进入初始化模式
    fmiStatus = m_fmi2EnterInitializationMode(m_fmiComponent);
    if (fmiStatus != fmi2OK) {
        setError("EnterInitializationMode失败");
        return ModelStatus::ERROR;
    }
    // 解析并设置自定义参数
    if (configJson.contains("parameters") && configJson["parameters"].is_object()) {
        for (auto& [paramName, paramValue] : configJson["parameters"].items()) {
            if (paramValue.is_number()) {
                double value = paramValue.get<double>();
                auto it = m_varNameToVR.find(paramName);
                if (it != m_varNameToVR.end()) {
                    fmi2ValueReference vr = it->second;
                    fmi2Real fmiVal = static_cast<fmi2Real>(value);
                    fmiStatus = m_fmi2SetReal(m_fmiComponent, &vr, 1, &fmiVal);
                    if (fmiStatus != fmi2OK) {
                        setError("设置参数" + paramName + "失败: 状态码=" + std::to_string(static_cast<int>(fmiStatus)));
                        return ModelStatus::ERROR;
                    }
                    std::cout << "[BouncingBallAdapter] 配置参数: " << paramName << " = " << value << " (VR: " << vr << ")" << std::endl;
                }
                else {
                    setError("参数" + paramName + "不存在于变量映射中");
                    return ModelStatus::ERROR;
                }
            }
            else {
                setError("参数" + paramName + "的值不是数字类型");
                return ModelStatus::ERROR;
            }
        }
    }
    // 退出初始化模式
    fmiStatus = m_fmi2ExitInitializationMode(m_fmiComponent);
    if (fmiStatus != fmi2OK) {
        setError("ExitInitializationMode失败");
        return ModelStatus::ERROR;
    }
    m_isInitialized = true;
    std::cout << "[BouncingBallAdapter] configure完成 | 起始时间=" << startTime << "s 终止时间=" << stopTime << "s" << std::endl;
    return ModelStatus::OK;
}

ModelStatus BouncingBallAdapter::reset() {
    // 状态校验
    if (!m_fmiComponent || !m_fmi2Reset) {
        setError("FMU未初始化或不支持reset");
        return ModelStatus::ERROR;
    }
    // 1. 调用FMI原生reset
    fmi2Status fmiStatus = m_fmi2Reset(m_fmiComponent);
    if (fmiStatus != fmi2OK) {
        setError("fmi2Reset失败: 状态码=" + std::to_string(static_cast<int>(fmiStatus)));
        return ModelStatus::ERROR;
    }
    // 2. 重置初始化状态+重应用上次配置
    m_isInitialized = false;
    if (!m_lastConfigData.empty()) {
        ModelStatus configStatus = configure(m_lastConfigData);
        if (configStatus != ModelStatus::OK) {
            setError("reset后重配置失败: " + getLastError());
            return ModelStatus::ERROR;
        }
    }
    std::cout << "[BouncingBallAdapter] reset完成，恢复到初始配置状态" << std::endl;
    return ModelStatus::OK;
}

ModelStatus BouncingBallAdapter::step(double time, double stepSize) {
    // 状态校验
    if (!m_isInitialized || !m_fmi2DoStep) {
        setError("step()仅可在configure后调用");
        return ModelStatus::ERROR;
    }
    // 步长合法性校验
    if (stepSize <= 0 || stepSize > 1.0) {
        setError("步长非法: " + std::to_string(stepSize) + "（建议0<stepSize≤1）");
        return ModelStatus::WARNING;
    }
    // 执行步进
    fmi2Status fmiStatus = m_fmi2DoStep(m_fmiComponent, (fmi2Real)time, (fmi2Real)stepSize, fmi2True);
    if (fmiStatus == fmi2OK) {
        std::cout << "[BouncingBallAdapter] step完成 | 时间=" << time << "s 步长=" << stepSize << "s" << std::endl;
        return ModelStatus::OK;
    }
    else {
        setError("step失败: 状态码=" + std::to_string(static_cast<int>(fmiStatus)));
        return fmiStatus == fmi2Warning ? ModelStatus::WARNING : ModelStatus::ERROR;
    }
}

void BouncingBallAdapter::setRealInput(const std::string& portName, double value) {
    // 状态校验
    if (!m_isInitialized || !m_fmi2SetReal) {
        setError("FMU未初始化，无法设置输入");
        std::cerr << "[Error] " << getLastError() << std::endl;
        return;
    }
    // 仅校验VR映射存在性（无其他属性校验）
    auto it = m_varNameToVR.find(portName);
    if (it == m_varNameToVR.end()) {
        setError("变量不存在: " + portName);
        std::cerr << "[Error] " << getLastError() << std::endl;
        return;
    }
    // 设置变量值
    fmi2ValueReference vr = it->second;
    fmi2Real fmiVal = static_cast<fmi2Real>(value);
    fmi2Status fmiStatus = m_fmi2SetReal(m_fmiComponent, &vr, 1, &fmiVal);
    if (fmiStatus == fmi2OK) {
        std::cout << "[BouncingBallAdapter] 设置输入: " << portName << " = " << value << " (VR: " << vr << ")" << std::endl;
    }
    else {
        setError("设置输入失败: " + portName + " 状态码=" + std::to_string(static_cast<int>(fmiStatus)));
        std::cerr << "[Error] " << getLastError() << std::endl;
    }
}

double BouncingBallAdapter::getRealOutput(const std::string& portName) {
    // 状态校验
    if (!m_isInitialized || !m_fmi2GetReal) {
        setError("FMU未初始化，无法读取输出");
        return 0.0;
    }
    // 仅校验VR映射存在性
    auto it = m_varNameToVR.find(portName);
    if (it == m_varNameToVR.end()) {
        setError("变量不存在: " + portName);
        return 0.0;
    }
    // 读取变量值
    fmi2ValueReference vr = it->second;
    fmi2Real fmiVal = 0.0;
    fmi2Status fmiStatus = m_fmi2GetReal(m_fmiComponent, &vr, 1, &fmiVal);
    if (fmiStatus == fmi2OK) {
        std::cout << "[BouncingBallAdapter] 读取输出: " << portName << " = " << fmiVal << " (VR: " << vr << ")" << std::endl;
        return static_cast<double>(fmiVal);
    }
    else {
        setError("读取输出失败: " + portName + " 状态码=" + std::to_string(static_cast<int>(fmiStatus)));
        return 0.0;
    }
}

ModelStatus BouncingBallAdapter::terminate() {
    // 释放FMU实例
    if (m_fmiComponent) {
        if (m_fmi2Terminate && m_isInitialized) m_fmi2Terminate(m_fmiComponent);
        if (m_fmi2FreeInstance) m_fmi2FreeInstance(m_fmiComponent);
        m_fmiComponent = nullptr;
    }
    // 释放DLL句柄
    if (m_fmuDllHandle) {
        FreeLibrary(m_fmuDllHandle);
        m_fmuDllHandle = nullptr;
    }
    m_isInitialized = false;
    m_lastErrorMsg.clear();
    std::cout << "[BouncingBallAdapter] terminate完成，资源已释放" << std::endl;
    return ModelStatus::OK;
}

std::string BouncingBallAdapter::getLastError() const {
    return m_lastErrorMsg.empty() ? "无错误" : m_lastErrorMsg;
}