/**
 * SQLite 数据库连接封装。
 * 设计 §3.12：WAL 模式、foreign_keys ON、optimized PRAGMAs。
 */
#pragma once
#include <sqlite3.h>
#include <string>

namespace codexray {

class Connection {
public:
  Connection() = default;
  ~Connection();

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  bool Open(const std::string& path);
  void Close();

  sqlite3* Get() const { return db_; }
  bool IsOpen() const { return db_ != nullptr; }

  bool BeginTransaction();
  bool Commit();
  bool Rollback();

private:
  sqlite3* db_ = nullptr;
};

}  // namespace codexray
