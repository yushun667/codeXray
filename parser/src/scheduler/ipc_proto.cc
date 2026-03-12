/**
 * IPC Protobuf 转换函数与帧读写实现。
 * 使用 ipc.proto 生成的 WorkerRequest / WorkerResponse 消息类型，
 * 实现 CombinedOutput ↔ Protobuf 双向转换和长度前缀二进制帧 I/O。
 */
#include "ipc_proto.h"
#include "ipc.pb.h"
#include "../common/logger.h"
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

namespace codexray {

// ─── 帧 I/O ─────────────────────────────────────────────────────────────────

/// 帧最大长度上限：512MB（防止损坏帧导致巨大内存分配）
static constexpr uint32_t kMaxFrameSize = 512u * 1024u * 1024u;

/// 循环写入全部字节到 fd，处理 partial write
/// @return false 表示管道错误
static bool WriteAll(int fd, const void* buf, size_t len) {
  const char* p = static_cast<const char*>(buf);
  size_t written = 0;
  while (written < len) {
    ssize_t n = ::write(fd, p + written, len - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;  // EPIPE 或其他错误
    }
    if (n == 0) return false;
    written += static_cast<size_t>(n);
  }
  return true;
}

/// 循环读取全部字节从 fd，可选超时
/// @param timeout_ms 超时毫秒，-1 = 无限等待
/// @param timed_out 输出：是否超时
/// @return 实际读取的字节数，< len 表示 EOF 或超时
static size_t ReadAll(int fd, void* buf, size_t len,
                      int timeout_ms, bool& timed_out) {
  char* p = static_cast<char*>(buf);
  size_t total = 0;
  timed_out = false;
  while (total < len) {
    if (timeout_ms >= 0) {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int pr = ::poll(&pfd, 1, timeout_ms);
      if (pr == 0) { timed_out = true; return total; }  // 超时
      if (pr < 0) {
        if (errno == EINTR) continue;
        return total;  // poll 错误
      }
      // POLLHUP / POLLERR → 尝试读取以获取 EOF
    }
    ssize_t n = ::read(fd, p + total, len - total);
    if (n < 0) {
      if (errno == EINTR) continue;
      return total;  // 读取错误
    }
    if (n == 0) return total;  // EOF
    total += static_cast<size_t>(n);
  }
  return total;
}

bool WriteFrame(int fd, const std::string& data) {
  uint32_t len = static_cast<uint32_t>(data.size());
  // 写入 4 字节 LE 长度头
  if (!WriteAll(fd, &len, sizeof(len))) return false;
  // 写入 payload
  if (len > 0 && !WriteAll(fd, data.data(), len)) return false;
  return true;
}

bool ReadFrame(int fd, std::string& out, int timeout_secs, bool& timed_out) {
  int timeout_ms = (timeout_secs > 0) ? (timeout_secs * 1000) : -1;
  timed_out = false;
  out.clear();

  // 读取 4 字节长度头
  uint32_t len = 0;
  bool to = false;
  size_t got = ReadAll(fd, &len, sizeof(len), timeout_ms, to);
  if (to) { timed_out = true; return false; }
  if (got < sizeof(len)) return false;  // EOF

  // 验证长度
  if (len > kMaxFrameSize) {
    LogError("ReadFrame: frame too large: " + std::to_string(len));
    return false;
  }

  // 读取 payload
  out.resize(len);
  if (len > 0) {
    got = ReadAll(fd, &out[0], len, timeout_ms, to);
    if (to) { timed_out = true; return false; }
    if (got < len) return false;  // 不完整帧（EOF 中途）
  }
  return true;
}

// ─── TUEntry → WorkerRequest 序列化 ─────────────────────────────────────────

std::string SerializeTURequestPb(const TUEntry& tu) {
  codexray::ipc::WorkerRequest req;
  req.set_source_file(tu.source_file);
  req.set_directory(tu.directory);
  for (const auto& arg : tu.arguments) {
    req.add_arguments(arg);
  }
  std::string out;
  req.SerializeToString(&out);
  return out;
}

bool DeserializeTURequestPb(const std::string& data, TUEntry& tu) {
  codexray::ipc::WorkerRequest req;
  if (!req.ParseFromString(data)) return false;
  tu.source_file = req.source_file();
  tu.directory = req.directory();
  tu.arguments.clear();
  tu.arguments.reserve(req.arguments_size());
  for (int i = 0; i < req.arguments_size(); ++i) {
    tu.arguments.push_back(req.arguments(i));
  }
  return true;
}

// ─── CombinedOutput → WorkerResponse 序列化 ──────────────────────────────────

std::string SerializeWorkerResponsePb(const CombinedOutput& out, bool ok,
                                       const std::string& error) {
  codexray::ipc::WorkerResponse resp;
  resp.set_ok(ok);
  if (!error.empty()) resp.set_error(error);
  resp.set_source_file_mtime(out.source_file_mtime);
  resp.set_source_file_hash(out.source_file_hash);

  // symbols
  for (const auto& r : out.symbols) {
    auto* pb = resp.add_symbols();
    pb->set_usr(r.usr);
    pb->set_name(r.name);
    pb->set_qualified_name(r.qualified_name);
    pb->set_kind(r.kind);
    pb->set_def_file_path(r.def_file_path);
    pb->set_def_file_id(r.def_file_id);
    pb->set_def_line(r.def_line);
    pb->set_def_column(r.def_column);
    pb->set_def_line_end(r.def_line_end);
    pb->set_def_col_end(r.def_col_end);
    pb->set_decl_file_path(r.decl_file_path);
    pb->set_decl_file_id(r.decl_file_id);
    pb->set_decl_line(r.decl_line);
    pb->set_decl_column(r.decl_column);
    pb->set_decl_line_end(r.decl_line_end);
    pb->set_decl_col_end(r.decl_col_end);
  }

  // call_edges
  for (const auto& r : out.call_edges) {
    auto* pb = resp.add_call_edges();
    pb->set_caller_usr(r.caller_usr);
    pb->set_callee_usr(r.callee_usr);
    pb->set_edge_type(r.edge_type);
    pb->set_call_file_path(r.call_file_path);
    pb->set_call_file_id(r.call_file_id);
    pb->set_call_line(r.call_line);
    pb->set_call_column(r.call_column);
  }

  // classes
  for (const auto& r : out.classes) {
    auto* pb = resp.add_classes();
    pb->set_usr(r.usr);
    pb->set_name(r.name);
    pb->set_qualified_name(r.qualified_name);
    pb->set_def_file_path(r.def_file_path);
    pb->set_def_file_id(r.def_file_id);
    pb->set_def_line(r.def_line);
    pb->set_def_column(r.def_column);
    pb->set_def_line_end(r.def_line_end);
    pb->set_def_col_end(r.def_col_end);
  }

  // class_relations
  for (const auto& r : out.class_relations) {
    auto* pb = resp.add_class_relations();
    pb->set_parent_usr(r.parent_usr);
    pb->set_child_usr(r.child_usr);
    pb->set_relation_type(r.relation_type);
  }

  // class_members
  for (const auto& r : out.class_members) {
    auto* pb = resp.add_class_members();
    pb->set_class_usr(r.class_usr);
    pb->set_member_usr(r.member_usr);
    pb->set_member_name(r.member_name);
    pb->set_member_type_str(r.member_type_str);
  }

  // global_vars
  for (const auto& r : out.global_vars) {
    auto* pb = resp.add_global_vars();
    pb->set_usr(r.usr);
    pb->set_name(r.name);
    pb->set_qualified_name(r.qualified_name);
    pb->set_def_file_path(r.def_file_path);
    pb->set_def_file_id(r.def_file_id);
    pb->set_def_line(r.def_line);
    pb->set_def_column(r.def_column);
    pb->set_def_line_end(r.def_line_end);
    pb->set_def_col_end(r.def_col_end);
  }

  // data_flow_edges
  for (const auto& r : out.data_flow_edges) {
    auto* pb = resp.add_data_flow_edges();
    pb->set_var_usr(r.var_usr);
    pb->set_accessor_usr(r.accessor_usr);
    pb->set_access_type(r.access_type);
    pb->set_access_file_path(r.access_file_path);
    pb->set_access_file_id(r.access_file_id);
    pb->set_access_line(r.access_line);
    pb->set_access_column(r.access_column);
  }

  // cfg_nodes
  for (const auto& r : out.cfg_nodes) {
    auto* pb = resp.add_cfg_nodes();
    pb->set_function_usr(r.function_usr);
    pb->set_block_id(r.block_id);
    pb->set_file_path(r.file_path);
    pb->set_file_id(r.file_id);
    pb->set_begin_line(r.begin_line);
    pb->set_begin_col(r.begin_col);
    pb->set_end_line(r.end_line);
    pb->set_end_col(r.end_col);
    pb->set_label(r.label);
  }

  // cfg_edges
  for (const auto& r : out.cfg_edges) {
    auto* pb = resp.add_cfg_edges();
    pb->set_function_usr(r.function_usr);
    pb->set_from_block(r.from_block);
    pb->set_to_block(r.to_block);
    pb->set_edge_type(r.edge_type);
  }

  // referenced_files
  for (const auto& f : out.referenced_files) {
    resp.add_referenced_files(f);
  }

  std::string result;
  resp.SerializeToString(&result);
  return result;
}

// ─── WorkerResponse → CombinedOutput 反序列化 ────────────────────────────────

bool DeserializeWorkerResponsePb(const std::string& data, CombinedOutput& out,
                                  bool& ok, std::string& error) {
  codexray::ipc::WorkerResponse resp;
  if (!resp.ParseFromString(data)) return false;

  ok = resp.ok();
  error = resp.error();
  out.source_file_mtime = resp.source_file_mtime();
  out.source_file_hash = resp.source_file_hash();

  // symbols
  out.symbols.reserve(resp.symbols_size());
  for (const auto& pb : resp.symbols()) {
    SymbolRow r;
    r.usr = pb.usr();
    r.name = pb.name();
    r.qualified_name = pb.qualified_name();
    r.kind = pb.kind();
    r.def_file_path = pb.def_file_path();
    r.def_file_id = pb.def_file_id();
    r.def_line = pb.def_line();
    r.def_column = pb.def_column();
    r.def_line_end = pb.def_line_end();
    r.def_col_end = pb.def_col_end();
    r.decl_file_path = pb.decl_file_path();
    r.decl_file_id = pb.decl_file_id();
    r.decl_line = pb.decl_line();
    r.decl_column = pb.decl_column();
    r.decl_line_end = pb.decl_line_end();
    r.decl_col_end = pb.decl_col_end();
    out.symbols.push_back(std::move(r));
  }

  // call_edges
  out.call_edges.reserve(resp.call_edges_size());
  for (const auto& pb : resp.call_edges()) {
    CallEdgeRow r;
    r.caller_usr = pb.caller_usr();
    r.callee_usr = pb.callee_usr();
    r.edge_type = pb.edge_type();
    r.call_file_path = pb.call_file_path();
    r.call_file_id = pb.call_file_id();
    r.call_line = pb.call_line();
    r.call_column = pb.call_column();
    out.call_edges.push_back(std::move(r));
  }

  // classes
  out.classes.reserve(resp.classes_size());
  for (const auto& pb : resp.classes()) {
    ClassRow r;
    r.usr = pb.usr();
    r.name = pb.name();
    r.qualified_name = pb.qualified_name();
    r.def_file_path = pb.def_file_path();
    r.def_file_id = pb.def_file_id();
    r.def_line = pb.def_line();
    r.def_column = pb.def_column();
    r.def_line_end = pb.def_line_end();
    r.def_col_end = pb.def_col_end();
    out.classes.push_back(std::move(r));
  }

  // class_relations
  out.class_relations.reserve(resp.class_relations_size());
  for (const auto& pb : resp.class_relations()) {
    ClassRelationRow r;
    r.parent_usr = pb.parent_usr();
    r.child_usr = pb.child_usr();
    r.relation_type = pb.relation_type();
    out.class_relations.push_back(std::move(r));
  }

  // class_members
  out.class_members.reserve(resp.class_members_size());
  for (const auto& pb : resp.class_members()) {
    ClassMemberRow r;
    r.class_usr = pb.class_usr();
    r.member_usr = pb.member_usr();
    r.member_name = pb.member_name();
    r.member_type_str = pb.member_type_str();
    out.class_members.push_back(std::move(r));
  }

  // global_vars
  out.global_vars.reserve(resp.global_vars_size());
  for (const auto& pb : resp.global_vars()) {
    GlobalVarRow r;
    r.usr = pb.usr();
    r.name = pb.name();
    r.qualified_name = pb.qualified_name();
    r.def_file_path = pb.def_file_path();
    r.def_file_id = pb.def_file_id();
    r.def_line = pb.def_line();
    r.def_column = pb.def_column();
    r.def_line_end = pb.def_line_end();
    r.def_col_end = pb.def_col_end();
    out.global_vars.push_back(std::move(r));
  }

  // data_flow_edges
  out.data_flow_edges.reserve(resp.data_flow_edges_size());
  for (const auto& pb : resp.data_flow_edges()) {
    DataFlowEdgeRow r;
    r.var_usr = pb.var_usr();
    r.accessor_usr = pb.accessor_usr();
    r.access_type = pb.access_type();
    r.access_file_path = pb.access_file_path();
    r.access_file_id = pb.access_file_id();
    r.access_line = pb.access_line();
    r.access_column = pb.access_column();
    out.data_flow_edges.push_back(std::move(r));
  }

  // cfg_nodes
  out.cfg_nodes.reserve(resp.cfg_nodes_size());
  for (const auto& pb : resp.cfg_nodes()) {
    CfgNodeRow r;
    r.function_usr = pb.function_usr();
    r.block_id = pb.block_id();
    r.file_path = pb.file_path();
    r.file_id = pb.file_id();
    r.begin_line = pb.begin_line();
    r.begin_col = pb.begin_col();
    r.end_line = pb.end_line();
    r.end_col = pb.end_col();
    r.label = pb.label();
    out.cfg_nodes.push_back(std::move(r));
  }

  // cfg_edges
  out.cfg_edges.reserve(resp.cfg_edges_size());
  for (const auto& pb : resp.cfg_edges()) {
    CfgEdgeRow r;
    r.function_usr = pb.function_usr();
    r.from_block = pb.from_block();
    r.to_block = pb.to_block();
    r.edge_type = pb.edge_type();
    out.cfg_edges.push_back(std::move(r));
  }

  // referenced_files
  out.referenced_files.reserve(resp.referenced_files_size());
  for (int i = 0; i < resp.referenced_files_size(); ++i) {
    out.referenced_files.push_back(resp.referenced_files(i));
  }

  return true;
}

}  // namespace codexray
