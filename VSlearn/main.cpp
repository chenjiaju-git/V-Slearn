#include "BouncingBallAdapter.h"
#include <iostream>
#include <cmath>
#include <string>

// 浮点数近似相等判断（避免精度问题）
bool isApproxEqual(double a, double b, double eps = 1e-3) {
    return std::fabs(a - b) < eps;
}

int main() {
    // ===================== 1. 基础配置 =====================
    // 替换为你本地的FMU解压目录
    std::string fmuExtractDir = "D:\\Application\\VS Project\\FMU\\FmuParser\\x64\\Release\\fmu-model\\BouncingBall";
    bool testSuccess = true;

    try {
        std::cout << "===== 开始BouncingBall核心流程测试 =====\n" << std::endl;

        // 创建适配器实例
        BouncingBallAdapter adapter(fmuExtractDir);
        std::cout << "✅ 适配器实例创建成功" << std::endl;

        // ===================== 2. 初始化FMU =====================
        ModelStatus initStatus = adapter.init();
        if (initStatus != ModelStatus::OK) {
            throw std::runtime_error("初始化失败: " + adapter.getLastError());
        }
        std::cout << "✅ FMU初始化成功" << std::endl;

        // ===================== 3. 自定义参数配置 =====================
        // 自定义参数：重力g=-10，恢复系数e=0.8，初始高度h=2.0
        std::string customConfig = R"({
            "startTime": 0.0,
            "stopTime": 5.0,
            "parameters": {
                "g": -10.0,
                "e": 0.8,
                "h": 2.0
            }
        })";
        ModelStatus configStatus = adapter.configure(customConfig);
        if (configStatus != ModelStatus::OK) {
            throw std::runtime_error("配置失败: " + adapter.getLastError());
        }
        // 校验初始参数是否生效
        double init_h = adapter.getRealOutput("h");
        double init_g = adapter.getRealOutput("g");
        if (isApproxEqual(init_h, 2.0) && isApproxEqual(init_g, -10.0)) {
            std::cout << "✅ 自定义参数配置成功（h=" << init_h << ", g=" << init_g << "）" << std::endl;
        }
        else {
            throw std::runtime_error("初始参数配置异常: h=" + std::to_string(init_h) + ", g=" + std::to_string(init_g));
        }

        // ===================== 4. 第一次仿真：连续2步步进 =====================
        std::cout << "\n----- 第一次仿真（连续2步）-----" << std::endl;
        double currentTime = 0.0;
        double stepSize = 0.01; // 步长0.01秒
        // 第1步
        ModelStatus step1Status = adapter.step(currentTime, stepSize);
        if (step1Status != ModelStatus::OK) {
            throw std::runtime_error("第1步仿真失败: " + adapter.getLastError());
        }
        double h1 = adapter.getRealOutput("h");
        double expected_h1 = 2.0 + 0.5 * (-10.0) * pow(stepSize, 2); // 自由落体公式
        std::cout << "✅ 第1步仿真完成 | 时间=" << currentTime + stepSize << "s | 高度h=" << h1 << "（预期≈" << expected_h1 << "）" << std::endl;

        // 第2步
        currentTime += stepSize;
        ModelStatus step2Status = adapter.step(currentTime, stepSize);
        if (step2Status != ModelStatus::OK) {
            throw std::runtime_error("第2步仿真失败: " + adapter.getLastError());
        }
        double h2 = adapter.getRealOutput("h");
        double expected_h2 = 2.0 + 0.5 * (-10.0) * pow(currentTime + stepSize, 2);
        std::cout << "✅ 第2步仿真完成 | 时间=" << currentTime + stepSize << "s | 高度h=" << h2 << "（预期≈" << expected_h2 << "）" << std::endl;

        // ===================== 5. 执行reset重置 =====================
        ModelStatus resetStatus = adapter.reset();
        if (resetStatus != ModelStatus::OK) {
            throw std::runtime_error("重置失败: " + adapter.getLastError());
        }
        // 校验重置后参数是否回到初始值
        double reset_h = adapter.getRealOutput("h");
        if (isApproxEqual(reset_h, 2.0)) {
            std::cout << "\n✅ FMU重置成功（高度h回到初始值2.0）" << std::endl;
        }
        else {
            throw std::runtime_error("重置后参数异常: h=" + std::to_string(reset_h));
        }

        // ===================== 6. 第二次仿真：再连续2步步进 =====================
        std::cout << "\n----- 第二次仿真（重置后连续2步）-----" << std::endl;
        currentTime = 0.0; // 重置后时间回到0
        // 第1步（重置后）
        ModelStatus step3Status = adapter.step(currentTime, stepSize);
        if (step3Status != ModelStatus::OK) {
            throw std::runtime_error("重置后第1步仿真失败: " + adapter.getLastError());
        }
        double h3 = adapter.getRealOutput("h");
        std::cout << "✅ 重置后第1步仿真完成 | 时间=" << currentTime + stepSize << "s | 高度h=" << h3 << "（预期≈" << expected_h1 << "）" << std::endl;

        // 第2步（重置后）
        currentTime += stepSize;
        ModelStatus step4Status = adapter.step(currentTime, stepSize);
        if (step4Status != ModelStatus::OK) {
            throw std::runtime_error("重置后第2步仿真失败: " + adapter.getLastError());
        }
        double h4 = adapter.getRealOutput("h");
        std::cout << "✅ 重置后第2步仿真完成 | 时间=" << currentTime + stepSize << "s | 高度h=" << h4 << "（预期≈" << expected_h2 << "）" << std::endl;

        // ===================== 7. 执行terminate终止 =====================
        ModelStatus termStatus = adapter.terminate();
        if (termStatus != ModelStatus::OK) {
            throw std::runtime_error("终止失败: " + adapter.getLastError());
        }
        std::cout << "\n✅ FMU资源终止成功" << std::endl;

    }
    catch (const std::exception& e) {
        // 异常捕获，避免程序崩溃
        std::cerr << "\n❌ 测试失败: " << e.what() << std::endl;
        testSuccess = false;
    }

    // ===================== 测试总结 =====================
    std::cout << "\n===== 测试总结 =====" << std::endl;
    if (testSuccess) {
        std::cout << "✅ 所有核心流程测试通过！" << std::endl;
        std::cout << "✅ 自定义参数生效 | 两次仿真正常 | reset重置有效 | terminate终止成功" << std::endl;
    }
    else {
        std::cout << "❌ 测试失败，请检查上述错误信息" << std::endl;
    }

    system("pause");
    return testSuccess ? 0 : 1;
}