#pragma once
#include "installer.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace setup::diagnostics {
inline std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
// 只按已记录的错误分类；未知原因保留原文，不猜测故障机状态。
inline const std::array<const char*, 3>* Match(const std::string& detail) {
    static const std::array<const char*, 3> rules[] = {
        {"being used by another process", "应用目录仍被进程占用，暂时无法替换。", "退出占用安装目录的程序或终端，再点击返回设置并重试；仍失败时保留日志反馈。"},
        {"更新调用方", "原更新进程已退出或不再匹配本次更新。", "点击返回设置并重试，由安装向导重新校验缓存并完成更新。"},
        {"symbolic link or junction", "后台进程的目录链接未通过安装器检查。", "重启电脑后先不要打开原助理，再重试；若仍失败，请复制诊断信息反馈。"},
        {"需要重启后才能继续", "Windows 已准备好所需系统组件。", "重启电脑后重新运行安装程序，即可继续安装原助理。"},
        {"process control failed", "检查或关闭后台进程失败。", "退出原助理及相关任务后重试；仍失败时重启电脑，并复制诊断信息反馈。"},
        {"后台进程", "检查或关闭后台进程失败。", "退出原助理及相关任务后重试；仍失败时重启电脑，并复制诊断信息反馈。"},
        {"退出码：1223", "本次管理员授权已取消。", "重新安装时允许系统的管理员授权请求。"},
        {"管理员授权", "本次管理员授权未完成。", "重新安装时允许系统的管理员授权请求。"},
        {"退出码：1618", "另一个安装任务正在运行。", "等待其他安装任务结束，再重试本次安装。"},
        {"错误 1058", "Windows 服务被禁用，系统组件安装无法启动。", "运行 services.msc，检查 Windows Modules Installer (TrustedInstaller) 与 Windows Update (wuauserv) 的服务设置；若由组织策略禁用，请联系管理员恢复后重试。"},
        {"0x800f081f", "Windows 组件服务找不到所需安装源（0x800F081F）。", "请复制故障机 %WINDIR%\\Logs\\CBS\\CBS.log 中本次安装时段的记录，以确认缺少哪个包或组件。"},
        {"0x80240037", "Windows Update 不支持此更新搜索操作。", "向导已自动解包并通过 Package Manager 本地安装；若仍失败，请复制诊断信息并附上 CBS 日志，不需要手动启用 Windows Update 服务。"},
        {"win32_error=5", "Windows 拒绝了应用目录的文件操作。", "退出占用安装目录的程序后重试；必要时选择有写入权限的安装位置。"},
        {"access is denied", "Windows 拒绝了文件或进程操作。", "检查安装位置权限，并关闭占用该目录的程序后重试。"},
        {"空间不足", "安装所需的磁盘空间不足。", "清理目标盘和系统临时目录所在磁盘的空间后重试。"},
        {"0x80092003", "Windows 在签名检查时无法读取或写入文件。", "查看详情中的具体文件路径，检查文件是否存在、目录权限和安全软件拦截记录后重试。"},
        {"0x80073d02", "Windows 应用包仍被占用，暂时无法注册。", "关闭原助理及其分享窗口，再点击返回设置并重试。"},
        {"身份修复或启动检查失败", "Windows 应用身份修复或启动检查未通过。", "查看详情中的具体系统错误，并保留 native-share-register.log.stderr。"},
        {"签名", "安装包或应用的签名校验未通过。", "重新下载安装包；仍失败时复制诊断信息反馈，保留签名校验。"},
        {"sha-256", "下载文件的摘要校验未通过。", "检查网络后重新下载，仍失败时复制诊断信息反馈。"},
        {"网络", "连接安装服务或下载文件失败。", "检查网络及代理设置后重试。"},
        {"webview2", "WebView2 运行环境未准备完成。", "查看详情中的 WebView2 安装结果，完成运行环境安装后重试。"},
    };
    const auto text = Lower(detail);
    for (const auto& rule : rules) if (text.find(rule[0]) != std::string::npos) return &rule;
    return nullptr;
}
inline std::string Reason(const Snapshot& state) {
    const auto rule = Match(state.detail);
    return rule ? (*rule)[1] : state.detail;
}
inline std::string Advice(const Snapshot& state) {
    if (state.phase == Phase::RestartRequired) return state.restartRegistered ?
        "保存工作后选择重启。下次登录此账户时向导会继续安装。" : "请重启电脑并手动打开安装向导继续。";
    const auto rule = Match(state.detail);
    return rule ? (*rule)[2] : "查看安装详情，按提示处理后重试；需要协助时请复制诊断信息反馈。";
}
inline std::string Report(const Snapshot& state) {
    std::ostringstream report;
    report << "原助理安装诊断\n阶段：" << StageName(state.stage)
        << "\n应用版本：" << (state.version.empty() ? "尚未取得" : state.version)
        << "\n安装位置：" << state.destination.u8string()
        << "\n用时：" << std::fixed << std::setprecision(2) << state.elapsed << " 秒"
        << "\n结果：" << state.detail << "\n建议：" << Advice(state)
        << "\n日志：" << (state.logPath.empty() ? "未能写入日志，请复制本页信息" : state.logPath.u8string())
        << "\n\n系统检查\n";
    for (const auto& check : state.checks) report << check << '\n';
    report << "\n本次安装记录\n";
    for (const auto& event : state.events) report << event << '\n';
    return report.str();
}
}
