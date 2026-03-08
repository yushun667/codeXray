/**
 * 解析引擎 DB 模块：SQLite 连接与事务
 * 参考：doc/01-解析引擎 数据库设计 §2
 */

#ifndef CODEXRAY_PARSER_DB_CONNECTION_H_
#define CODEXRAY_PARSER_DB_CONNECTION_H_

#include <memory>
#include <string>

struct sqlite3;

namespace codexray {

class Connection {
 public:
  Connection();
  ~Connection();
  Connection(Connection&&) noexcept;
  Connection& operator=(Connection&&) noexcept;
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  bool Open(const std::string& path);
  void Close();
  bool IsOpen() const { return db_ != nullptr; }
  sqlite3* Get() const { return db_; }

  bool Execute(const char* sql);
  bool BeginTransaction();
  bool Commit();
  bool Rollback();

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace codexray

#endif  // CODEXRAY_PARSER_DB_CONNECTION_H_
