#include "connection.h"
#include "../common/logger.h"
#include <sqlite3.h>
#include <string>

namespace codexray {

static void ExecPragma(sqlite3* db, const char* sql) {
  char* err = nullptr;
  sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (err) {
    LogWarn(std::string("PRAGMA failed: ") + err);
    sqlite3_free(err);
  }
}

Connection::~Connection() { Close(); }

bool Connection::Open(const std::string& path) {
  if (db_) Close();
  int rc = sqlite3_open(path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::string err_msg = std::string("Cannot open DB: ") + path + " — " + sqlite3_errmsg(db_);
    LogError(err_msg);
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  // 性能与可靠性优化（设计 §3.12 / §5.4）
  ExecPragma(db_, "PRAGMA journal_mode=WAL");
  ExecPragma(db_, "PRAGMA synchronous=NORMAL");
  ExecPragma(db_, "PRAGMA cache_size=-64000");
  ExecPragma(db_, "PRAGMA foreign_keys=ON");
  ExecPragma(db_, "PRAGMA temp_store=MEMORY");
  return true;
}

void Connection::Close() {
  if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

bool Connection::BeginTransaction() {
  return sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Connection::Commit() {
  return sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool Connection::Rollback() {
  return sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK;
}

}  // namespace codexray
