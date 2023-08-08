// Copyright (C) 2022-2023 Exaloop Inc. <https://exaloop.io>

#include "codon/parser/ast.h"
#include "codon/parser/common.h"
#include "codon/parser/visitors/typecheck/typecheck.h"

using fmt::format;

namespace codon::ast {

using namespace types;

/// Transform asserts.
/// @example
///   `assert foo()` ->
///   `if not foo(): raise __internal__.seq_assert([file], [line], "")`
///   `assert foo(), msg` ->
///   `if not foo(): raise __internal__.seq_assert([file], [line], str(msg))`
/// Use `seq_assert_test` instead of `seq_assert` and do not raise anything during unit
/// testing (i.e., when the enclosing function is marked with `@test`).
void TypecheckVisitor::visit(AssertStmt *stmt) {
  ExprPtr msg = N<StringExpr>("");
  if (stmt->message)
    msg = N<CallExpr>(N<IdExpr>("str"), clone(stmt->message));
  auto test = ctx->inFunction() && (ctx->getBase()->attributes &&
                                    ctx->getBase()->attributes->has(Attr::Test));
  auto ex = N<CallExpr>(
      N<DotExpr>("__internal__", test ? "seq_assert_test" : "seq_assert"),
      N<StringExpr>(stmt->getSrcInfo().file), N<IntExpr>(stmt->getSrcInfo().line), msg);
  auto cond = N<UnaryExpr>("!", clone(stmt->expr));
  if (test) {
    resultStmt = transform(N<IfStmt>(cond, N<ExprStmt>(ex)));
  } else {
    resultStmt = transform(N<IfStmt>(cond, N<ThrowStmt>(ex)));
  }
}

/// Typecheck try-except statements. Handle Python exceptions separately.
/// @example
///   ```try: ...
///      except python.Error as e: ...
///      except PyExc as f: ...
///      except ValueError as g: ...
///   ``` -> ```
///      try: ...
///      except ValueError as g: ...                   # ValueError
///      except PyExc as exc:
///        while True:
///          if isinstance(exc.pytype, python.Error):  # python.Error
///            e = exc.pytype; ...; break
///          f = exc; ...; break                       # PyExc
///          raise```
void TypecheckVisitor::visit(TryStmt *stmt) {
  ctx->blockLevel++;
  transform(stmt->suite);
  ctx->blockLevel--;

  std::vector<TryStmt::Catch> catches;
  auto pyVar = ctx->cache->getTemporaryVar("pyexc");
  auto pyCatchStmt = N<WhileStmt>(N<BoolExpr>(true), N<SuiteStmt>());

  auto done = stmt->suite->isDone();
  for (auto &c : stmt->catches) {
    TypeContext::Item val = nullptr;
    if (!c.var.empty()) {
      if (!c.exc->hasAttr(ExprAttr::Dominated))
        val = ctx->addVar(c.var, ctx->generateCanonicalName(c.var), ctx->getUnbound());
      else
        val = ctx->forceFind(c.var);
      c.var = val->canonicalName;
    }
    transform(c.exc);
    if (c.exc && c.exc->type->is("pyobj")) {
      // Transform python.Error exceptions
      if (!c.var.empty()) {
        c.suite = N<SuiteStmt>(
            N<AssignStmt>(N<IdExpr>(c.var), N<DotExpr>(N<IdExpr>(pyVar), "pytype")),
            c.suite);
      }
      c.suite =
          N<IfStmt>(N<CallExpr>(N<IdExpr>("isinstance"),
                                N<DotExpr>(N<IdExpr>(pyVar), "pytype"), clone(c.exc)),
                    N<SuiteStmt>(c.suite, N<BreakStmt>()), nullptr);
      pyCatchStmt->suite->getSuite()->stmts.push_back(c.suite);
    } else if (c.exc && c.exc->type->is("std.internal.types.error.PyError")) {
      // Transform PyExc exceptions
      if (!c.var.empty()) {
        c.suite =
            N<SuiteStmt>(N<AssignStmt>(N<IdExpr>(c.var), N<IdExpr>(pyVar)), c.suite);
      }
      c.suite = N<SuiteStmt>(c.suite, N<BreakStmt>());
      pyCatchStmt->suite->getSuite()->stmts.push_back(c.suite);
    } else {
      // Handle all other exceptions
      transformType(c.exc);
      if (val)
        unify(val->type, c.exc->getType());
      ctx->blockLevel++;
      transform(c.suite);
      ctx->blockLevel--;
      done &= (!c.exc || c.exc->isDone()) && c.suite->isDone();
      catches.push_back(c);
    }
  }
  if (!pyCatchStmt->suite->getSuite()->stmts.empty()) {
    // Process PyError catches
    auto exc = NT<IdExpr>("std.internal.types.error.PyError");
    pyCatchStmt->suite->getSuite()->stmts.push_back(N<ThrowStmt>(nullptr));
    TryStmt::Catch c{pyVar, transformType(exc), pyCatchStmt};

    auto val = ctx->addVar(pyVar, pyVar, c.exc->getType());
    unify(val->type, c.exc->getType());
    ctx->blockLevel++;
    transform(c.suite);
    ctx->blockLevel--;
    done &= (!c.exc || c.exc->isDone()) && c.suite->isDone();
    catches.push_back(c);
  }
  stmt->catches = catches;
  if (stmt->finally) {
    ctx->blockLevel++;
    transform(stmt->finally);
    ctx->blockLevel--;
    done &= stmt->finally->isDone();
  }

  if (done)
    stmt->setDone();
}

/// Transform `raise` statements.
/// @example
///   `raise exc` -> ```raise __internal__.set_header(exc, "fn", "file", line, col)```
void TypecheckVisitor::visit(ThrowStmt *stmt) {
  if (!stmt->expr) {
    stmt->setDone();
    return;
  }

  transform(stmt->expr);

  if (!(stmt->expr->getCall() &&
        stmt->expr->getCall()->expr->isId("__internal__.set_header"))) {
    stmt->expr = transform(N<CallExpr>(
        N<DotExpr>(N<IdExpr>("__internal__"), "set_header"), stmt->expr,
        N<StringExpr>(ctx->getBase()->name), N<StringExpr>(stmt->getSrcInfo().file),
        N<IntExpr>(stmt->getSrcInfo().line), N<IntExpr>(stmt->getSrcInfo().col)));
  }
  if (stmt->expr->isDone())
    stmt->setDone();
}

/// Transform with statements.
/// @example
///   `with foo(), bar() as a: ...` ->
///   ```tmp = foo()
///      tmp.__enter__()
///      try:
///        a = bar()
///        a.__enter__()
///        try:
///          ...
///        finally:
///          a.__exit__()
///      finally:
///        tmp.__exit__()```
void TypecheckVisitor::visit(WithStmt *stmt) {
  seqassert(!stmt->items.empty(), "stmt->items is empty");
  std::vector<StmtPtr> content;
  for (auto i = stmt->items.size(); i-- > 0;) {
    std::string var =
        stmt->vars[i].empty() ? ctx->cache->getTemporaryVar("with") : stmt->vars[i];
    content = std::vector<StmtPtr>{
        N<AssignStmt>(N<IdExpr>(var), clone(stmt->items[i])),
        N<ExprStmt>(N<CallExpr>(N<DotExpr>(var, "__enter__"))),
        N<TryStmt>(
            !content.empty() ? N<SuiteStmt>(content) : clone(stmt->suite),
            std::vector<TryStmt::Catch>{},
            N<SuiteStmt>(N<ExprStmt>(N<CallExpr>(N<DotExpr>(var, "__exit__")))))};
  }
  resultStmt = transform(N<SuiteStmt>(content));
}

} // namespace codon::ast
