/**
 * 解析引擎 DB 模块：Schema 实现
 * 表：project, file, parse_run, parsed_file, symbol, call_edge,
 *     class, class_relation, class_member, global_var, data_flow_edge, cfg_node, cfg_edge
 */

#include "db/schema.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <string>

namespace codexray {

static bool RunStatements(sqlite3* db, const std::string& sql) {
  char* err = nullptr;
  int r = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
  if (r != SQLITE_OK) {
    LogError("schema: %s", err ? err : sqlite3_errmsg(db));
    if (err) sqlite3_free(err);
    return false;
  }
  return true;
}

bool EnsureSchema(sqlite3* db) {
  if (!db) return false;
  LogInfo("EnsureSchema: creating tables and indexes");

  const char* tables = R"sql(
CREATE TABLE IF NOT EXISTS project (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  root_path TEXT NOT NULL,
  compile_commands_path TEXT
);

CREATE TABLE IF NOT EXISTS file (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL REFERENCES project(id),
  path TEXT NOT NULL,
  UNIQUE(project_id, path)
);
CREATE INDEX IF NOT EXISTS idx_file_project_path ON file(project_id, path);

CREATE TABLE IF NOT EXISTS parse_run (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL REFERENCES project(id),
  started_at TEXT NOT NULL,
  finished_at TEXT,
  mode TEXT NOT NULL CHECK(mode IN ('full','incremental')),
  files_parsed INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL CHECK(status IN ('running','completed','failed')),
  error_message TEXT
);
CREATE INDEX IF NOT EXISTS idx_parse_run_project_started ON parse_run(project_id, started_at DESC);

CREATE TABLE IF NOT EXISTS parsed_file (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL REFERENCES project(id),
  file_id INTEGER NOT NULL REFERENCES file(id),
  parse_run_id INTEGER NOT NULL REFERENCES parse_run(id),
  file_mtime INTEGER,
  content_hash TEXT,
  parsed_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_parsed_file_project_file ON parsed_file(project_id, file_id);

CREATE TABLE IF NOT EXISTS symbol (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  usr TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  qualified_name TEXT,
  kind TEXT,
  def_file_id INTEGER NOT NULL REFERENCES file(id),
  def_line INTEGER NOT NULL,
  def_column INTEGER NOT NULL,
  def_line_end INTEGER,
  def_column_end INTEGER
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_symbol_usr ON symbol(usr);
CREATE INDEX IF NOT EXISTS idx_symbol_name_file ON symbol(name, def_file_id);
CREATE INDEX IF NOT EXISTS idx_symbol_qualified ON symbol(qualified_name);

CREATE TABLE IF NOT EXISTS call_edge (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  caller_id INTEGER NOT NULL REFERENCES symbol(id),
  callee_id INTEGER NOT NULL REFERENCES symbol(id),
  call_site_file_id INTEGER NOT NULL REFERENCES file(id),
  call_site_line INTEGER NOT NULL,
  call_site_column INTEGER NOT NULL,
  edge_type TEXT NOT NULL CHECK(edge_type IN ('direct','via_function_pointer'))
);
CREATE INDEX IF NOT EXISTS idx_call_edge_caller_callee ON call_edge(caller_id, callee_id);
CREATE INDEX IF NOT EXISTS idx_call_edge_site ON call_edge(call_site_file_id, call_site_line, call_site_column);
CREATE INDEX IF NOT EXISTS idx_call_edge_type ON call_edge(edge_type);

CREATE TABLE IF NOT EXISTS class (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  usr TEXT NOT NULL,
  file_id INTEGER NOT NULL REFERENCES file(id),
  name TEXT NOT NULL,
  qualified_name TEXT,
  def_file_id INTEGER NOT NULL REFERENCES file(id),
  def_line INTEGER NOT NULL,
  def_column INTEGER NOT NULL,
  def_line_end INTEGER,
  def_column_end INTEGER
);
CREATE INDEX IF NOT EXISTS idx_class_name_file ON class(name, file_id);
CREATE INDEX IF NOT EXISTS idx_class_usr ON class(usr);

CREATE TABLE IF NOT EXISTS class_relation (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  parent_id INTEGER NOT NULL REFERENCES class(id),
  child_id INTEGER NOT NULL REFERENCES class(id),
  relation_type TEXT NOT NULL CHECK(relation_type IN ('inheritance','composition','dependency'))
);
CREATE INDEX IF NOT EXISTS idx_class_relation_parent_child ON class_relation(parent_id, child_id);
CREATE INDEX IF NOT EXISTS idx_class_relation_type ON class_relation(relation_type);

CREATE TABLE IF NOT EXISTS class_member (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  class_id INTEGER NOT NULL REFERENCES class(id),
  symbol_id INTEGER REFERENCES symbol(id),
  member_type TEXT,
  name TEXT
);
CREATE INDEX IF NOT EXISTS idx_class_member_class ON class_member(class_id);

CREATE TABLE IF NOT EXISTS global_var (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  usr TEXT NOT NULL,
  def_file_id INTEGER NOT NULL REFERENCES file(id),
  def_line INTEGER NOT NULL,
  def_column INTEGER NOT NULL,
  def_line_end INTEGER,
  def_column_end INTEGER,
  file_id INTEGER NOT NULL REFERENCES file(id),
  name TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_global_var_name_file ON global_var(name, file_id);

CREATE TABLE IF NOT EXISTS data_flow_edge (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  var_id INTEGER NOT NULL REFERENCES global_var(id),
  reader_id INTEGER REFERENCES symbol(id),
  writer_id INTEGER REFERENCES symbol(id),
  read_file_id INTEGER REFERENCES file(id),
  read_line INTEGER,
  read_column INTEGER,
  write_file_id INTEGER REFERENCES file(id),
  write_line INTEGER,
  write_column INTEGER
);
CREATE INDEX IF NOT EXISTS idx_data_flow_var ON data_flow_edge(var_id);
CREATE INDEX IF NOT EXISTS idx_data_flow_reader_writer ON data_flow_edge(reader_id, writer_id);

CREATE TABLE IF NOT EXISTS cfg_node (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  symbol_id INTEGER NOT NULL REFERENCES symbol(id),
  block_id TEXT,
  kind TEXT,
  file_id INTEGER REFERENCES file(id),
  line INTEGER,
  column INTEGER
);
CREATE INDEX IF NOT EXISTS idx_cfg_node_symbol ON cfg_node(symbol_id);

CREATE TABLE IF NOT EXISTS cfg_edge (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  from_node_id INTEGER NOT NULL REFERENCES cfg_node(id),
  to_node_id INTEGER NOT NULL REFERENCES cfg_node(id),
  edge_type TEXT
);
CREATE INDEX IF NOT EXISTS idx_cfg_edge_from_to ON cfg_edge(from_node_id, to_node_id);
)sql";

  if (!RunStatements(db, tables)) return false;
  LogInfo("EnsureSchema: done");
  return true;
}

}  // namespace codexray
