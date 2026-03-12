/**
 * IPC Protobuf 转换函数与帧读写工具。
 * 供 Pre-fork Worker Pool 使用：
 * - 父进程侧：序列化 TUEntry 为 WorkerRequest，反序列化 WorkerResponse 为 CombinedOutput
 * - Worker 侧：反序列化 WorkerRequest 为 TUEntry，序列化 CombinedOutput 为 WorkerResponse
 * - 帧 I/O：长度前缀二进制帧的读写（[4 bytes uint32_t LE 长度][N bytes protobuf 数据]）
 */
#pragma once
#include "compile_commands/load.h"
#include "common/analysis_output.h"
#include <string>

namespace codexray {

// ── 帧 I/O ──────────────────────────────────────────────────────────────────

/// 写长度前缀帧到 fd。
/// @param fd 目标文件描述符（管道写端）
/// @param data 帧 payload（已序列化的 protobuf 数据）
/// @return false 表示写入失败（EPIPE 等管道错误）
bool WriteFrame(int fd, const std::string& data);

/// 读长度前缀帧。
/// @param fd 源文件描述符（管道读端）
/// @param out 读取的帧 payload
/// @param timeout_secs 超时秒数，0 = 无限等待
/// @param timed_out 输出参数：true = 超时，false = EOF 或成功
/// @return true = 成功读取一帧，false = EOF 或超时
bool ReadFrame(int fd, std::string& out, int timeout_secs, bool& timed_out);

// ── 父进程侧（pool.cc 使用）─────────────────────────────────────────────────

/// 将 TUEntry 序列化为 WorkerRequest protobuf 二进制数据
/// @param tu 待序列化的 TU 条目
/// @return 序列化后的 protobuf 二进制字符串
std::string SerializeTURequestPb(const TUEntry& tu);

/// 将 WorkerResponse protobuf 二进制数据反序列化为 CombinedOutput
/// @param data protobuf 二进制数据
/// @param out 输出的 CombinedOutput 结构
/// @param ok 输出：分析是否成功
/// @param error 输出：失败时的错误信息
/// @return true = 反序列化成功，false = 数据损坏
bool DeserializeWorkerResponsePb(const std::string& data, CombinedOutput& out,
                                  bool& ok, std::string& error);

// ── Worker 侧（main.cc parse-tu-worker 使用）─────────────────────────────────

/// 将 WorkerRequest protobuf 二进制数据反序列化为 TUEntry
/// @param data protobuf 二进制数据
/// @param tu 输出的 TUEntry 结构
/// @return true = 反序列化成功
bool DeserializeTURequestPb(const std::string& data, TUEntry& tu);

/// 将 CombinedOutput 序列化为 WorkerResponse protobuf 二进制数据
/// @param out 分析输出结构
/// @param ok 分析是否成功
/// @param error 失败时的错误信息
/// @return 序列化后的 protobuf 二进制字符串
std::string SerializeWorkerResponsePb(const CombinedOutput& out, bool ok,
                                       const std::string& error);

}  // namespace codexray
