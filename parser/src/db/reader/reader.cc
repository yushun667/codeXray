#include "reader.h"
#include "../../common/logger.h"
#include "../../common/path_util.h"
#include "cfg.pb.h"
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <queue>

namespace codexray {

namespace {

// Bind text helper
static void BindText(sqlite3_stmt* s, int idx, const std::string& v) {
  sqlite3_bind_text(s, idx, v.c_str(), -1, SQLITE_TRANSIENT);
}

// Safe column text
static std::string ColText(sqlite3_stmt* s, int col) {
  const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
  return p ? std::string(p) : std::string();
}

// Lookup file path by id (simple cache via unordered_map passed in)
static std::string FilePath(sqlite3* db, int64_t fid,
                             std::unordered_map<int64_t, std::string>& cache) {
  if (fid <= 0) return "";
  auto it = cache.find(fid);
  if (it != cache.end()) return it->second;
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, "SELECT path FROM file WHERE id=?", -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, fid);
  std::string path;
  if (sqlite3_step(s) == SQLITE_ROW) path = ColText(s, 0);
  sqlite3_finalize(s);
  cache[fid] = path;
  return path;
}

// Resolve symbol by USR, name, or qualified_name, return DB id.
// 策略：1. USR 精确匹配；
//       2. name + def_file_id JOIN；
//       3. qualified_name + def_file_id JOIN（支持 "llvm::foo" 等全限定名）；
//       4. 从全限定名提取短名再匹配 name + file；
//       5. USR 中包含文件名 fallback；
//       6. 无文件过滤的 name / qualified_name 匹配。
static int64_t ResolveSymbolId(sqlite3* db,
                                const std::string& usr_or_name,
                                const std::string& file_path) {
  if (usr_or_name.empty()) return 0;

  // 辅助 lambda：执行单条查询返回 ID
  auto queryOne = [&](const char* sql, const std::string& p1,
                      const std::string& p2 = "") -> int64_t {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
    BindText(s, 1, p1);
    if (!p2.empty()) BindText(s, 2, p2);
    int64_t id = 0;
    if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    return id;
  };

  // 1. USR 精确匹配
  if (usr_or_name.size() > 2 && usr_or_name[0] == 'c' && usr_or_name[1] == ':') {
    int64_t id = queryOne("SELECT id FROM symbol WHERE usr=?", usr_or_name);
    if (id) return id;
  }

  // 判断输入是否为全限定名（包含 ::）
  const bool is_qualified = usr_or_name.find("::") != std::string::npos;
  // 从全限定名提取短名（最后一个 :: 之后的部分）
  std::string short_name;
  if (is_qualified) {
    auto dpos = usr_or_name.rfind("::");
    if (dpos != std::string::npos && dpos + 2 < usr_or_name.size())
      short_name = usr_or_name.substr(dpos + 2);
  }

  const std::string file_like = "%" + file_path + "%";

  if (!file_path.empty()) {
    // 2. name + def_file_id
    int64_t id = queryOne(
        "SELECT s.id FROM symbol s JOIN file f ON s.def_file_id=f.id"
        " WHERE s.name=? AND f.path LIKE ? LIMIT 1",
        usr_or_name, file_like);
    if (id) return id;

    // 3. qualified_name + def_file_id（处理传入 "llvm::hasAssumption" 等情况）
    if (is_qualified) {
      id = queryOne(
          "SELECT s.id FROM symbol s JOIN file f ON s.def_file_id=f.id"
          " WHERE s.qualified_name=? AND f.path LIKE ? LIMIT 1",
          usr_or_name, file_like);
      if (id) return id;
    }

    // 4. 从全限定名提取短名，用短名 + file 匹配
    if (!short_name.empty()) {
      id = queryOne(
          "SELECT s.id FROM symbol s JOIN file f ON s.def_file_id=f.id"
          " WHERE s.name=? AND f.path LIKE ? LIMIT 1",
          short_name, file_like);
      if (id) return id;
    }

    // 5. 兜底：按 decl_file_id 查找（def_file_id 为空）
    id = queryOne(
        "SELECT s.id FROM symbol s JOIN file f ON s.decl_file_id=f.id"
        " WHERE s.name=? AND f.path LIKE ? LIMIT 1",
        is_qualified ? (short_name.empty() ? usr_or_name : short_name) : usr_or_name,
        file_like);
    if (id) return id;

    // 6. USR 中包含文件名的兜底匹配
    std::string basename = file_path;
    auto pos = basename.rfind('/');
    if (pos != std::string::npos) basename = basename.substr(pos + 1);
    if (!basename.empty()) {
      const std::string& match_name = short_name.empty() ? usr_or_name : short_name;
      id = queryOne(
          "SELECT id FROM symbol WHERE name=? AND usr LIKE ? LIMIT 1",
          match_name, "%" + basename + "%");
      if (id) return id;
    }
  }

  // 7. 无文件过滤：先试 name，再试 qualified_name
  int64_t id = queryOne("SELECT id FROM symbol WHERE name=? LIMIT 1", usr_or_name);
  if (!id && is_qualified) {
    id = queryOne("SELECT id FROM symbol WHERE qualified_name=? LIMIT 1", usr_or_name);
  }
  if (!id && !short_name.empty()) {
    id = queryOne("SELECT id FROM symbol WHERE name=? LIMIT 1", short_name);
  }
  return id;
}

/// 加载符号节点信息。使用 COALESCE 兜底：当 def_* 为 0/NULL 时回退到 decl_* 字段，
/// 确保即使解析器未正确记录定义位置，也能从声明位置获取可用的跳转目标。
static QueryNode LoadSymbolNode(sqlite3* db, int64_t sym_id,
                                 std::unordered_map<int64_t, std::string>& fc) {
  QueryNode n{};
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db,
      "SELECT id, usr, name, qualified_name, kind,"
      " COALESCE(NULLIF(def_file_id,0), decl_file_id) AS file_id,"
      " COALESCE(NULLIF(def_line,0), decl_line) AS line,"
      " COALESCE(NULLIF(def_column,0), decl_column) AS col,"
      " COALESCE(NULLIF(def_line_end,0), decl_line_end) AS line_end,"
      " COALESCE(NULLIF(def_col_end,0), decl_col_end) AS col_end"
      " FROM symbol WHERE id=?", -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, sym_id);
  if (sqlite3_step(s) == SQLITE_ROW) {
    n.id             = sqlite3_column_int64(s, 0);
    n.usr            = ColText(s, 1);
    n.name           = ColText(s, 2);
    n.qualified_name = ColText(s, 3);
    n.kind           = ColText(s, 4);
    n.def_file       = FilePath(db, sqlite3_column_int64(s, 5), fc);
    n.def_line       = sqlite3_column_int(s, 6);
    n.def_column     = sqlite3_column_int(s, 7);
    n.def_line_end   = sqlite3_column_int(s, 8);
    n.def_col_end    = sqlite3_column_int(s, 9);
  }
  sqlite3_finalize(s);
  return n;
}

}  // namespace

