/**
 * 解析引擎公共模块：路径工具实现
 */

#include "common/path_util.h"
#include <algorithm>
#include <sstream>

#ifdef _WIN32
#define CODEXRAY_IS_WIN 1
#else
#define CODEXRAY_IS_WIN 0
#endif

namespace codexray {

namespace {

const char kSep = '/';
#if CODEXRAY_IS_WIN
const char kSepAlt = '\\';
#else
const char kSepAlt = '/';
#endif

std::string NormalizeSep(std::string path) {
  for (char& c : path) {
    if (c == kSepAlt && kSepAlt != kSep) c = kSep;
  }
  return path;
}

std::vector<std::string> SplitPath(const std::string& path) {
  std::vector<std::string> parts;
  std::string s = NormalizeSep(path);
  if (s.empty()) return parts;
  size_t i = 0;
  if (s[0] == kSep) {
    parts.push_back("/");
    i = 1;
  }
  while (i < s.size()) {
    size_t j = s.find(kSep, i);
    if (j == std::string::npos) j = s.size();
    if (j > i) {
      std::string seg = s.substr(i, j - i);
      if (seg == "..") {
        if (!parts.empty() && parts.back() != "..") {
          if (parts.back() == "/") {
            /* 根下的 .. 仍为根 */
          } else {
            parts.pop_back();
          }
        } else {
          parts.push_back(seg);
        }
      } else if (seg != ".") {
        parts.push_back(seg);
      }
    }
    i = j + 1;
  }
  return parts;
}

std::string JoinPath(const std::vector<std::string>& parts) {
  if (parts.empty()) return "";
  std::string r = parts[0];
  for (size_t i = 1; i < parts.size(); ++i) {
    if (r.back() != kSep) r += kSep;
    r += parts[i];
  }
  return r;
}

}  // namespace

std::string NormalizePath(const std::string& path) {
  if (path.empty()) return path;
  std::vector<std::string> parts = SplitPath(path);
  std::string result = JoinPath(parts);
  if (result.size() > 1 && result.back() == kSep) result.pop_back();
  return result;
}

std::string MakeAbsolute(const std::string& base, const std::string& rel) {
  if (rel.empty()) return base;
  std::string r = NormalizeSep(rel);
  if (r[0] == kSep) return NormalizePath(r);
  std::string b = NormalizePath(base);
  if (b.empty()) return NormalizePath(rel);
  if (b.back() != kSep) b += kSep;
  return NormalizePath(b + r);
}

bool PathStartsWith(const std::string& path, const std::string& prefix) {
  std::string p = NormalizePath(path);
  std::string pre = NormalizePath(prefix);
  if (pre.empty()) return true;
  if (p.size() < pre.size()) return false;
  if (p.compare(0, pre.size(), pre) != 0) return false;
  return p.size() == pre.size() || p[pre.size()] == kSep;
}

std::string ToProjectRelative(const std::string& path,
                              const std::string& project_root) {
  std::string p = NormalizePath(path);
  std::string root = NormalizePath(project_root);
  if (root.empty()) return p;
  if (!PathStartsWith(p, root)) return p;
  if (p.size() == root.size()) return ".";
  if (p[root.size()] != kSep) return p;
  std::string rel = p.substr(root.size() + 1);
  return rel.empty() ? "." : rel;
}

}  // namespace codexray
