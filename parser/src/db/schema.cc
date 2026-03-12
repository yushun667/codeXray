#include "schema.h"
#include "../common/logger.h"
#include <sqlite3.h>

namespace codexray {

namespace {

bool Exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    LogError(std::string("Schema SQL failed: ") + (err ? err : "") + " | " + sql);
    sqlite3_free(err);
    return false;
  }
  return true;
}

}  // namespace

bool EnsureSchema(sqlite3* db) {
  // ── project ──────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS project ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  root_path TEXT NOT NULL UNIQUE,"
      "  compile_commands_path TEXT,"
      "  created_at TEXT DEFAULT (datetime('now'))"
      ")")) return false;

  // ── file ─────────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS file ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  project_id INTEGER NOT NULL REFERENCES project(id),"
      "  path TEXT NOT NULL,"
      "  UNIQUE(project_id, path)"
      ")")) return false;

  // ── parse_run ─────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS parse_run ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  project_id INTEGER NOT NULL REFERENCES project(id),"
      "  started_at TEXT DEFAULT (datetime('now')),"
      "  finished_at TEXT,"
      "  mode TEXT NOT NULL DEFAULT 'full',"   // 'full' or 'incremental'
      "  files_parsed INTEGER DEFAULT 0,"
      "  files_failed INTEGER DEFAULT 0,"
      "  status TEXT NOT NULL DEFAULT 'running',"  // running / completed / failed
      "  error_message TEXT"
      ")")) return false;

  // ── parsed_file ───────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS parsed_file ("
      "  project_id INTEGER NOT NULL REFERENCES project(id),"
      "  file_id INTEGER NOT NULL REFERENCES file(id),"
      "  parse_run_id INTEGER REFERENCES parse_run(id),"
      "  file_mtime INTEGER DEFAULT 0,"
      "  file_hash TEXT DEFAULT '',"
      "  parsed_at TEXT DEFAULT (datetime('now')),"
      "  PRIMARY KEY(project_id, file_id)"
      ")")) return false;

  // ── symbol ────────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS symbol ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  usr TEXT NOT NULL UNIQUE,"
      "  name TEXT NOT NULL,"
      "  qualified_name TEXT,"
      "  kind TEXT NOT NULL,"
      "  def_file_id INTEGER REFERENCES file(id),"
      "  def_line INTEGER DEFAULT 0,"
      "  def_column INTEGER DEFAULT 0,"
      "  def_line_end INTEGER DEFAULT 0,"
      "  def_col_end INTEGER DEFAULT 0,"
      "  decl_file_id INTEGER REFERENCES file(id),"
      "  decl_line INTEGER DEFAULT 0,"
      "  decl_column INTEGER DEFAULT 0,"
      "  decl_line_end INTEGER DEFAULT 0,"
      "  decl_col_end INTEGER DEFAULT 0"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_symbol_name ON symbol(name)")) return false;
  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_symbol_def_file ON symbol(def_file_id, def_line)")) return false;
  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_symbol_decl_file ON symbol(decl_file_id, decl_line)")) return false;

  // ── call_edge ─────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS call_edge ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  caller_id INTEGER NOT NULL REFERENCES symbol(id),"
      "  callee_id INTEGER NOT NULL REFERENCES symbol(id),"
      "  edge_type TEXT NOT NULL DEFAULT 'direct',"
      "  call_file_id INTEGER REFERENCES file(id),"
      "  call_line INTEGER DEFAULT 0,"
      "  call_column INTEGER DEFAULT 0,"
      "  UNIQUE(caller_id, callee_id, call_file_id, call_line, call_column)"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_call_edge_caller ON call_edge(caller_id)")) return false;
  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_call_edge_callee ON call_edge(callee_id)")) return false;

  // ── class ─────────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS class ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  usr TEXT NOT NULL UNIQUE,"
      "  name TEXT NOT NULL,"
      "  qualified_name TEXT,"
      "  def_file_id INTEGER REFERENCES file(id),"
      "  def_line INTEGER DEFAULT 0,"
      "  def_column INTEGER DEFAULT 0,"
      "  def_line_end INTEGER DEFAULT 0,"
      "  def_col_end INTEGER DEFAULT 0"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_class_name ON class(name)")) return false;

  // ── class_relation ────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS class_relation ("
      "  parent_id INTEGER NOT NULL REFERENCES class(id),"
      "  child_id INTEGER NOT NULL REFERENCES class(id),"
      "  relation_type TEXT NOT NULL,"
      "  PRIMARY KEY(parent_id, child_id, relation_type)"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_cr_parent ON class_relation(parent_id)")) return false;
  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_cr_child ON class_relation(child_id)")) return false;

  // ── class_member ──────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS class_member ("
      "  class_id INTEGER NOT NULL REFERENCES class(id),"
      "  member_name TEXT NOT NULL,"
      "  member_type_str TEXT,"
      "  member_symbol_id INTEGER REFERENCES symbol(id)"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_cm_class ON class_member(class_id)")) return false;

  // ── global_var ────────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS global_var ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  usr TEXT NOT NULL UNIQUE,"
      "  name TEXT NOT NULL,"
      "  qualified_name TEXT,"
      "  def_file_id INTEGER REFERENCES file(id),"
      "  def_line INTEGER DEFAULT 0,"
      "  def_column INTEGER DEFAULT 0,"
      "  def_line_end INTEGER DEFAULT 0,"
      "  def_col_end INTEGER DEFAULT 0"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_gv_name ON global_var(name)")) return false;

  // ── data_flow_edge ────────────────────────────────────────────────────────
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS data_flow_edge ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  var_id INTEGER NOT NULL REFERENCES global_var(id),"
      "  accessor_id INTEGER NOT NULL REFERENCES symbol(id),"
      "  access_type TEXT NOT NULL,"
      "  access_file_id INTEGER REFERENCES file(id),"
      "  access_line INTEGER DEFAULT 0,"
      "  access_column INTEGER DEFAULT 0"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_dfe_var ON data_flow_edge(var_id)")) return false;
  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_dfe_accessor ON data_flow_edge(accessor_id)")) return false;

  // ── cfg_index ─────────────────────────────────────────────────────────────
  // CFG 数据以 protobuf 文件存储于 <db_dir>/cfg/<hash[0:2]>/<hash>.pb
  // 此表仅存储函数 symbol_id → pb 文件路径的索引（相对于 db_dir 的相对路径）
  if (!Exec(db,
      "CREATE TABLE IF NOT EXISTS cfg_index ("
      "  symbol_id INTEGER NOT NULL UNIQUE REFERENCES symbol(id),"
      "  pb_path TEXT NOT NULL"
      ")")) return false;

  if (!Exec(db, "CREATE INDEX IF NOT EXISTS idx_cfg_index_sym ON cfg_index(symbol_id)")) return false;

  return true;
}

}  // namespace codexray
