/**
 * 解析引擎 history 实现
 */

#include "history/history.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace codexray {

namespace {

std::string ColText(sqlite3_stmt* stmt, int col) {
  const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
  return p ? std::string(p) : "";
}

std::string NowUtc() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf;
#ifdef _WIN32
  gmtime_s(&tm_buf, &t);
#else
  gmtime_r(&t, &tm_buf);
#endif
  std::ostringstream os;
  os << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
  return os.str();
}

void EscapeJson(const std::string& s, std::ostringstream& out) {
  out << '"';
  for (char c : s) {
    if (c == '\\') out << "\\\\";
    else if (c == '"') out << "\\\"";
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (static_cast<unsigned char>(c) < 32) out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c) << std::dec;
    else out << c;
  }
  out << '"';
}

}  // namespace

int64_t InsertParseRun(sqlite3* db, int64_t project_id, const std::string& mode) {
  if (!db) return 0;
  std::string now = NowUtc();
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "INSERT INTO parse_run(project_id,started_at,mode,files_parsed,status) VALUES(?,?,?,0,'running')", -1, &stmt, nullptr) != SQLITE_OK) {
    LogError("InsertParseRun prepare failed");
    return 0;
  }
  sqlite3_bind_int64(stmt, 1, project_id);
  sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, mode.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return 0;
  }
  int64_t id = static_cast<int64_t>(sqlite3_last_insert_rowid(db));
  sqlite3_finalize(stmt);
  LogInfo("InsertParseRun: run_id=%ld", (long)id);
  return id;
}

bool UpdateParseRun(sqlite3* db, int64_t run_id,
                   const std::string& finished_at,
                   int files_parsed,
                   const std::string& status,
                   const std::string& error_message) {
  if (!db) return false;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "UPDATE parse_run SET finished_at=?, files_parsed=?, status=?, error_message=? WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, finished_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, files_parsed);
  sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, error_message.empty() ? nullptr : error_message.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, run_id);
  bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

std::string ListRunsJson(sqlite3* db, int64_t project_id, int limit) {
  if (!db) return "[]";
  if (limit <= 0) limit = 100;
  sqlite3_stmt* stmt = nullptr;
  const char* sql = (project_id > 0)
      ? "SELECT id,started_at,finished_at,mode,files_parsed,status FROM parse_run WHERE project_id = ? ORDER BY started_at DESC LIMIT ?"
      : "SELECT id,started_at,finished_at,mode,files_parsed,status FROM parse_run ORDER BY started_at DESC LIMIT ?";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return "[]";
  if (project_id > 0) {
    sqlite3_bind_int64(stmt, 1, project_id);
    sqlite3_bind_int(stmt, 2, limit);
  } else {
    sqlite3_bind_int(stmt, 1, limit);
  }
  std::ostringstream out;
  out << "[";
  bool first = true;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (!first) out << ",";
    first = false;
    int64_t id = sqlite3_column_int64(stmt, 0);
    std::string started = ColText(stmt, 1);
    std::string finished = ColText(stmt, 2);
    std::string mode = ColText(stmt, 3);
    int files_parsed = sqlite3_column_int(stmt, 4);
    std::string status = ColText(stmt, 5);
    out << "{\"run_id\":" << id << ",\"started_at\":";
    EscapeJson(started, out);
    out << ",\"finished_at\":";
    EscapeJson(finished, out);
    out << ",\"mode\":";
    EscapeJson(mode, out);
    out << ",\"files_parsed\":" << files_parsed << ",\"status\":";
    EscapeJson(status, out);
    out << "}";
  }
  sqlite3_finalize(stmt);
  out << "]";
  return out.str();
}

int64_t GetProjectId(sqlite3* db, const std::string& root_path) {
  if (!db || root_path.empty()) return 0;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id FROM project WHERE root_path = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(stmt, 1, root_path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return id;
}

}  // namespace codexray