// ─── project helpers ─────────────────────────────────────────────────────────

int64_t GetProjectId(sqlite3* db, const std::string& root_path) {
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, "SELECT id FROM project WHERE root_path=?", -1, &s, nullptr);
  BindText(s, 1, root_path);
  int64_t id = 0;
  if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
  sqlite3_finalize(s);
  return id;
}

int64_t EnsureProjectId(sqlite3* db, const std::string& root_path,
                        const std::string& cc_path) {
  int64_t id = GetProjectId(db, root_path);
  if (id) return id;
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db,
      "INSERT OR IGNORE INTO project(root_path, compile_commands_path) VALUES(?,?)",
      -1, &s, nullptr);
  BindText(s, 1, root_path);
  BindText(s, 2, cc_path);
  sqlite3_step(s);
  sqlite3_finalize(s);
  return GetProjectId(db, root_path);
}

int64_t QueryFileIdByPath(sqlite3* db, int64_t project_id, const std::string& path) {
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db,
      "SELECT id FROM file WHERE project_id=? AND path=?", -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, project_id);
  BindText(s, 2, path);
  int64_t id = 0;
  if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
  sqlite3_finalize(s);
  return id;
}

std::string QueryFilePathById(sqlite3* db, int64_t file_id) {
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, "SELECT path FROM file WHERE id=?", -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, file_id);
  std::string p;
  if (sqlite3_step(s) == SQLITE_ROW) p = ColText(s, 0);
  sqlite3_finalize(s);
  return p;
}

