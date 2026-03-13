#include "reader.h"
#include "../../common/logger.h"
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

// Resolve symbol by USR or name, return DB id
// 策略：1. USR 精确匹配；2. name + def_file_id JOIN；
//       3. name + USR 包含文件名 fallback（处理 def_file_id 为 NULL 的情况）
static int64_t ResolveSymbolId(sqlite3* db,
                                const std::string& usr_or_name,
                                const std::string& file_path) {
  if (usr_or_name.empty()) return 0;
  // Try by USR first (USRs start with 'c:')
  if (usr_or_name.size() > 2 && usr_or_name[0] == 'c' && usr_or_name[1] == ':') {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db, "SELECT id FROM symbol WHERE usr=?", -1, &s, nullptr);
    BindText(s, 1, usr_or_name);
    int64_t id = 0;
    if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    if (id) return id;
  }
  // Try by name + file (via def_file_id JOIN)
  if (!file_path.empty()) {
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT s.id FROM symbol s JOIN file f ON s.def_file_id=f.id"
        " WHERE s.name=? AND f.path LIKE ? LIMIT 1", -1, &s, nullptr);
    BindText(s, 1, usr_or_name);
    BindText(s, 2, "%" + file_path + "%");
    int64_t id = 0;
    if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    if (id) return id;

    // Fallback: def_file_id 为 NULL 时，通过 USR 中包含的文件名匹配
    // 许多匿名命名空间的方法其 USR 以文件名开头（如 "c:IntrinsicEmitter.cpp@..."）
    // 提取 file_path 的基本文件名用于 LIKE 匹配 USR
    std::string basename = file_path;
    auto pos = basename.rfind('/');
    if (pos != std::string::npos) basename = basename.substr(pos + 1);
    if (!basename.empty()) {
      sqlite3_stmt* s2 = nullptr;
      sqlite3_prepare_v2(db,
          "SELECT id FROM symbol WHERE name=? AND usr LIKE ? LIMIT 1",
          -1, &s2, nullptr);
      BindText(s2, 1, usr_or_name);
      BindText(s2, 2, "%" + basename + "%");
      int64_t id2 = 0;
      if (sqlite3_step(s2) == SQLITE_ROW) id2 = sqlite3_column_int64(s2, 0);
      sqlite3_finalize(s2);
      if (id2) return id2;
    }
  }
  // No file filter, just match by name
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, "SELECT s.id FROM symbol s WHERE s.name=? LIMIT 1",
                     -1, &s, nullptr);
  BindText(s, 1, usr_or_name);
  int64_t id = 0;
  if (sqlite3_step(s) == SQLITE_ROW) id = sqlite3_column_int64(s, 0);
  sqlite3_finalize(s);
  return id;
}

static QueryNode LoadSymbolNode(sqlite3* db, int64_t sym_id,
                                 std::unordered_map<int64_t, std::string>& fc) {
  QueryNode n{};
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db,
      "SELECT id,usr,name,qualified_name,kind,def_file_id,def_line,def_column,"
      "def_line_end,def_col_end FROM symbol WHERE id=?", -1, &s, nullptr);
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

std::vector<SymbolRow> QuerySymbolsByFileAndLine(sqlite3* db, int64_t file_id,
                                                  int line, int column) {
  std::vector<SymbolRow> result;
  // Best match: definition range [def_line, def_line_end] contains line
  const char* sql =
      "SELECT usr, name, qualified_name, kind, def_file_id, def_line, def_column,"
      " def_line_end, def_col_end FROM symbol"
      " WHERE def_file_id=? AND def_line <= ? AND def_line_end >= ?"
      " ORDER BY (def_line_end - def_line) ASC LIMIT 5";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
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

CallGraphResult QueryCallGraph(sqlite3* db, const std::string& symbol_usr_or_name,
                                const std::string& file_path, int depth) {
  CallGraphResult res;
  int64_t root_id = ResolveSymbolId(db, symbol_usr_or_name, file_path);
  if (!root_id) return res;

  std::unordered_map<int64_t, std::string> fc;
  std::unordered_map<int64_t, QueryNode> node_map;
  std::unordered_set<int64_t> visited;
  std::unordered_set<int64_t> edge_set;

  // BFS forward (callees) and backward (callers)
  struct Work { int64_t id; int depth_remaining; };
  std::queue<Work> q;
  q.push({root_id, depth});
  visited.insert(root_id);
  node_map[root_id] = LoadSymbolNode(db, root_id, fc);

  while (!q.empty()) {
    auto [cur_id, rem] = q.front(); q.pop();
    if (rem <= 0) continue;

    // Forward: callees
    {
      sqlite3_stmt* s = nullptr;
      sqlite3_prepare_v2(db,
          "SELECT ce.id, ce.callee_id, ce.edge_type, ce.call_file_id, ce.call_line,"
          " ce.call_column FROM call_edge ce WHERE ce.caller_id=?", -1, &s, nullptr);
      sqlite3_bind_int64(s, 1, cur_id);
      while (sqlite3_step(s) == SQLITE_ROW) {
        int64_t eid     = sqlite3_column_int64(s, 0);
        int64_t callee  = sqlite3_column_int64(s, 1);
        if (edge_set.count(eid)) continue;
        edge_set.insert(eid);
        if (!node_map.count(callee)) {
          node_map[callee] = LoadSymbolNode(db, callee, fc);
        }
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
          q.push({callee, rem - 1});
        }
      }
      sqlite3_finalize(s);
    }

    // Backward: callers
    {
      sqlite3_stmt* s = nullptr;
      sqlite3_prepare_v2(db,
          "SELECT ce.id, ce.caller_id, ce.edge_type, ce.call_file_id, ce.call_line,"
          " ce.call_column FROM call_edge ce WHERE ce.callee_id=?", -1, &s, nullptr);
      sqlite3_bind_int64(s, 1, cur_id);
      while (sqlite3_step(s) == SQLITE_ROW) {
        int64_t eid    = sqlite3_column_int64(s, 0);
        int64_t caller = sqlite3_column_int64(s, 1);
        if (edge_set.count(eid)) continue;
        edge_set.insert(eid);
        if (!node_map.count(caller)) {
          node_map[caller] = LoadSymbolNode(db, caller, fc);
        }
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
          q.push({caller, rem - 1});
        }
      }
      sqlite3_finalize(s);
    }
  }

  res.nodes.reserve(node_map.size());
  for (auto& [k, v] : node_map) res.nodes.push_back(v);
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
