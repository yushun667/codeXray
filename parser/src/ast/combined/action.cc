#include "action.h"
#include "../call_graph/action.h"
#include "../class_relation/action.h"
#include "../data_flow/action.h"
#include "../control_flow/action.h"
#include "../../common/clang_include_detector.h"
#include "../../common/logger.h"

#ifdef CODEXRAY_HAVE_CLANG
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <llvm/Support/raw_ostream.h>

namespace codexray {
namespace combined {

namespace {

class CombinedConsumer : public clang::ASTConsumer {
public:
  explicit CombinedConsumer(_CombinedOut* out) : out_(out) {}

  void HandleTranslationUnit(clang::ASTContext& ctx) override {
    // Call each analysis independently; failures are isolated
    try {
      call_graph::Analyze(ctx,
          &out_->symbols, &out_->call_edges, &out_->referenced_files);
    } catch (const std::exception& e) {
      LogError(std::string("call_graph::Analyze failed: ") + e.what());
    } catch (...) {
      LogError("call_graph::Analyze failed with unknown exception");
    }

    try {
      class_relation::Analyze(ctx,
          &out_->classes, &out_->class_relations,
          &out_->class_members, &out_->referenced_files);
    } catch (const std::exception& e) {
      LogError(std::string("class_relation::Analyze failed: ") + e.what());
    } catch (...) {
      LogError("class_relation::Analyze failed");
    }

    try {
      data_flow::Analyze(ctx,
          &out_->global_vars, &out_->data_flow_edges, &out_->referenced_files);
    } catch (const std::exception& e) {
      LogError(std::string("data_flow::Analyze failed: ") + e.what());
    } catch (...) {
      LogError("data_flow::Analyze failed");
    }

    try {
      control_flow::Analyze(ctx,
          &out_->cfg_nodes, &out_->cfg_edges, &out_->referenced_files);
    } catch (const std::exception& e) {
      LogError(std::string("control_flow::Analyze failed: ") + e.what());
    } catch (...) {
      LogError("control_flow::Analyze failed");
    }
  }

private:
  _CombinedOut* out_;
};

class CombinedAction : public clang::ASTFrontendAction {
public:
  explicit CombinedAction(_CombinedOut* out) : out_(out) {}

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance& /*ci*/,
                    llvm::StringRef /*file*/) override {
    return std::make_unique<CombinedConsumer>(out_);
  }

private:
  _CombinedOut* out_;
};

class CombinedActionFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit CombinedActionFactory(_CombinedOut* out) : out_(out) {}
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<CombinedAction>(out_);
  }
private:
  _CombinedOut* out_;
};

}  // namespace

bool RunAllAnalysesOnTU(const TUEntry& tu, _CombinedOut& out) {
  // Build compile flags for FixedCompilationDatabase.
  // FixedCompilationDatabase prepends "clang-tool" as argv[0] automatically,
  // so we must strip: compiler executable (argv[0]), -o <output>, -c, source file.
  std::vector<std::string> args;
  args.reserve(tu.arguments.size());

  const auto& raw = tu.arguments;
  bool skip_next = false;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (skip_next) { skip_next = false; continue; }
    const std::string& a = raw[i];

    // Skip compiler executable (first token looks like a path or known compiler name)
    if (i == 0) {
      // If starts with '/' or is a compiler name, skip
      if (!a.empty() && (a[0] == '/' || a == "cc" || a == "c++" ||
                         a == "clang" || a == "clang++")) continue;
    }

    // Skip -o <output>
    if (a == "-o") { skip_next = true; continue; }
    // Skip -o<output> (no space)
    if (a.size() > 2 && a[0] == '-' && a[1] == 'o' && a[2] != 'b') continue;

    // Skip -c (compile-only flag; ClangTool handles this internally)
    if (a == "-c") continue;

    // Skip the source file itself (ClangTool adds it)
    if (a == tu.source_file) continue;

    // Skip .o output files
    if (a.size() > 2 && a.substr(a.size() - 2) == ".o") continue;

    args.push_back(a);
  }

  // Inject system include paths (§5.2)
  auto inc_args = ClangIncludeArgs();
  for (const auto& a : inc_args) args.push_back(a);

  clang::tooling::FixedCompilationDatabase cdb(tu.directory, args);
  std::vector<std::string> sources = {tu.source_file};

  clang::tooling::ClangTool tool(cdb, sources);

  // Suppress diagnostics to stderr (redirect to /dev/null)
  tool.setDiagnosticConsumer(new clang::IgnoringDiagConsumer());

  CombinedActionFactory factory(&out);
  int rc = tool.run(&factory);
  // rc == 1 can mean warnings/errors in source; we still use results
  return (rc == 0 || !out.symbols.empty() || !out.classes.empty());
}

}  // namespace combined
}  // namespace codexray

#else  // !CODEXRAY_HAVE_CLANG

namespace codexray {
namespace combined {

bool RunAllAnalysesOnTU(const TUEntry& /*tu*/, _CombinedOut& /*out*/) {
  LogError("Clang not available — cannot analyze TU");
  return false;
}

}  // namespace combined
}  // namespace codexray

#endif  // CODEXRAY_HAVE_CLANG