bool IsFileParsed(sqlite3* db, int64_t project_id, int64_t file_id) {
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db,
      "SELECT 1 FROM parsed_file WHERE project_id=? AND file_id=?", -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, project_id);
  sqlite3_bind_int64(s, 2, file_id);
  bool found = (sqlite3_step(s) == SQLITE_ROW);
  sqlite3_finalize(s);
  return found;
}

// ─── symbol by location ───────────────────────────────────────────────────────

/// 按文件+行号查询符号。优先查 def_file_id 定义位置，
/// 若无结果则回退查 decl_file_id 声明位置（兜底旧数据中 def_file_id 为空的情况）。
std::vector<SymbolRow> QuerySymbolsByFileAndLine(sqlite3* db, int64_t file_id,
                                                  int line, int column) {
  std::vector<SymbolRow> result;
  // 优先：按定义位置范围 [def_line, def_line_end] 查询
  const char* sql_def =
      "SELECT usr, name, qualified_name, kind, def_file_id, def_line, def_column,"
      " def_line_end, def_col_end FROM symbol"
      " WHERE def_file_id=? AND def_line <= ? AND def_line_end >= ?"
      " ORDER BY (def_line_end - def_line) ASC LIMIT 5";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql_def, -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, file_id);
  sqlite3_bind_int(s, 2, line);
  sqlite3_bind_int(s, 3, line);
  while (sqlite3_step(s) == SQLITE_ROW) {
    SymbolRow r;
    r.usr            = ColText(s, 0);
    r.name           = ColText(s, 1);
    r.qualified_name = ColText(s, 2);
    r.kind           = ColText(s, 3);
    r.def_file_id    = sqlite3_column_int64(s, 4);
    r.def_line       = sqlite3_column_int(s, 5);
    r.def_column     = sqlite3_column_int(s, 6);
    r.def_line_end   = sqlite3_column_int(s, 7);
    r.def_col_end    = sqlite3_column_int(s, 8);
    result.push_back(r);
  }
  sqlite3_finalize(s);

  // 兜底：若按定义位置未找到，尝试按声明位置查找（处理 def_file_id 为空的旧数据）
  if (result.empty()) {
    const char* sql_decl =
        "SELECT usr, name, qualified_name, kind, decl_file_id, decl_line, decl_column,"
        " decl_line_end, decl_col_end FROM symbol"
        " WHERE decl_file_id=? AND decl_line <= ? AND decl_line_end >= ?"
        " ORDER BY (decl_line_end - decl_line) ASC LIMIT 5";
    sqlite3_stmt* s2 = nullptr;
    sqlite3_prepare_v2(db, sql_decl, -1, &s2, nullptr);
    sqlite3_bind_int64(s2, 1, file_id);
    sqlite3_bind_int(s2, 2, line);
    sqlite3_bind_int(s2, 3, line);
    while (sqlite3_step(s2) == SQLITE_ROW) {
      SymbolRow r;
      r.usr            = ColText(s2, 0);
      r.name           = ColText(s2, 1);
      r.qualified_name = ColText(s2, 2);
      r.kind           = ColText(s2, 3);
      // 使用 decl 字段填充 def 字段，使上层调用者无需区分
      r.def_file_id    = sqlite3_column_int64(s2, 4);
      r.def_line       = sqlite3_column_int(s2, 5);
      r.def_column     = sqlite3_column_int(s2, 6);
      r.def_line_end   = sqlite3_column_int(s2, 7);
      r.def_col_end    = sqlite3_column_int(s2, 8);
      result.push_back(r);
    }
    sqlite3_finalize(s2);
  }
  return result;
}

