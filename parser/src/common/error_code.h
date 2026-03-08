/**
 * 解析引擎公共模块：错误码
 * 与 doc/01-解析引擎/接口约定.md §4 一致
 */

#ifndef CODEXRAY_PARSER_COMMON_ERROR_CODE_H_
#define CODEXRAY_PARSER_COMMON_ERROR_CODE_H_

namespace codexray {

enum class ExitCode : int {
  kSuccess = 0,
  kArgError = 1,
  kCompileCommands = 2,
  kClangFailed = 3,
  kDbWriteFailed = 4,
  kQueryFailed = 5,
};

}  // namespace codexray

#endif  // CODEXRAY_PARSER_COMMON_ERROR_CODE_H_
