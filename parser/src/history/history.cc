#include "history.h"
#include "../common/logger.h"
#include <sstream>
#include <iomanip>

namespace codexray {

namespace {

static std::string ColText(sqlite3_stmt* s, int col) {
  const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
  return p ? std::string(p) : std::string();
}

// Escape a string for JSON
static std::string JsonStr(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  out += '"';
  for (char c : s) {
    if (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  out += '"';
  return out;
}

}  // namespace

int64_t InsertParseRun(sqlite3* db, int64_t project_id, const std::string& mode) {
  const char* sql =
      "INSERT INTO parse_run(project_id, mode, status)"
      " VALUES(?,?,'running')";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
  sqlite3_bind_int64(s, 1, project_id);
  sqlite3_bind_text(s, 2, mode.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(s);
  int64_t run_id = sqlite3_last_insert_rowid(db);
  sqlite3_finalize(s);
  LogInfo("InsertParseRun: run_id=" + std::to_string(run_id));
  return run_id;
}

bool UpdateParseRun(sqlite3* db, int64_t run_id, const std::string& status,
                    int files_parsed, int files_failed,
                    const std::string& error_message) {
  const char* sql =
      "UPDATE parse_run SET status=?, files_parsed=?, files_failed=?,"
      " finished_at=datetime('now'), error_message=? WHERE id=?";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
  sqlite3_bind_text(s, 1, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(s, 2, files_parsed);
  sqlite3_bind_int(s, 3, files_failed);
  sqlite3_bind_text(s, 4, error_message.empty() ? nullptr : error_message.c_str(),
                    -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(s, 5, run_id);
  bool ok = (sqlite3_step(s) == SQLITE_DONE);
  sqlite3_finalize(s);
  return ok;
}

std::string ListRunsJson(sqlite3* db, int64_t project_id, int limit) {
  const char* sql = project_id > 0
      ? "SELECT id, started_at, finished_at, mode, files_parsed, files_failed, status,"
        " error_message FROM parse_run WHERE project_id=?"
        " ORDER BY started_at DESC LIMIT ?"
      : "SELECT id, started_at, finished_at, mode, files_parsed, files_failed, status,"
        " error_message FROM parse_run ORDER BY started_at DESC LIMIT ?";
  sqlite3_stmt* s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
  int bind_idx = 1;
  if (project_id > 0) sqlite3_bind_int64(s, bind_idx++, project_id);
  sqlite3_bind_int(s, bind_idx, limit);

  std::ostringstream out;
  out << "[";
  bool first = true;
  while (sqlite3_step(s) == SQLITE_ROW) {
    if (!first) out << ",";
    first = false;
    int64_t id        = sqlite3_column_int64(s, 0);
    std::string start = ColText(s, 1);
    std::string fin   = ColText(s, 2);
    std::string mode  = ColText(s, 3);
    int fp            = sqlite3_column_int(s, 4);
    int ff            = sqlite3_column_int(s, 5);
    std::string st    = ColText(s, 6);
    std::string err   = ColText(s, 7);
    out << "{"
        << "\"run_id\":" << id << ","
        << "\"started_at\":" << JsonStr(start) << ","
        << "\"finished_at\":" << (fin.empty() ? "null" : JsonStr(fin)) << ","
        << "\"mode\":" << JsonStr(mode) << ","
        << "\"files_parsed\":" << fp << ","
        << "\"files_failed\":" << ff << ","
        << "\"status\":" << JsonStr(st);
    if (!err.empty()) out << ",\"error_message\":" << JsonStr(err);
    out << "}";
  }
  sqlite3_finalize(s);
  out << "]";
  return out.str();
}

}  // namespace codexray