std::vector<QueryNode> QuerySymbolsByName(sqlite3* db, const std::string& name,
                                           int limit) {
  std::vector<QueryNode> result;
  std::unordered_map<int64_t, std::string> fc;
  const char* sql =
      "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,"
      "def_line_end,def_col_end FROM symbol WHERE name LIKE ? LIMIT ?";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
  BindText(s, 1, "%" + name + "%");
  sqlite3_bind_int(s, 2, limit);
  while (sqlite3_step(s) == SQLITE_ROW) {
    QueryNode n;
    n.id             = sqlite3_column_int64(s, 0);
    n.usr            = ColText(s, 1);
    n.name           = ColText(s, 2);
    n.qualified_name = ColText(s, 3);
    n.kind           = ColText(s, 4);
    n.def_file       = FilePath(db, sqlite3_column_int64(s, 5), fc);
    n.def_line       = sqlite3_column_int(s, 6);
    n.def_column     = sqlite3_column_int(s, 7);
    n.def_line_end   = sqlite3_column_int(s, 8);
    n.def_col_end    = sqlite3_column_int(s, 9);
    result.push_back(n);
  }
  sqlite3_finalize(s);
  return result;
}

// ─── call graph BFS ───────────────────────────────────────────────────────────

/// 判断某个符号路径是否属于系统/外部库路径（应完全排除出结果）
static bool IsSystemPath(const std::string& path) {
  if (path.empty()) return false;
  // macOS / Linux 系统头文件
  if (path.compare(0, 5, "/usr/") == 0)        return true;
  if (path.compare(0, 9, "/Library/") == 0)    return true;
  if (path.compare(0, 21, "/Applications/Xcode") == 0) return true;
  // SDK / toolchain 内置
  if (path.find("/clang/") != std::string::npos &&
      path.find("/include/") != std::string::npos) return true;
  return false;
}

/// 判断某个符号是否应被完全排除出调用链结果。
/// 排除类别：
///   1. 标准库符号（std::, __builtin_, __cxa_, llvm::detail:: 等命名空间）
///   2. 系统路径中定义的符号（/usr/, /Library/, Xcode SDK 等）
///   3. 无定义位置的外部符号（def_file 为空且 def_line == 0，即纯 forward decl）
///   4. 构造函数 / 析构函数（调用链噪声大，连接度极高）
///   5. operator 重载
///   6. 高频 trivial accessor（size, empty, begin, end 等）
static bool ShouldExclude(const QueryNode& node) {
  const auto& qn = node.qualified_name;
  const auto& name = node.name;

  // 1. 标准库 / 编译器内置命名空间前缀
  static const std::vector<std::string> kExcludedPrefixes = {
    "std::", "__", "llvm::detail::", "llvm::sys::detail::",
    "clang::detail::", "_LIBCPP_", "boost::",
  };
  for (const auto& pfx : kExcludedPrefixes) {
    if (qn.size() >= pfx.size() && qn.compare(0, pfx.size(), pfx) == 0) return true;
    if (name.size() >= pfx.size() && name.compare(0, pfx.size(), pfx) == 0) return true;
  }

  // 2. 系统路径中定义的符号
  if (IsSystemPath(node.def_file)) return true;

  // 3. 无定义位置的外部符号（forward decl / 外部库接口）
  if (node.def_file.empty() && node.def_line == 0) return true;

  // 4. 构造函数 / 析构函数
  if (node.kind == "constructor" || node.kind == "destructor") return true;

  // 5. operator 重载
  if (name.size() >= 8 && name.compare(0, 8, "operator") == 0) return true;

  // 6. trivial accessor / 基础方法（几乎所有类都有，连接度高但无分析价值）
  static const std::unordered_set<std::string> kTrivialNames = {
    "size", "empty", "begin", "end", "cbegin", "cend",
    "rbegin", "rend", "front", "back", "data", "length",
    "capacity", "reserve", "clear", "push_back", "pop_back",
    "emplace_back", "insert", "erase", "find", "count",
    "at", "get", "swap", "resize", "append",
  };
  if (kTrivialNames.count(name)) return true;

  return false;
}

