#include "nixd/Controller/Controller.h"

#include <atomic>
#include <utility>
#include "nixd/Eval/Launch.h"

#include <boost/asio/post.hpp>

using namespace nixd;
using namespace lspserver;
using llvm::json::ObjectMapper;
using llvm::json::Value;

bool nixd::fromJSON(const Value &Params, Configuration::Diagnostic &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O && O.mapOptional("suppress", R.suppress);
}

bool nixd::fromJSON(const Value &Params, Configuration::Formatting &R,
                    llvm::json::Path P) {
  // If it is a single string, treat it as a single vector
  if (auto Str = Params.getAsString()) {
    R.command = {Str->str()};
    return true;
  }
  ObjectMapper O(Params, P);
  return O && O.mapOptional("command", R.command);
}

bool nixd::fromJSON(const Value &Params, Configuration::OptionProvider &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O && O.mapOptional("expr", R.expr) &&
         O.mapOptional("reloadOnSave", R.reloadOnSave);
}

bool nixd::fromJSON(const Value &Params, Configuration::NixpkgsProvider &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O && O.mapOptional("expr", R.expr);
}

bool nixd::fromJSON(const Value &Params, Configuration &R, llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O                                            //
         && O.mapOptional("formatting", R.formatting) //
         && O.mapOptional("options", R.options)       //
         && O.mapOptional("nixpkgs", R.nixpkgs)       //
         && O.mapOptional("diagnostic", R.diagnostic) //
      ;
}

void Controller::onDidSave(const lspserver::DidSaveTextDocumentParams &) {
  // The option tree is otherwise fixed for the server's lifetime. Which
  // providers want this is their own configuration; one reading a pinned
  // release wants nothing to do with it.
  reloadOptions();
}

void Controller::onDidChangeConfiguration(
    const DidChangeConfigurationParams &Params) {
  // FIXME: incrementally change?
  fetchConfig();
}

void Controller::reloadOptions() {
  std::vector<std::pair<std::string, std::string>> Reload;
  {
    std::lock_guard CG(ConfigLock);
    std::lock_guard OG(OptionsLock);

    if (OptionsReloading) {
      // One is already running. Whatever prompted this will be picked up when
      // it finishes, because evaluation reads the files as they are then.
      OptionsReloadPending = true;
      return;
    }

    for (const auto &[Name, Opt] : Config.options) {
      if (Opt.reloadOnSave)
        Reload.emplace_back(Name, Opt.expr);
    }
    if (Reload.empty())
      return;

    OptionsReloading = true;
  }

  auto Remaining = std::make_shared<std::atomic<size_t>>(Reload.size());

  auto Finish = [this, Remaining]() {
    if (--*Remaining != 0)
      return;
    bool Again = false;
    {
      std::lock_guard OG(OptionsLock);
      OptionsReloading = false;
      Again = std::exchange(OptionsReloadPending, false);
    }
    if (Again)
      reloadOptions();
  };

  for (const auto &[Name, Expr] : Reload) {
    // Built beside the worker it replaces, not in place of it. The one already
    // serving keeps answering — with the tree it has, which is the previous
    // state of the workspace rather than nothing — until the replacement has
    // evaluated. Swapping first would answer every request arriving during the
    // evaluation from a worker that has evaluated nothing, and in an editor
    // that reads as a type flickering away and coming back.
    auto Fresh = std::make_shared<std::unique_ptr<AttrSetClientProc>>();
    startOption(Name, *Fresh);

    AttrSetClient *Client = *Fresh ? (*Fresh)->client() : nullptr;
    if (!Client) {
      Finish();
      continue;
    }

    evalExprWithProgress(*Client, Expr, Name + " (reload)",
                         [this, Name, Fresh, Finish](bool Ok) {
                           if (Ok) {
                             std::lock_guard OG(OptionsLock);
                             // Destroys the old one, whose destructor exits it.
                             Options[Name] = std::move(*Fresh);
                           }
                           // On failure the replacement is dropped instead, and
                           // what was working goes on working.
                           Finish();
                         });
  }
}

void Controller::updateConfig(Configuration NewConfig) {
  std::lock_guard G(ConfigLock);
  Config = std::move(NewConfig);

  if (!Config.nixpkgs.expr.empty()) {
    /// Evaluate nixpkgs and options, using user-provided config.
    if (nixpkgsClient()) {
      evalExprWithProgress(*nixpkgsClient(), Config.nixpkgs.expr,
                           "nixpkgs entries");
    }
  }
  if (!Config.options.empty()) {
    std::lock_guard _(OptionsLock);
    // For each option configuration, update the worker.
    for (const auto &[Name, Opt] : Config.options) {
      auto &Client = Options[Name];
      if (!Client) {
        // If it does not exist. Launch a new client.
        startOption(Name, Client);
      }
      assert(Client);
      evalExprWithProgress(*Client->client(), Opt.expr, Name);
    }
  }

  // Update the diagnostic part.
  updateSuppressed(Config.diagnostic.suppress);

  // After all, notify all AST modules the diagnostic set has been updated.
  std::lock_guard TUsGuard(TUsLock);
  for (const auto &[File, TU] : TUs) {
    publishDiagnostics(File, std::nullopt, TU->src(), TU->diagnostics());
  }
}

void Controller::fetchConfig() {
  auto Action = [this](llvm::Expected<llvm::json::Value> Resp) mutable {
    if (!Resp) {
      elog("workspace/configuration: {0}", Resp.takeError());
      return;
    }

    // LSP response is a json array, just take the first.
    if (Resp->kind() != llvm::json::Value::Array) {
      lspserver::elog("workspace/configuration response is not an array: {0}",
                      *Resp);
      return;
    }
    const Value &FirstConfig = Resp->getAsArray()->front();

    // Run this job in the thread pool. Don't block input thread.
    auto ConfigAction = [this, FirstConfig]() mutable {
      // Parse the config
      Configuration NewConfig;
      llvm::json::Path::Root P;
      if (!fromJSON(FirstConfig, NewConfig, P)) {
        elog("workspace/configuration: parse error {0}", P.getError());
        return;
      }

      // OK, update the config
      updateConfig(std::move(NewConfig));
    };

    boost::asio::post(Pool, std::move(ConfigAction));
  };
  workspaceConfiguration({.items = {ConfigurationItem{.section = "nixd"}}},
                         std::move(Action));
}

void Controller::workspaceConfiguration(
    const lspserver::ConfigurationParams &Params,
    lspserver::Callback<llvm::json::Value> Reply) {
  if (ClientCaps.WorkspaceConfiguration) {
    WorkspaceConfiguration(Params, std::move(Reply));
  } else {
    Reply(lspserver::error("client does not support workspace configuration"));
  }
}
