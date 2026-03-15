/**
 * 解析引擎 incremental 实现
 */

#include "incremental.h"
#include "../db/writer/writer.h"
#include "../common/logger.h"
#include <sqlite3.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

namespace codexray {

namespace {

int64_t GetFileMtimeImpl(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return 0;
  return static_cast<int64_t>(st.st_mtime);
}

std::string ComputeFileHashImpl(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream os;
  os << f.rdbuf();
  std::string content = os.str();
  size_t h = 0;
  for (unsigned char c : content) h = h * 31 + c;
  return std::to_string(h);
}

int64_t GetFileIdByPath(sqlite3* db, int64_t project_id, const std::string& path) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id FROM file WHERE project_id = ? AND path = ?", -1, &stmt, nullptr) != SQLITE_OK)
    return 0;
  sqlite3_bind_int64(stmt, 1, project_id);
  sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  int64_t id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    id = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return id;
}

}  // namespace

std::vector<std::string> GetChangedFiles(sqlite3* db, int64_t project_id) {
  std::vector<std::string> out;
  if (!db) return out;
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
      "SELECT f.id, f.path, pf.file_mtime, pf.content_hash FROM "
      "(SELECT file_id, MAX(id) AS mid FROM parsed_file WHERE project_id = ? GROUP BY file_id) latest "
      "JOIN parsed_file pf ON pf.id = latest.mid JOIN file f ON f.id = pf.file_id WHERE f.project_id = ?",
      -1, &stmt, nullptr) != SQLITE_OK)
    return out;
  sqlite3_bind_int64(stmt, 1, project_id);
  sqlite3_bind_int64(stmt, 2, project_id);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int64_t file_id = sqlite3_column_int64(stmt, 0);
    const char* path_p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    int64_t stored_mtime = sqlite3_column_int64(stmt, 2);
    const char* stored_hash_p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    std::string path = path_p ? path_p : "";
    std::string stored_hash = stored_hash_p ? stored_hash_p : "";
    (void)file_id;
    int64_t current_mtime = GetFileMtimeImpl(path);
    if (current_mtime == 0) {
      out.push_back(path);
      continue;
    }
    if (current_mtime != stored_mtime) {
      out.push_back(path);
      continue;
    }
    if (!stored_hash.empty()) {
      std::string current_hash = ComputeFileHashImpl(path);
      if (current_hash != stored_hash) out.push_back(path);
    }
  }
  sqlite3_finalize(stmt);
  LogInfo("GetChangedFiles: %zu changed", out.size());
  return out;
}

bool RemoveDataForFiles(sqlite3* db, int64_t project_id,
                        const std::vector<std::string>& paths,
                        const std::string& db_dir) {
  if (!db) return false;
  codexray::DbWriter writer(db, project_id);
  if (!db_dir.empty()) writer.SetDbDir(db_dir);
  for (const std::string& path : paths) {
    int64_t file_id = GetFileIdByPath(db, project_id, path);
    if (file_id > 0)
      writer.DeleteDataForFile(file_id);
  }
  return true;
}

int64_t GetFileMtime(const std::string& path) {
  return GetFileMtimeImpl(path);
}

std::string ComputeFileHash(const std::string& path) {
  return ComputeFileHashImpl(path);
}

}  // namespace codexray
