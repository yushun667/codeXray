/**
 * 解析引擎 CLI 参数解析实现
 */

#include "cli/parse_args.h"
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace codexray {

static bool ConsumeArg(int* i, int argc, char* argv[], const char* name, std::string* out) {
  if (std::strcmp(argv[*i], name) != 0) return false;
  if (*i + 1 >= argc) return false;
  *out = argv[*i + 1];
  *i += 2;
  return true;
}

static void SplitComma(const std::string& s, std::vector<std::string>* out) {
  out->clear();
  std::string cur;
  for (char c : s) {
    if (c == ',') {
      if (!cur.empty()) out->push_back(cur);
      cur.clear();
    } else
      cur.push_back(c);
  }
  if (!cur.empty()) out->push_back(cur);
}

Subcommand ParseArgs(int argc, char* argv[],
                     ParseOptions* parse_opts,
                     QueryOptions* query_opts,
                     ListRunsOptions* list_opts,
                     std::string* error_msg) {
  if (argc < 2) return Subcommand::kNone;
  const char* cmd = argv[1];
  if (std::strcmp(cmd, "parse") == 0) {
    if (!parse_opts) return Subcommand::kNone;
    *parse_opts = ParseOptions();
    for (int i = 2; i < argc; ) {
      if (ConsumeArg(&i, argc, argv, "--project", &parse_opts->project_root)) continue;
      if (ConsumeArg(&i, argc, argv, "--compile-commands", &parse_opts->compile_commands_path)) continue;
      if (ConsumeArg(&i, argc, argv, "--output-db", &parse_opts->output_db)) continue;
      std::string parallel_str;
      if (ConsumeArg(&i, argc, argv, "--parallel", &parallel_str)) {
        parse_opts->parallel = static_cast<unsigned>(std::atoi(parallel_str.c_str()));
        continue;
      }
      if (ConsumeArg(&i, argc, argv, "--priority-dirs", &parallel_str)) {
        SplitComma(parallel_str, &parse_opts->priority_dirs);
        continue;
      }
      if (i < argc && std::strcmp(argv[i], "--lazy") == 0) {
        parse_opts->lazy = true;
        i++;
        continue;
      }
      if (i < argc && std::strcmp(argv[i], "--incremental") == 0) {
        parse_opts->incremental = true;
        i++;
        continue;
      }
      i++;
    }
    if (parse_opts->project_root.empty() && error_msg) *error_msg = "missing --project";
    return parse_opts->project_root.empty() ? Subcommand::kNone : Subcommand::kParse;
  }
  if (std::strcmp(cmd, "query") == 0) {
    if (!query_opts) return Subcommand::kNone;
    *query_opts = QueryOptions();
    for (int i = 2; i < argc; ) {
      std::string s;
      if (ConsumeArg(&i, argc, argv, "--db", &query_opts->db_path)) continue;
      if (ConsumeArg(&i, argc, argv, "--project", &query_opts->project_root)) continue;
      if (ConsumeArg(&i, argc, argv, "--type", &query_opts->query_type)) continue;
      if (ConsumeArg(&i, argc, argv, "--symbol", &query_opts->symbol)) continue;
      if (ConsumeArg(&i, argc, argv, "--file", &query_opts->file_path)) continue;
      if (ConsumeArg(&i, argc, argv, "--line", &s)) {
        query_opts->line = std::atoi(s.c_str());
        continue;
      }
      if (ConsumeArg(&i, argc, argv, "--column", &s)) {
        query_opts->column = std::atoi(s.c_str());
        continue;
      }
      if (ConsumeArg(&i, argc, argv, "--depth", &s)) {
        query_opts->depth = std::atoi(s.c_str());
        if (query_opts->depth <= 0) query_opts->depth = 3;
        continue;
      }
      i++;
    }
    if (query_opts->db_path.empty() || query_opts->query_type.empty()) {
      if (error_msg) *error_msg = "query requires --db and --type";
      return Subcommand::kNone;
    }
    return Subcommand::kQuery;
  }
  if (std::strcmp(cmd, "list-runs") == 0) {
    if (!list_opts) return Subcommand::kNone;
    *list_opts = ListRunsOptions();
    for (int i = 2; i < argc; ) {
      std::string s;
      if (ConsumeArg(&i, argc, argv, "--db", &list_opts->db_path)) continue;
      if (ConsumeArg(&i, argc, argv, "--project", &list_opts->project_root)) continue;
      if (ConsumeArg(&i, argc, argv, "--limit", &s)) {
        list_opts->limit = std::atoi(s.c_str());
        if (list_opts->limit <= 0) list_opts->limit = 100;
        continue;
      }
      i++;
    }
    if (list_opts->db_path.empty() && error_msg) *error_msg = "list-runs requires --db";
    return list_opts->db_path.empty() ? Subcommand::kNone : Subcommand::kListRuns;
  }
  return Subcommand::kNone;
}

}  // namespace codexray
