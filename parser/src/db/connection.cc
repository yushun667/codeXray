/**
 * 解析引擎 DB 模块：SQLite 连接实现
 */

#include "db/connection.h"
#include "common/logger.h"
#include <sqlite3.h>
#include <cstring>

namespace codexray {

Connection::Connection() = default;

Connection::~Connection() { Close(); }

Connection::Connection(Connection&& other) noexcept : db_(other.db_) {
  other.db_ = nullptr;
}

Connection& Connection::operator=(Connection&& other) noexcept {
  if (this != &other) {
    Close();
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

bool Connection::Open(const std::string& path) {
  if (db_) {
    LogInfo("db connection already open");
    return true;
  }
  int r = sqlite3_open(path.c_str(), &db_);
  if (r != SQLITE_OK) {
    LogError("sqlite3_open failed: %s", path.c_str());
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }
  LogInfo("db opened: %s", path.c_str());
  return true;
}

void Connection::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
    LogInfo("db closed");
  }
}

bool Connection::Execute(const char* sql) {
  if (!db_) return false;
  char* err = nullptr;
  int r = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (r != SQLITE_OK) {
    LogError("sqlite3_exec: %s", err ? err : sqlite3_errmsg(db_));
    if (err) sqlite3_free(err);
    return false;
  }
  return true;
}

bool Connection::BeginTransaction() {
  return Execute("BEGIN TRANSACTION");
}

bool Connection::Commit() {
  return Execute("COMMIT");
}

bool Connection::Rollback() {
  return Execute("ROLLBACK");
}

}  // namespace codexray
