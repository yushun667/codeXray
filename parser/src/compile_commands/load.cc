/**
 * 解析引擎 compile_commands 加载实现
 * 最小化 JSON 解析，不依赖 nlohmann（仅解析 array of { directory, file, arguments/command }）
 */

#include "compile_commands/load.h"
#include "common/logger.h"
#include "common/path_util.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace codexray {

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) return "";
  std::ostringstream os;
  os << f.rdbuf();
  return os.str();
}

// 从 s[pos] 起跳过空白，返回新位置
size_t SkipSpace(const std::string& s, size_t pos) {
  while (pos < s.size() && (std::isspace(static_cast<unsigned char>(s[pos])) || s[pos] == ','))
    ++pos;
  return pos;
}

// 解析 "key" : "value" 或 "key" : [ ... ]，返回 true 并填充 out_string 或 out_vec；否则 false
bool ParseStringValue(const std::string& s, size_t* pos, std::string* out) {
  size_t p = SkipSpace(s, *pos);
  if (p >= s.size() || s[p] != '"') return false;
  ++p;
  out->clear();
  while (p < s.size() && s[p] != '"') {
    if (s[p] == '\\' && p + 1 < s.size()) { ++p; out->push_back(s[p]); }
    else { out->push_back(s[p]); }
    ++p;
  }
  if (p < s.size()) ++p;
  *pos = p;
  return true;
}

// 解析 "key" : [ "a", "b", ... ]
bool ParseArgumentsArray(const std::string& s, size_t* pos, std::vector<std::string>* out) {
  size_t p = SkipSpace(s, *pos);
  if (p >= s.size() || s[p] != '[') return false;
  ++p;
  out->clear();
  while (p < s.size()) {
    p = SkipSpace(s, p);
    if (p < s.size() && s[p] == ']') { ++p; break; }
    if (p < s.size() && s[p] == '"') {
      std::string arg;
      ++p;
      while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) { ++p; arg.push_back(s[p]); }
        else arg.push_back(s[p]);
        ++p;
      }
      if (p < s.size()) ++p;
      out->push_back(std::move(arg));
    } else
      ++p;
  }
  *pos = p;
  return true;
}

// 解析 "command" 的单个字符串为参数列表（简单按空白分，引号内不拆）
void SplitCommandLine(const std::string& cmd, std::vector<std::string>* out) {
  out->clear();
  std::string cur;
  bool in_quote = false;
  char q = 0;
  for (size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (in_quote) {
      if (c == '\\' && i + 1 < cmd.size()) { cur.push_back(cmd[++i]); continue; }
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

// 在 s 中从 start 起找 "key"（带引号），然后找 ':' 后的值
bool FindKeyString(const std::string& s, size_t start, const char* key,
                  std::string* value, size_t* end_pos) {
  std::string qkey = std::string("\"") + key + "\"";
  size_t idx = s.find(qkey, start);
  if (idx == std::string::npos) return false;
  size_t colon = s.find(':', idx + qkey.size());
  if (colon == std::string::npos) return false;
  *end_pos = colon + 1;
  return ParseStringValue(s, end_pos, value);
}

bool FindKeyArguments(const std::string& s, size_t start, const char* key,
                     std::vector<std::string>* value, size_t* end_pos) {
  std::string qkey = std::string("\"") + key + "\"";
  size_t idx = s.find(qkey, start);
  if (idx == std::string::npos) return false;
  size_t colon = s.find(':', idx + qkey.size());
  if (colon == std::string::npos) return false;
  *end_pos = colon + 1;
  return ParseArgumentsArray(s, end_pos, value);
}

// 从 s[from] 起找到下一个 '}' 对应的对象开始 '{'，返回该对象子串的起始；否则 npos
size_t FindNextObjectStart(const std::string& s, size_t from) {
  size_t i = s.find('{', from);
  if (i == std::string::npos) return std::string::npos;
  return i;
}

}  // namespace

std::vector<TUEntry> LoadCompileCommands(const std::string& project_root,
                                         const std::string& path_to_cc) {
  std::string path = path_to_cc.empty()
      ? NormalizePath(project_root + "/compile_commands.json")
      : path_to_cc;
  std::string content = ReadFile(path);
  if (content.empty()) {
    LogError("LoadCompileCommands: cannot read %s", path.c_str());
    return {};
  }
  LogInfo("LoadCompileCommands: read %zu bytes from %s", content.size(), path.c_str());

  std::vector<TUEntry> result;
  size_t array_start = content.find('[');
  if (array_start == std::string::npos) {
    LogError("LoadCompileCommands: no array in JSON");
    return {};
  }
  size_t pos = array_start + 1;
  std::string dir_abs = NormalizePath(project_root);
  if (!dir_abs.empty() && dir_abs.back() != '/') dir_abs += '/';

  while (pos < content.size()) {
    pos = SkipSpace(content, pos);
    if (pos < content.size() && content[pos] == ']') break;
    size_t obj = FindNextObjectStart(content, pos);
    if (obj == std::string::npos) break;
    size_t depth = 0;
    size_t obj_end = obj;
    for (size_t i = obj; i < content.size(); ++i) {
      if (content[i] == '{') ++depth;
      else if (content[i] == '}') {
        --depth;
        if (depth == 0) { obj_end = i + 1; break; }
      }
    }
    std::string obj_str = content.substr(obj, obj_end - obj);

    std::string directory, file_val;
    std::vector<std::string> args;
    size_t p = 0;
    if (!FindKeyString(obj_str, 0, "directory", &directory, &p)) {
      pos = obj_end;
      continue;
    }
    if (!FindKeyString(obj_str, p, "file", &file_val, &p)) {
      pos = obj_end;
      continue;
    }
    std::string source_file = MakeAbsolute(directory.empty() ? project_root : directory, file_val);
    bool have_args = FindKeyArguments(obj_str, p, "arguments", &args, &p);
    if (!have_args) {
      std::string command;
          if (FindKeyString(obj_str, p, "command", &command, &p))
        SplitCommandLine(command, &args);
    }
    TUEntry entry;
    entry.source_file = NormalizePath(source_file);
    entry.compile_args = std::move(args);
    if (!entry.source_file.empty() && !entry.compile_args.empty())
      result.push_back(std::move(entry));
    pos = obj_end;
  }

  LogInfo("LoadCompileCommands: loaded %zu TU entries", result.size());
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
          if (rel == pn || (rel.size() > pn.size() && rel[pn.size()] == '/' && rel.compare(0, pn.size(), pn) == 0)) {
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
