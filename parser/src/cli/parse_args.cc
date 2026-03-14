#include "parse_args.h"
#include <cstring>
#include <sstream>

namespace codexray {

namespace {

// Split comma-separated string into vector
static std::vector<std::string> SplitComma(const std::string& s) {
  std::vector<std::string> result;
  std::istringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    if (!tok.empty()) result.push_back(tok);
  }
  return result;
}

// Check if argv[i] matches flag and advance to the value (next arg or =val)
static bool GetFlagValue(int argc, char* argv[], int& i,
                          const char* flag, std::string& out) {
  size_t flen = strlen(flag);
  if (strncmp(argv[i], flag, flen) == 0) {
    if (argv[i][flen] == '=') {
      out = argv[i] + flen + 1;
      return true;
    }
    if (argv[i][flen] == '\0' && i + 1 < argc) {
      out = argv[++i];
      return true;
    }
  }
  return false;
}

static bool IsBoolFlag(const char* arg, const char* flag) {
  return strcmp(arg, flag) == 0;
}

}  // namespace

Subcommand ParseArgs(int argc, char* argv[],
                     ParseOptions* po,
                     QueryOptions* qo,
                     ListRunsOptions* lo,
                     std::string* error_msg) {
  if (argc < 2) { *error_msg = "No command given"; return Subcommand::kNone; }

  std::string cmd = argv[1];
  Subcommand sub;
  if      (cmd == "parse")     sub = Subcommand::kParse;
  else if (cmd == "query")     sub = Subcommand::kQuery;
  else if (cmd == "list-runs") sub = Subcommand::kListRuns;
  else {
    *error_msg = "Unknown command: " + cmd;
    return Subcommand::kNone;
  }

  std::string val;
  for (int i = 2; i < argc; ++i) {
    if (sub == Subcommand::kParse) {
      if      (GetFlagValue(argc, argv, i, "--project",          val)) po->project_root = val;
      else if (GetFlagValue(argc, argv, i, "--compile-commands", val)) po->compile_commands_path = val;
      else if (GetFlagValue(argc, argv, i, "--output-db",        val)) po->db_path = val;
      else if (GetFlagValue(argc, argv, i, "--parallel",         val) ||
               GetFlagValue(argc, argv, i, "--jobs",             val)) {
        int n = std::atoi(val.c_str());
        if (n > 0) po->parallel = static_cast<unsigned>(n);
      }
      else if (IsBoolFlag(argv[i], "--lazy"))        po->lazy = true;
      else if (IsBoolFlag(argv[i], "--incremental")) po->incremental = true;
      else if (IsBoolFlag(argv[i], "--verbose"))     po->verbose = true;
      else if (GetFlagValue(argc, argv, i, "--priority-dirs", val))
        po->priority_dirs = SplitComma(val);
    }
    else if (sub == Subcommand::kQuery) {
      if      (GetFlagValue(argc, argv, i, "--db",         val)) qo->db_path = val;
      else if (GetFlagValue(argc, argv, i, "--project",    val)) qo->project_root = val;
      else if (GetFlagValue(argc, argv, i, "--type",       val)) qo->query_type = val;
      else if (GetFlagValue(argc, argv, i, "--symbol",     val)) qo->symbol = val;
      else if (GetFlagValue(argc, argv, i, "--file",       val)) qo->file_path = val;
      else if (GetFlagValue(argc, argv, i, "--line",       val)) qo->line = std::atoi(val.c_str());
      else if (GetFlagValue(argc, argv, i, "--column",     val)) qo->column = std::atoi(val.c_str());
      else if (GetFlagValue(argc, argv, i, "--depth",      val)) qo->depth = std::atoi(val.c_str());
      else if (GetFlagValue(argc, argv, i, "--direction",  val)) qo->direction = val;
      else if (GetFlagValue(argc, argv, i, "--parallel",   val) ||
               GetFlagValue(argc, argv, i, "--jobs",       val)) {
        int n = std::atoi(val.c_str());
        if (n > 0) qo->parallel = static_cast<unsigned>(n);
      }
      else if (IsBoolFlag(argv[i], "--lazy"))    qo->lazy = true;
      else if (IsBoolFlag(argv[i], "--verbose")) qo->verbose = true;
      else if (GetFlagValue(argc, argv, i, "--priority-dirs", val))
        qo->priority_dirs = SplitComma(val);
    }
    else if (sub == Subcommand::kListRuns) {
      if      (GetFlagValue(argc, argv, i, "--db",      val)) lo->db_path = val;
      else if (GetFlagValue(argc, argv, i, "--project", val)) lo->project_root = val;
      else if (GetFlagValue(argc, argv, i, "--limit",   val)) lo->limit = std::atoi(val.c_str());
      else if (IsBoolFlag(argv[i], "--verbose"))               lo->verbose = true;
    }
  }

  // Validate required args
  if (sub == Subcommand::kParse && po->project_root.empty()) {
    *error_msg = "parse: --project is required";
    return Subcommand::kNone;
  }
  if (sub == Subcommand::kQuery) {
    if (qo->db_path.empty()) {
      *error_msg = "query: --db is required";
      return Subcommand::kNone;
    }
    if (qo->query_type.empty()) {
      if (!qo->file_path.empty() && qo->line > 0)
        qo->query_type = "symbol_at";
      else {
        *error_msg = "query: --type is required";
        return Subcommand::kNone;
      }
    }
    if (qo->depth <= 0) qo->depth = 2;
  }
  if (sub == Subcommand::kListRuns && lo->db_path.empty()) {
    *error_msg = "list-runs: --db is required";
    return Subcommand::kNone;
  }

  return sub;
}

}  // namespace codexray