CallGraphResult QueryCallGraph(sqlite3* db, const std::string& symbol_usr_or_name,
                                const std::string& file_path, int depth,
                                CallDirection direction) {
  CallGraphResult res;
  int64_t root_id = ResolveSymbolId(db, symbol_usr_or_name, file_path);
  if (!root_id) return res;

  std::unordered_map<int64_t, std::string> fc;
  std::unordered_map<int64_t, QueryNode> node_map;
  std::unordered_set<int64_t> visited;
  std::unordered_set<int64_t> edge_set;

  // dir 字段：kBoth 仅用于根节点（第一层双向扩展），
  // 后续节点固定为 kForward 或 kBackward，只沿发现方向继续。
  struct Work { int64_t id; int depth_remaining; CallDirection dir; };
  std::queue<Work> q;
  q.push({root_id, depth, direction});
  visited.insert(root_id);
  node_map[root_id] = LoadSymbolNode(db, root_id, fc);

  while (!q.empty()) {
    auto [cur_id, rem, cur_dir] = q.front(); q.pop();
    if (rem <= 0) continue;

    const bool do_forward  = (cur_dir == CallDirection::kBoth || cur_dir == CallDirection::kForward);
    const bool do_backward = (cur_dir == CallDirection::kBoth || cur_dir == CallDirection::kBackward);

    // Forward: callees（被调用节点，图的右侧方向）
    if (do_forward) {
      sqlite3_stmt* s = nullptr;
      sqlite3_prepare_v2(db,
          "SELECT ce.id, ce.callee_id, ce.edge_type, ce.call_file_id, ce.call_line,"
          " ce.call_column FROM call_edge ce WHERE ce.caller_id=?", -1, &s, nullptr);
      sqlite3_bind_int64(s, 1, cur_id);
      while (sqlite3_step(s) == SQLITE_ROW) {
        int64_t eid     = sqlite3_column_int64(s, 0);
        int64_t callee  = sqlite3_column_int64(s, 1);
        if (edge_set.count(eid)) continue;
        if (!node_map.count(callee)) {
          node_map[callee] = LoadSymbolNode(db, callee, fc);
        }
        if (ShouldExclude(node_map[callee])) continue;
        edge_set.insert(eid);
        QueryEdge e;
        e.id        = eid;
        e.from_id   = cur_id;
        e.to_id     = callee;
        e.edge_type = ColText(s, 2);
        e.call_file = FilePath(db, sqlite3_column_int64(s, 3), fc);
        e.call_line   = sqlite3_column_int(s, 4);
        e.call_column = sqlite3_column_int(s, 5);
        res.edges.push_back(e);
        if (!visited.count(callee)) {
          visited.insert(callee);
          q.push({callee, rem - 1, CallDirection::kForward});
        }
      }
      sqlite3_finalize(s);
    }

    // Backward: callers（调用节点，图的左侧方向）
    if (do_backward) {
      sqlite3_stmt* s = nullptr;
      sqlite3_prepare_v2(db,
          "SELECT ce.id, ce.caller_id, ce.edge_type, ce.call_file_id, ce.call_line,"
          " ce.call_column FROM call_edge ce WHERE ce.callee_id=?", -1, &s, nullptr);
      sqlite3_bind_int64(s, 1, cur_id);
      while (sqlite3_step(s) == SQLITE_ROW) {
        int64_t eid    = sqlite3_column_int64(s, 0);
        int64_t caller = sqlite3_column_int64(s, 1);
        if (edge_set.count(eid)) continue;
        if (!node_map.count(caller)) {
          node_map[caller] = LoadSymbolNode(db, caller, fc);
        }
        if (ShouldExclude(node_map[caller])) continue;
        edge_set.insert(eid);
        QueryEdge e;
        e.id        = eid;
        e.from_id   = caller;
        e.to_id     = cur_id;
        e.edge_type = ColText(s, 2);
        e.call_file = FilePath(db, sqlite3_column_int64(s, 3), fc);
        e.call_line   = sqlite3_column_int(s, 4);
        e.call_column = sqlite3_column_int(s, 5);
        res.edges.push_back(e);
        if (!visited.count(caller)) {
          visited.insert(caller);
          q.push({caller, rem - 1, CallDirection::kBackward});
        }
      }
      sqlite3_finalize(s);
    }
  }

  res.nodes.reserve(node_map.size());
  for (auto& [k, v] : node_map) {
    if (!ShouldExclude(v)) res.nodes.push_back(v);
  }
  return res;
}

