/**
 * 解析引擎 compile_commands 加载实现
 * 使用 nlohmann/json 解析 JSON，禁止手写 JSON 解析逻辑。
 * 格式：JSON Compilation Database，https://clang.llvm.org/docs/JSONCompilationDatabase.html
 */

#include "compile_commands/load.h"
#include "common/logger.h"
#include "common/path_util.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cctype>
#include <iostream>

namespace codexray {

namespace {

using json = nlohmann::json;

/** 将 "command" 字符串拆成 argv 风格列表；只取第一个 ';' 前的部分。非 JSON 解析。 */
void SplitCommandLine(const std::string& cmd, std::vector<std::string>* out) {
  out->clear();
  std::string segment = cmd;
  size_t semi = segment.find(';');
  if (semi != std::string::npos) segment.resize(semi);
  while (!segment.empty() && (segment.back() == ' ' || segment.back() == '\t'))
    segment.pop_back();
  std::string cur;
  bool in_quote = false;
  char q = 0;
  for (size_t i = 0; i < segment.size(); ++i) {
    char c = segment[i];
    if (in_quote) {
      if (c == '\\' && i + 1 < segment.size()) { cur.push_back(segment[++i]); continue; }
      if (c == q) { in_quote = false; q = 0; continue; }
      cur.push_back(c);
      continue;
    }
    if (c == '"' || c == '\'') { in_quote = true; q = c; continue; }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) { out->push_back(std::move(cur)); cur.clear(); }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) out->push_back(std::move(cur));
}

static std::string TrimCopy(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

/** Clang 要求一次只传一个 compiler job；截断在 ";" 或第二个 "-cc1" 处。非 JSON 解析。 */
void TruncateToFirstJob(std::vector<std::string>* args) {
  if (!args || args->empty()) return;
  int cc1_count = 0;
  for (size_t i = 0; i < args->size(); ++i) {
    std::string t = TrimCopy((*args)[i]);
    if (t == ";" || (!t.empty() && t[0] == ';')) {
      args->resize(i);
      return;
    }
    if (t == "-cc1") {
      ++cc1_count;
      if (cc1_count >= 2) {
        args->resize(i);
        return;
      }
    }
  }
}

/** 解析仅做语法/语义分析，不生成代码；若参数中尚无 -fsyntax-only 则追加。 */
void AppendFsyntaxOnly(std::vector<std::string>* args) {
  if (!args) return;
  for (const std::string& a : *args)
    if (a == "-fsyntax-only") return;
  args->push_back("-fsyntax-only");
}

}  // namespace

std::vector<TUEntry> LoadCompileCommands(const std::string& project_root,
                                         const std::string& path_to_cc) {
  std::string path = path_to_cc.empty()
      ? NormalizePath(project_root + "/compile_commands.json")
      : path_to_cc;
  json j;
  try {
    std::ifstream f(path);
    if (!f) {
      LogError("LoadCompileCommands: cannot open %s", path.c_str());
      std::cerr << "LoadCompileCommands: could not read file: " << path << "\n";
      return {};
    }
    f >> j;
  } catch (const json::exception& e) {
    LogError("LoadCompileCommands: JSON parse error %s", e.what());
    std::cerr << "LoadCompileCommands: JSON parse error: " << e.what() << "\n";
    return {};
  }

  if (!j.is_array()) {
    LogError("LoadCompileCommands: root is not an array");
    std::cerr << "LoadCompileCommands: JSON must have a top-level array\n";
    return {};
  }

  std::vector<TUEntry> result;
  for (const auto& obj : j) {
    if (!obj.is_object()) continue;
    if (!obj.contains("directory") || !obj.contains("file")) continue;

    std::string directory = obj["directory"].get<std::string>();
    std::string file_val = obj["file"].get<std::string>();
    std::string source_file = MakeAbsolute(
        directory.empty() ? project_root : directory, file_val);

    std::vector<std::string> args;
    if (obj.contains("arguments") && obj["arguments"].is_array()) {
      for (const auto& a : obj["arguments"])
        args.push_back(a.get<std::string>());
      TruncateToFirstJob(&args);
    } else if (obj.contains("command") && obj["command"].is_string()) {
      std::string command = obj["command"].get<std::string>();
      SplitCommandLine(command, &args);
      TruncateToFirstJob(&args);
    }
    if (args.empty()) continue;

    std::string norm_source = NormalizePath(source_file);
    if (args.size() >= 2 && args[args.size() - 2] == "-c" &&
        NormalizePath(args.back()) == norm_source)
      args.pop_back();

    AppendFsyntaxOnly(&args);

    TUEntry entry;
    entry.source_file = norm_source;
    entry.compile_args = std::move(args);
    entry.working_directory = NormalizePath(directory.empty() ? project_root : directory);
    result.push_back(std::move(entry));
  }

  LogInfo("LoadCompileCommands: loaded %zu TU entries", result.size());
  if (result.empty())
    std::cerr << "LoadCompileCommands: no valid entries (need \"directory\", \"file\", and \"arguments\" or \"command\")\n";
  return result;
}

void SplitByPriorityDirs(const std::vector<TUEntry>& all,
                         const std::string& project_root,
                         const std::vector<std::string>& priority_dirs,
                         std::vector<TUEntry>* priority,
                         std::vector<TUEntry>* rest) {
  if (priority) priority->clear();
  if (rest) rest->clear();
  std::string root_n = NormalizePath(project_root);
  for (const TUEntry& e : all) {
    std::string rel = ToProjectRelative(e.source_file, root_n);
    bool in_priority = false;
    for (const std::string& pd : priority_dirs) {
      std::string pn = NormalizePath(pd);
      if (pn.empty()) continue;
      if (rel == pn ||
          (rel.size() > pn.size() && rel[pn.size()] == '/' && rel.compare(0, pn.size(), pn) == 0)) {
        in_priority = true;
        break;
      }
    }
    if (in_priority) {
      if (priority) priority->push_back(e);
    } else {
      if (rest) rest->push_back(e);
    }
  }
}

}  // namespace codexray
