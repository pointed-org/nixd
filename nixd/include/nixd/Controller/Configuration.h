/// \file
/// \brief Declares workspace configuration schema
#pragma once

#include <llvm/Support/JSON.h>

#include <map>
#include <string>
#include <vector>

namespace nixd {

// NOLINTBEGIN(readability-identifier-naming)
struct Configuration {
  struct Formatting {
    std::vector<std::string> command = {"nixfmt"};
  } formatting;

  struct OptionProvider {
    /// \brief Expression to eval. Select this attrset as eval .options
    std::string expr;

    /// \brief Re-read this provider when a file in the workspace is saved.
    ///
    /// Off by default. The option tree is otherwise fixed for the lifetime of
    /// the server, so declaring a new option means restarting it — which is
    /// right for `nixosOptions` pinned to a release and wrong for an expression
    /// reading the workspace being edited.
    ///
    /// Reloading replaces the worker rather than re-evaluating inside it: an
    /// evaluator caches every file it has parsed, so asking the same one again
    /// returns the same answer however much the file changed.
    bool reloadOnSave = false;
  };

  std::map<std::string, OptionProvider> options;

  struct NixpkgsProvider {
    /// \brief Expression to eval. Treat it as "import <nixpkgs> { }"
    std::string expr;
  } nixpkgs;

  struct Diagnostic {
    std::vector<std::string> suppress;
  } diagnostic;
};

bool fromJSON(const llvm::json::Value &Params, Configuration::Diagnostic &R,
              llvm::json::Path P);

bool fromJSON(const llvm::json::Value &Params, Configuration::Formatting &R,
              llvm::json::Path P);

bool fromJSON(const llvm::json::Value &Params, Configuration::OptionProvider &R,
              llvm::json::Path P);

bool fromJSON(const llvm::json::Value &Params,
              Configuration::NixpkgsProvider &R, llvm::json::Path P);

bool fromJSON(const llvm::json::Value &Params, Configuration &R,
              llvm::json::Path P);

// NOLINTEND(readability-identifier-naming)

} // namespace nixd