// ─── class graph ──────────────────────────────────────────────────────────────

static int64_t ResolveClassId(sqlite3* db, const std::string& usr_or_name,
                               const std::string& file_path) {
  if (usr_or_name.empty()) return 0;
  if (usr_or_name.size() > 2 && usr_or_name[0] == 'c' && usr_or_name[1] == ':') {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db, "SELECT id FROM class WHERE usr=?", -1, &s, nullptr);
    BindText(s, 1, usr_or_name);
    int64_t id = 0;
    if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    if (id) return id;
  }
  const char* sql = file_path.empty()
      ? "SELECT c.id FROM class c WHERE c.name=? LIMIT 1"
      : "SELECT c.id FROM class c JOIN file f ON c.def_file_id=f.id"
        " WHERE c.name=? AND f.path LIKE ? LIMIT 1";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
  BindText(s, 1, usr_or_name);
  if (!file_path.empty()) BindText(s, 2, "%" + file_path + "%");
  int64_t id = 0;
  if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
  sqlite3_finalize(s);
  return id;
}

static ClassQueryNode LoadClassNode(sqlite3* db, int64_t cls_id,
                                     std::unordered_map<int64_t, std::string>& fc) {
  ClassQueryNode n{};
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db,
      "SELECT id,usr,name,qualified_name,def_file_id,def_line,def_column,"
      "def_line_end,def_col_end FROM class WHERE id=?", -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, cls_id);
  if (sqlite3_step(s) == SQLITE_ROW) {
    n.id             = sqlite3_column_int64(s, 0);
    n.usr            = ColText(s, 1);
    n.name           = ColText(s, 2);
    n.qualified_name = ColText(s, 3);
    n.def_file       = FilePath(db, sqlite3_column_int64(s, 4), fc);
    n.def_line       = sqlite3_column_int(s, 5);
    n.def_column     = sqlite3_column_int(s, 6);
    n.def_line_end   = sqlite3_column_int(s, 7);
    n.def_col_end    = sqlite3_column_int(s, 8);
  }
  sqlite3_finalize(s);
  return n;
}

ClassGraphResult QueryClassGraph(sqlite3* db, const std::string& symbol_usr_or_name,
                                  const std::string& file_path) {
  ClassGraphResult res;
  int64_t root_id = ResolveClassId(db, symbol_usr_or_name, file_path);
  if (!root_id) return res;

  std::unordered_map<int64_t, std::string> fc;
  std::unordered_map<int64_t, ClassQueryNode> node_map;
  std::unordered_set<int64_t> visited;

  std::queue<int64_t> q;
  q.push(root_id);
  visited.insert(root_id);
  node_map[root_id] = LoadClassNode(db, root_id, fc);

  while (!q.empty()) {
    int64_t cur = q.front(); q.pop();
    // Relations where cur is parent or child
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT parent_id, child_id, relation_type FROM class_relation"
        " WHERE parent_id=? OR child_id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, cur);
    sqlite3_bind_int64(s, 2, cur);
    while (sqlite3_step(s) == SQLITE_ROW) {
      int64_t pid = sqlite3_column_int64(s, 0);
      int64_t cid = sqlite3_column_int64(s, 1);
      ClassQueryEdge e{pid, cid, ColText(s, 2)};
      res.edges.push_back(e);
      for (int64_t nid : {pid, cid}) {
        if (!visited.count(nid)) {
          visited.insert(nid);
          node_map[nid] = LoadClassNode(db, nid, fc);
          q.push(nid);
        }
      }
    }
    sqlite3_finalize(s);
  }

  res.nodes.reserve(node_map.size());
  for (auto& [k, v] : node_map) res.nodes.push_back(v);
  return res;
}

// ─── data flow ────────────────────────────────────────────────────────────────

