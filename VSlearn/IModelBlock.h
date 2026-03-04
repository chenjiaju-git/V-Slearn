/**
 * @file IModelBlock.h
 * @brief 统一模型架构接口定义 (Architecture Interface Contract)
 * @author Architect Team (Module 1)
 * @version 1.0.0
 * * 说明：
 * 1. 本文件定义了系统所有仿真组件（FMU, AI模型, 普通C++算法）必须遵循的统一标准。
 * 2. 模块2生成的 C++ 包装类（Wrapper）必须 public 继承此类。
 * 3. 模块1生成的 架构调度代码 将通过此类指针多态调用子模型。
 */

#pragma once

#include <string>
#include <vector>
#include <map>

 // -------------------------------------------------------------------------
 // 1. 标准状态码定义 (Model Status)
 // 用于 step/init 等接口的返回值，统一错误处理逻辑
 // -------------------------------------------------------------------------
enum class ModelStatus {
    OK = 0,             ///< 执行成功
    WARNING = 1,        ///< 出现警告，但模型可继续运行
    ERROR = 2,          ///< 一般错误（如数值发散），当前步长无效
    FATAL = 3           ///< 致命错误（如资源丢失、内存崩溃），必须立即停止仿真
};

// -------------------------------------------------------------------------
// 2. 统一模型接口基类 (Abstract Base Class)
// -------------------------------------------------------------------------
class IModelBlock {
public:
    // 虚析构函数，确保派生类资源（如 FMU 内存、ONNX Session）能正确释放
    virtual ~IModelBlock() = default;

    // =================================================================
    // 生命周期接口 (Lifecycle Methods)
    // =================================================================

    /**
     * @brief 初始化模型
     * 对应动作：
     * - FMU: fmi2Instantiate, fmi2EnterInitializationMode
     * - AI:  Load ONNX Model, Create Session
     * @return 状态码
     */
    virtual ModelStatus init() = 0;

    /**
     * @brief 参数配置 (Configuration Injection)
     * 在 init 之后、第一次 step 之前调用。
     * @param configData 配置字符串（通常为 JSON 格式），包含初始参数值（如重力 g, 质量 m）
     * @return 状态码
     */
    virtual ModelStatus configure(const std::string& configData) = 0;

    /**
     * @brief 执行单步仿真 (Step)
     * 核心计算逻辑。
     * @param time 当前仿真绝对时间 (seconds)
     * @param stepSize 本次步长 (seconds, dt)
     * @return 状态码
     */
    virtual ModelStatus step(double time, double stepSize) = 0;

    /**
     * @brief 重置模型 (Reset)
     * 将模型状态回滚到 T=0 时刻，用于多轮测试或自动化回归测试。
     * 对应动作：FMU fmi2Reset
     */
    virtual ModelStatus reset() = 0;

    /**
     * @brief 终止并清理 (Terminate)
     * 仿真结束时调用，用于释放 DLL 句柄、关闭文件、释放显存。
     * 对应动作：FMU fmi2FreeInstance
     */
    virtual ModelStatus terminate() = 0;

    // =================================================================
    // 数据交互与诊断接口 (Data Exchange & Diagnostics)
    // =================================================================

    /**
     * @brief 获取最后一次错误的详细描述
     * 当 step 返回 ERROR/FATAL 时，主程序调用此接口记录日志。
     */
    virtual std::string getLastError() const = 0;

    /**
     * @brief 设置实数输入端口的值 (Data Input)
     * 架构层通过此接口将数据（如 SignalBus 的数据）注入模型。
     * @param portName 端口名称（对应 JSON 中的 Input 变量名）
     * @param value 具体数值
     */
    virtual void setRealInput(const std::string& portName, double value) = 0;

    /**
     * @brief 获取实数输出端口的值 (Data Output)
     * 架构层通过此接口从模型提取计算结果。
     * @param portName 端口名称（对应 JSON 中的 Output 变量名）
     * @return 具体数值
     */
    virtual double getRealOutput(const std::string& portName) = 0;

    // (可选) 如果未来支持整型或字符串，可在此扩展 setIntegerInput 等接口
};