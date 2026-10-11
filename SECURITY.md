# 安全问题报告

## 报告方式

请在仓库 **Security** 页使用私密漏洞报告。若该入口不可用，请私下联系仓库所有者，并提供：

- 受影响的提交版本。
- 复现步骤或最小复现代码。
- 影响范围与预期结果。
- 已知修复建议。

请预留合理调查与修复时间，再公开技术细节。

## 代码检查

CI 扫描常见秘密格式、拒绝生成的二进制与超大文件、运行 C++ 测试与嵌入脚本回归，并对第一方代码执行 MSVC 原生静态分析。
涉及进程访问、PE 加载和 Hook 的改动仍需人工审查与实际环境验证。

## English

Use private vulnerability reporting under the repository's **Security** tab, or contact the owner privately if that feature is unavailable.
Include the affected revision, reproduction steps, impact and suggested fixes. Allow time for investigation before public disclosure.
CI runs hygiene and secret checks, unit tests, embedding regressions and MSVC native static analysis; process access, PE loading and hooks still require manual review and runtime validation.