DataFlowResult QueryDataFlow(sqlite3* db, const std::string& symbol_usr_or_name,
                              const std::string& file_path) {
  DataFlowResult res{};
  std::unordered_map<int64_t, std::string> fc;

  // Find global_var
  const char* var_sql = symbol_usr_or_name.size() > 2 && symbol_usr_or_name[0] == 'c'
      ? "SELECT id,usr,name,def_file_id,def_line,def_column FROM global_var WHERE usr=?"
      : (file_path.empty()
         ? "SELECT id,usr,name,def_file_id,def_line,def_column FROM global_var"
           " WHERE name=? LIMIT 1"
         : "SELECT gv.id,gv.usr,gv.name,gv.def_file_id,gv.def_line,gv.def_column"
           " FROM global_var gv JOIN file f ON gv.def_file_id=f.id"
           " WHERE gv.name=? AND f.path LIKE ? LIMIT 1");
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, var_sql, -1, &s, nullptr);
  BindText(s, 1, symbol_usr_or_name);
  if (!file_path.empty() && !(symbol_usr_or_name.size() > 2 && symbol_usr_or_name[0] == 'c'))
    BindText(s, 2, "%" + file_path + "%");
  if (sqlite3_step(s) == SQLITE_ROW) {
    res.var.id        = sqlite3_column_int64(s, 0);
    res.var.usr       = ColText(s, 1);
    res.var.name      = ColText(s, 2);
    res.var.def_file  = FilePath(db, sqlite3_column_int64(s, 3), fc);
    res.var.def_line  = sqlite3_column_int(s, 4);
    res.var.def_column = sqlite3_column_int(s, 5);
  }
  sqlite3_finalize(s);
  if (!res.var.id) return res;

  // Load edges
  sqlite3_prepare_v2(db,
      "SELECT dfe.accessor_id, sym.name, dfe.access_type, dfe.access_file_id,"
      " dfe.access_line, dfe.access_column FROM data_flow_edge dfe"
      " JOIN symbol sym ON dfe.accessor_id=sym.id WHERE dfe.var_id=?",
      -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, res.var.id);
  while (sqlite3_step(s) == SQLITE_ROW) {
    DataFlowEdge e;
    e.accessor_id   = sqlite3_column_int64(s, 0);
    e.accessor_name = ColText(s, 1);
    e.access_type   = ColText(s, 2);
    e.access_file   = FilePath(db, sqlite3_column_int64(s, 3), fc);
    e.access_line   = sqlite3_column_int(s, 4);
    e.access_column = sqlite3_column_int(s, 5);
    res.edges.push_back(e);
  }
  sqlite3_finalize(s);
  return res;
}

// ─── control flow ─────────────────────────────────────────────────────────────
// 从 cfg_index 查询 pb 文件路径，读取并反序列化 CFG protobuf 消息。
// 支持两种格式：新格式 TuCfgBundle（per-TU 捆绑）和旧格式 FunctionCfg（per-function）。

/// 将 FunctionCfg 中的节点和边转换为 ControlFlowResult
static void ConvertFunctionCfg(const codexray::cfg::FunctionCfg& func_cfg,
                                ControlFlowResult& res) {
  for (const auto& pn : func_cfg.nodes()) {
    CfgNodeQ n;
    n.id         = pn.block_id();
    n.block_id   = pn.block_id();
    n.file       = pn.file_path();
    n.begin_line = pn.begin_line();
    n.begin_col  = pn.begin_col();
    n.end_line   = pn.end_line();
    n.end_col    = pn.end_col();
    n.label      = pn.label();
    res.nodes.push_back(n);
  }
  for (const auto& pe : func_cfg.edges()) {
    CfgEdgeQ e;
    e.from_node_id = pe.from_block();
    e.to_node_id   = pe.to_block();
    e.edge_type    = pe.edge_type();
    res.edges.push_back(e);
  }
}

