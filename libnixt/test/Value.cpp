#include "StateTest.h"

#include "nixt/Value.h"

using namespace nixt;

namespace {

struct ValueTest : StateTest {
  nix::SourcePath cwd() { return State->rootPath("."); }
  std::vector<nix::Symbol> path(const std::vector<std::string> &Names) {
    return toSymbols(State->symbols, Names);
  }
};

TEST_F(ValueTest, IsOption_neg) {
  nix::Expr *AST = State->parseExprFromString("1", cwd());
  nix::Value V;
  State->eval(AST, V);

  ASSERT_FALSE(isOption(*State, V));
}

TEST_F(ValueTest, IsOption_pos) {
  nix::Expr *AST =
      State->parseExprFromString(R"({ _type = "option"; })", cwd());
  nix::Value V;
  State->eval(AST, V);

  ASSERT_TRUE(isOption(*State, V));
}

TEST_F(ValueTest, IsDerivation_neg) {
  nix::Expr *AST =
      State->parseExprFromString(R"({ _type = "option"; })", cwd());
  nix::Value V;
  State->eval(AST, V);

  ASSERT_FALSE(isDerivation(*State, V));
}

TEST_F(ValueTest, IsDerivation_pos) {
  nix::Expr *AST =
      State->parseExprFromString(R"({ type = "derivation"; })", cwd());
  nix::Value V;
  State->eval(AST, V);

  ASSERT_TRUE(isDerivation(*State, V));
}

TEST_F(ValueTest, selectAttrPath) {
  nix::Expr *AST = State->parseExprFromString(R"({ a.b.c.d = 1; })", cwd());
  nix::Value V;
  State->eval(AST, V);

  nix::Value &Nested = selectStringViews(*State, V, {"a", "b"});

  // Make sure no extra "force" performed.
  ASSERT_EQ(Nested.type(), nix::ValueType::nThunk);

  nix::Value &Kern = selectStringViews(*State, Nested, {"c", "d"});

  ASSERT_EQ(Kern.type(), nix::ValueType::nInt);
  ASSERT_EQ(Kern.integer(), nix::NixInt{1});
}

/// An option tree whose `nested` option has a bare `submodule` type.
constexpr const char *SubmoduleOptions = R"({
  nested = {
    _type = "option";
    description = "a bare submodule";
    type = {
      _type = "option-type";
      name = "submodule";
      getSubOptions = _: {
        inner = { _type = "option"; description = "inside"; };
      };
    };
  };
})";

TEST_F(ValueTest, selectOptions_keepTerminalSubmodule) {
  nix::Expr *AST = State->parseExprFromString(SubmoduleOptions, cwd());
  nix::Value V;
  State->eval(AST, V);

  nix::Value Nested =
      selectOptions(*State, V, path({"nested"}), TerminalSubmodule::Keep);

  ASSERT_TRUE(isOption(*State, Nested));
  ASSERT_EQ(getFieldString(*State, Nested, "description"), "a bare submodule");
}

TEST_F(ValueTest, selectOptions_walkThroughLeavesTreeIntact) {
  nix::Expr *AST = State->parseExprFromString(SubmoduleOptions, cwd());
  nix::Value V;
  State->eval(AST, V);

  nix::Value Inner = selectOptions(*State, V, path({"nested", "inner"}),
                                   TerminalSubmodule::Keep);
  ASSERT_EQ(getFieldString(*State, Inner, "description"), "inside");

  // Walking through `nested` used to write its sub-options over it in the tree.
  nix::Value Nested =
      selectOptions(*State, V, path({"nested"}), TerminalSubmodule::Keep);
  ASSERT_TRUE(isOption(*State, Nested));
  ASSERT_EQ(getFieldString(*State, Nested, "description"), "a bare submodule");
}

} // namespace
