/**
 * 公共输出结构体 — 各 AST 分析模块 → db_writer 的数据契约
 * 定义见：解析引擎详细功能与架构设计 §6
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace codexray {

// ─── 符号 ────────────────────────────────────────────────────────────────────
struct SymbolRow {
  std::string usr;
  std::string name;
  std::string qualified_name;
  std::string kind;           // "function" "method" "constructor" "destructor" "class" "struct" "variable"
  std::string def_file_path;  // file path for def_file_id resolution
  int64_t def_file_id   = 0;
  int     def_line      = 0;
  int     def_column    = 0;
  int     def_line_end  = 0;
  int     def_col_end   = 0;
  std::string decl_file_path; // file path for decl_file_id resolution
  int64_t decl_file_id  = 0;
  int     decl_line     = 0;
  int     decl_column   = 0;
  int     decl_line_end = 0;
  int     decl_col_end  = 0;
};

// ─── 调用边 ───────────────────────────────────────────────────────────────────
struct CallEdgeRow {
  std::string caller_usr;
  std::string callee_usr;
  std::string edge_type;      // "direct" "via_function_pointer"
  std::string call_file_path; // file path for call_file_id resolution
  int64_t     call_file_id = 0;
  int         call_line    = 0;
  int         call_column  = 0;
};

// ─── 类关系 ───────────────────────────────────────────────────────────────────
struct ClassRow {
  std::string usr;
  std::string name;
  std::string qualified_name;
  std::string def_file_path;  // file path for def_file_id resolution
  int64_t def_file_id  = 0;
  int     def_line     = 0;
  int     def_column   = 0;
  int     def_line_end = 0;
  int     def_col_end  = 0;
};

struct ClassRelationRow {
  std::string parent_usr;
  std::string child_usr;
  std::string relation_type;  // "inheritance" "composition" "dependency"
};

struct ClassMemberRow {
  std::string class_usr;
  std::string member_usr;     // symbol usr or empty
  std::string member_name;
  std::string member_type_str;
};

// ─── 全局变量 ─────────────────────────────────────────────────────────────────
struct GlobalVarRow {
  std::string usr;
  std::string name;
  std::string qualified_name;
  std::string def_file_path;  // file path for def_file_id resolution
  int64_t def_file_id  = 0;
  int     def_line     = 0;
  int     def_column   = 0;
  int     def_line_end = 0;
  int     def_col_end  = 0;
};

struct DataFlowEdgeRow {
  std::string var_usr;
  std::string accessor_usr;   // function USR that accesses the var
  std::string access_type;    // "read" "write"
  std::string access_file_path; // file path for access_file_id resolution
  int64_t     access_file_id = 0;
  int         access_line    = 0;
  int         access_column  = 0;
};

// ─── 控制流 ───────────────────────────────────────────────────────────────────
struct CfgNodeRow {
  // id assigned by db writer
  std::string function_usr;
  int         block_id    = 0;    // Clang CFGBlock::getBlockID()
  std::string file_path;          // file path for file_id resolution
  int64_t     file_id     = 0;
  int         begin_line  = 0;
  int         begin_col   = 0;
  int         end_line    = 0;
  int         end_col     = 0;
  std::string label;              // optional summary text
};

struct CfgEdgeRow {
  std::string function_usr;
  int         from_block  = 0;
  int         to_block    = 0;
  std::string edge_type;          // "unconditional" "true" "false" "exception"
};

// ─── 聚合输出 ─────────────────────────────────────────────────────────────────
struct CombinedOutput {
  std::vector<SymbolRow>       symbols;
  std::vector<CallEdgeRow>     call_edges;
  std::vector<ClassRow>        classes;
  std::vector<ClassRelationRow> class_relations;
  std::vector<ClassMemberRow>  class_members;
  std::vector<GlobalVarRow>    global_vars;
  std::vector<DataFlowEdgeRow> data_flow_edges;
  std::vector<CfgNodeRow>      cfg_nodes;
  std::vector<CfgEdgeRow>      cfg_edges;

  // file paths referenced (to pre-register in file table)
  std::vector<std::string>     referenced_files;

  // 源文件的 mtime 和哈希值（由子进程计算，避免父进程重复读文件）
  int64_t     source_file_mtime = 0;
  std::string source_file_hash;
};

}  // namespace codexray