ControlFlowResult QueryControlFlow(sqlite3* db, const std::string& db_dir,
                                    const std::string& symbol_usr_or_name,
                                    const std::string& file_path) {
  ControlFlowResult res;
  int64_t sym_id = ResolveSymbolId(db, symbol_usr_or_name, file_path);
  if (!sym_id) return res;

  // 查询 cfg_index 表获取 pb 文件相对路径
  std::string pb_path;
  {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT pb_path FROM cfg_index WHERE symbol_id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, sym_id);
    if (sqlite3_step(s) == SQLITE_ROW) pb_path = ColText(s, 0);
    sqlite3_finalize(s);
  }
  if (pb_path.empty()) return res;

  // 查询该 symbol 的 USR（用于在 TuCfgBundle 中定位对应函数）
  std::string sym_usr;
  {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT usr FROM symbol WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, sym_id);
    if (sqlite3_step(s) == SQLITE_ROW) sym_usr = ColText(s, 0);
    sqlite3_finalize(s);
  }

  // 构造绝对路径并读取 pb 文件
  std::string abs_path = db_dir + "/" + pb_path;
  std::ifstream ifs(abs_path, std::ios::binary);
  if (!ifs) {
    LogError("QueryControlFlow: cannot open " + abs_path);
    return res;
  }
  std::string data((std::istreambuf_iterator<char>(ifs)),
                   std::istreambuf_iterator<char>());

  // 尝试新格式 TuCfgBundle（per-TU 捆绑多个函数的 CFG）
  codexray::cfg::TuCfgBundle bundle;
  if (bundle.ParseFromString(data) && bundle.functions_size() > 0) {
    // 在 bundle 中查找匹配的函数
    for (const auto& func_cfg : bundle.functions()) {
      if (func_cfg.function_usr() == sym_usr) {
        ConvertFunctionCfg(func_cfg, res);
        return res;
      }
    }
    // USR 未匹配到（可能数据不一致），尝试旧格式兜底
  }

  // 兜底：尝试旧格式 FunctionCfg（per-function 单独文件）
  codexray::cfg::FunctionCfg proto;
  if (proto.ParseFromString(data)) {
    ConvertFunctionCfg(proto, res);
  } else {
    LogError("QueryControlFlow: ParseFromString failed for " + abs_path);
  }

  return res;
}

// ─── symbol_at ────────────────────────────────────────────────────────────────

std::vector<QueryNode> QuerySymbolsAt(sqlite3* db, int64_t project_id,
                                       const std::string& file_path,
                                       int line, int column) {
  std::vector<QueryNode> result;
  std::unordered_map<int64_t, std::string> fc;
  int64_t fid = QueryFileIdByPath(db, project_id, file_path);
  if (!fid) {
    // Try without project filter
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db, "SELECT id FROM file WHERE path=? LIMIT 1", -1, &s, nullptr);
    BindText(s, 1, file_path);
    if (sqlite3_step(s) == SQLITE_ROW) fid = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
  }
  if (!fid) {
    // 与解析时 MakeAbsolute/NormalizePath 一致，用规范化路径再试一次
    std::string norm = NormalizePath(file_path);
    if (norm != file_path) {
      fid = QueryFileIdByPath(db, project_id, norm);
      if (!fid) {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db, "SELECT id FROM file WHERE path=? LIMIT 1", -1, &s, nullptr);
        BindText(s, 1, norm);
        if (sqlite3_step(s) == SQLITE_ROW) fid = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
      }
    }
  }
  if (!fid) return result;
  auto rows = QuerySymbolsByFileAndLine(db, fid, line, column);
  for (const auto& r : rows) {
    QueryNode n;
    n.usr            = r.usr;
    n.name           = r.name;
    n.qualified_name = r.qualified_name;
    n.kind           = r.kind;
    n.def_file       = QueryFilePathById(db, r.def_file_id);
    n.def_line       = r.def_line;
    n.def_column     = r.def_column;
    n.def_line_end   = r.def_line_end;
    n.def_col_end    = r.def_col_end;
    // Get DB id
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db, "SELECT id FROM symbol WHERE usr=?", -1, &s, nullptr);
    BindText(s, 1, r.usr);
    if (sqlite3_step(s) == SQLITE_ROW) n.id = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    result.push_back(n);
  }
  return result;
}

}  // namespace codexray
