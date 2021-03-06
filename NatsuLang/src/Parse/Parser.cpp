﻿#include "Parse/Parser.h"
#include "Sema/Sema.h"
#include "Sema/Scope.h"
#include "Sema/Declarator.h"
#include "Sema/CompilerAction.h"
#include "AST/Type.h"
#include "AST/ASTContext.h"
#include "AST/ASTConsumer.h"
#include "AST/Expression.h"
#include "AST/Declaration.h"

using namespace NatsuLib;
using namespace NatsuLang;
using namespace Syntax;
using namespace Lex;
using namespace Diag;

ResolveContext::~ResolveContext()
{
}

void ResolveContext::StartResolvingDeclarator(Declaration::DeclaratorPtr decl)
{
	m_ResolvingDeclarators.emplace(std::move(decl));
}

void ResolveContext::EndResolvingDeclarator(Declaration::DeclaratorPtr const& decl)
{
	assert(decl && (!decl->GetDecl() || (decl->IsAlias() || decl->GetDecl()->GetType() != Declaration::Decl::Unresolved)));
	m_ResolvedDeclarators.emplace(decl);
	m_ResolvingDeclarators.erase(decl);
}

ResolveContext::ResolvingState ResolveContext::GetDeclaratorResolvingState(
	Declaration::DeclaratorPtr const& decl) const noexcept
{
	if (m_ResolvingDeclarators.find(decl) != m_ResolvingDeclarators.cend())
	{
		return ResolvingState::Resolving;
	}

	if (m_ResolvedDeclarators.find(decl) != m_ResolvedDeclarators.cend())
	{
		return ResolvingState::Resolved;
	}

	return ResolvingState::Unknown;
}

Parser::Parser(Preprocessor& preprocessor, Semantic::Sema& sema)
	: m_Preprocessor{ preprocessor }, m_Diag{ preprocessor.GetDiag() }, m_Sema{ sema }, m_ParenCount{}, m_BracketCount{},
	  m_BraceCount{}
{
	ConsumeToken();
}

Parser::~Parser()
{
}

Preprocessor& Parser::GetPreprocessor() const noexcept
{
	return m_Preprocessor;
}

DiagnosticsEngine& Parser::GetDiagnosticsEngine() const noexcept
{
	return m_Diag;
}

Semantic::Sema& Parser::GetSema() const noexcept
{
	return m_Sema;
}

#if PARSER_USE_EXCEPTION
Expression::ExprPtr Parser::ParseExprError()
{
	nat_Throw(ParserException, "An error occured while parsing expression.");
}

Statement::StmtPtr Parser::ParseStmtError()
{
	nat_Throw(ParserException, "An error occured while parsing statement.");
}

Declaration::DeclPtr Parser::ParseDeclError()
{
	nat_Throw(ParserException, "An error occured while parsing declaration.");
}
#else
Expression::ExprPtr Parser::ParseExprError() noexcept
{
	return nullptr;
}

Statement::StmtPtr Parser::ParseStmtError() noexcept
{
	return nullptr;
}

Declaration::DeclPtr Parser::ParseDeclError() noexcept
{
	return nullptr;
}
#endif

void Parser::DivertPhase(std::vector<Declaration::DeclPtr>& decls)
{
	m_ResolveContext = make_ref<ResolveContext>(*this);
	const auto scope = make_scope([this]
	{
		m_ResolveContext.Reset();
	});

	// 假装是 1 阶段，这样可以分析到 UnresolvedDecl，并和其他 UnresolvedDecl 一起再分析，也可以坚持直接进行解析，但是这样无法访问其他同样位于 CompilerAction 参数中的声明
	for (auto&& cachedCompilerAction : m_CachedCompilerActions)
	{
		auto cachedScope = cachedCompilerAction.Scope;
		const auto tempUnsafe = cachedCompilerAction.InUnsafeScope && !cachedScope->HasFlags(Semantic::ScopeFlags::UnsafeScope);
		const auto recoveryScope = make_scope(
			[this, curScope = m_Sema.GetCurrentScope(), curDeclContext = m_Sema.GetDeclContext(), tempUnsafe]
		{
			if (tempUnsafe)
			{
				m_Sema.GetCurrentScope()->RemoveFlags(Semantic::ScopeFlags::UnsafeScope);
			}
			m_Sema.SetDeclContext(curDeclContext);
			m_Sema.SetCurrentScope(curScope);
		});
		m_Sema.SetCurrentScope(cachedScope);
		m_Sema.SetDeclContext(cachedCompilerAction.DeclContext);
		if (tempUnsafe)
		{
			cachedScope->AddFlags(Semantic::ScopeFlags::UnsafeScope);
		}

		pushCachedTokens(move(cachedCompilerAction.Tokens));
		const auto compilerActionScope = make_scope([this]
		{
			popCachedTokens();
		});

		ParseCompilerAction(cachedCompilerAction.Context, [&decls](natRefPointer<ASTNode> const& astNode)
		{
			if (auto decl = astNode.Cast<Declaration::Decl>(); decl && decl->GetType() != Declaration::Decl::Unresolved)
			{
				decls.emplace_back(std::move(decl));
				return false;
			}

			// TODO: 报告错误：编译器动作插入了声明以外的 AST
			return true;
		});

		/*if (m_CurrentToken.Is(TokenType::Semi))
		{
			ConsumeToken();
		}
		else
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				.AddArgument(TokenType::Semi)
				.AddArgument(m_CurrentToken.GetType());
		}*/
	}

	m_CachedCompilerActions.clear();

	m_Sema.SetCurrentPhase(Semantic::Sema::Phase::Phase2);

	for (const auto& declPtr : m_Sema.GetCachedDeclarators())
	{
		if (m_ResolveContext->GetDeclaratorResolvingState(declPtr) == ResolveContext::ResolvingState::Unknown)
		{
			ResolveDeclarator(declPtr);
		}
	}

	for (const auto& declPtr : m_ResolveContext->GetResolvedDeclarators())
	{
		decls.emplace_back(declPtr->GetDecl());
	}

	m_Sema.ActOnPhaseDiverted();
}

nBool Parser::ParseTopLevelDecl(std::vector<Declaration::DeclPtr>& decls)
{
	switch (m_CurrentToken.GetType())
	{
	case TokenType::Kw_import:
		// 不可以引入本翻译单元的模块，因此不考虑延迟分析
		decls = ParseModuleImport();
		return false;
	case TokenType::Eof:
		return true;
	case TokenType::Kw_unsafe:
	{
		ConsumeToken();

		const auto curScope = m_Sema.GetCurrentScope();
		curScope->AddFlags(Semantic::ScopeFlags::UnsafeScope);
		const auto scope = make_scope([curScope]
		{
			curScope->RemoveFlags(Semantic::ScopeFlags::UnsafeScope);
		});

		if (!m_CurrentToken.Is(TokenType::LeftBrace))
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				  .AddArgument(TokenType::LeftBrace)
				  .AddArgument(m_CurrentToken.GetType());
			// 假设漏写了左大括号，继续分析
		}
		else
		{
			ConsumeBrace();
		}

		std::vector<Declaration::DeclPtr> curResult;
		while (!m_CurrentToken.Is(TokenType::RightBrace))
		{
			const auto encounteredEof = ParseTopLevelDecl(curResult);

			decls.insert(decls.end(), std::make_move_iterator(curResult.begin()), std::make_move_iterator(curResult.end()));

			if (encounteredEof)
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF, m_CurrentToken.GetLocation());
				return true;
			}
		}

		ConsumeBrace();
		return false;
	}
	default:
		break;
	}

	decls = ParseExternalDeclaration();
	return false;
}

void Parser::SkipSimpleCompilerAction(Declaration::Context context)
{
	std::vector<Token> cachedTokens;
	SkipUntil({ TokenType::Semi }, false, &cachedTokens);
	auto curScope = m_Sema.GetCurrentScope();
	const auto unsafeScope = curScope->HasFlags(Semantic::ScopeFlags::UnsafeScope);
	m_CachedCompilerActions.push_back({ context, std::move(curScope), m_Sema.GetDeclContext(), unsafeScope, move(cachedTokens) });
}

std::vector<Declaration::DeclPtr> Parser::ParseExternalDeclaration(Declaration::Context context)
{
	switch (m_CurrentToken.GetType())
	{
	case TokenType::Kw_module:
		return { ParseModuleDecl() };
	case TokenType::Dollar:
		SkipSimpleCompilerAction(context);
		return {};
	case TokenType::Semi:
		ConsumeToken();
		return {};
	case TokenType::RightBrace:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExtraneousClosingBrace, m_CurrentToken.GetLocation());
		ConsumeBrace();
		return {};
	case TokenType::Eof:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF, m_CurrentToken.GetLocation());
		return {};
	case TokenType::Kw_def:
	case TokenType::Kw_alias:
	{
		SourceLocation declEnd;

		auto decl = ParseDeclaration(context, declEnd);
		if (decl->GetType() == Declaration::Decl::Unresolved)
		{
			return {};
		}

		return { std::move(decl) };
	}
	case TokenType::Kw_class:
		return { ParseClassDeclaration() };
	case TokenType::Kw_enum:
		return { ParseEnumDeclaration() };
	default:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
			  .AddArgument(m_CurrentToken.GetType());

		// 吃掉 1 个 Token 以保证不会死循环
		ConsumeToken();
		return {};
	}
}

void Parser::ParseCompilerActionArguments(Declaration::Context context, const natRefPointer<IActionContext>& actionContext)
{
	const auto argRequirement = actionContext->GetArgumentRequirement();

	if (m_CurrentToken.Is(TokenType::LeftParen))
	{
		ParseCompilerActionArgumentList(actionContext, context, argRequirement);
	}

	if (!m_CurrentToken.IsAnyOf({ TokenType::Semi, TokenType::LeftBrace }))
	{
		ParseCompilerActionArgument(actionContext, context, true, argRequirement);
	}

	if (m_CurrentToken.Is(TokenType::LeftBrace))
	{
		ParseCompilerActionArgumentSequence(actionContext, context, argRequirement);
	}
}

// compiler-action:
//	'$' compiler-action-name ['(' compiler-action-argument-list ')'] [compiler-action-argument] ['{' compiler-action-argument-seq '}'] [;]
// compiler-action-name:
//	[compiler-action-namespace-specifier] compiler-action-id
// compiler-action-namespace-specifier:
//	compiler-action-namespace-id '.'
//	compiler-action-namespace-specifier compiler-action-namespace-id '.'
void Parser::ParseCompilerAction(Declaration::Context context, std::function<nBool(natRefPointer<ASTNode>)> const& output)
{
	assert(m_CurrentToken.Is(TokenType::Dollar));
	ConsumeToken();

	const auto action = ParseCompilerActionName();

	if (!action)
	{
		return;
	}

	const auto actionContext = action->StartAction(CompilerActionContext{ *this });
	const auto scope = make_scope([&action, &actionContext, &output]
	{
		action->EndAction(actionContext, output);
	});

	ParseCompilerActionArguments(context, actionContext);
}

natRefPointer<ICompilerAction> Parser::ParseCompilerActionName()
{
	auto actionNamespace = m_Sema.GetTopLevelActionNamespace();

	while (m_CurrentToken.Is(TokenType::Identifier))
	{
		const auto id = m_CurrentToken.GetIdentifierInfo();
		ConsumeToken();
		if (m_CurrentToken.Is(TokenType::Period))
		{
			actionNamespace = actionNamespace->GetSubNamespace(id->GetName());
			if (!actionNamespace)
			{
				// TODO: 报告错误
				return nullptr;
			}
			ConsumeToken();
		}
		else
		{
			return actionNamespace->GetAction(id->GetName());
		}
	}

	m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
		  .AddArgument(m_CurrentToken.GetType());
	return nullptr;
}

nBool Parser::ParseCompilerActionArgument(const natRefPointer<IActionContext>& actionContext, Declaration::Context context, nBool isSingle, natRefPointer<IArgumentRequirement> argRequirement, CompilerActionArgumentType argType)
{
	if (!argRequirement)
	{
		argRequirement = actionContext->GetArgumentRequirement();
	}

	if (argType == CompilerActionArgumentType::None)
	{
		argType = argRequirement->GetNextExpectedArgumentType();
		if (argType == CompilerActionArgumentType::None || (isSingle && !HasAnyFlags(argType, CompilerActionArgumentType::MayBeSingle)))
		{
			return false;
		}
	}

	assert(GetCategoryPart(argType) != CompilerActionArgumentType::None && "argType should have at least one category flag set");

	// TODO: 替换成正儿八经的实现
	// 禁止匹配过程中的错误报告
	m_Diag.EnableDiag(false);
	const auto scope = make_scope([this]
	{
		m_Diag.EnableDiag(true);
	});

	// 最优先尝试匹配标识符
	// 如果标识符后面有非分隔符或者结束符的 Token 的话，将由调用者报告错误
	if (HasAnyFlags(argType, CompilerActionArgumentType::Identifier) && m_CurrentToken.Is(TokenType::Identifier))
	{
		actionContext->AddArgument(m_Sema.ActOnCompilerActionIdentifierArgument(m_CurrentToken.GetIdentifierInfo()));
		ConsumeToken();
		return true;
	}

	// 记录状态以便匹配失败时还原
	const auto memento = m_Preprocessor.SaveToMemento();
	const auto curToken = m_CurrentToken;

	if (HasAnyFlags(argType, CompilerActionArgumentType::Type))
	{
		const auto typeDecl = make_ref<Declaration::Declarator>(Declaration::Context::TypeName);
		ParseType(typeDecl);
		const auto type = typeDecl->GetType();
		if (type)
		{
			actionContext->AddArgument(type);
			return true;
		}
	}

	// 匹配类型失败了，还原 Preprocessor 状态
	m_Preprocessor.RestoreFromMemento(memento);
	m_CurrentToken = curToken;

	if (HasAnyFlags(argType, CompilerActionArgumentType::Declaration))
	{
		SourceLocation end;

		const auto restorePhase = m_Sema.GetCurrentPhase();
		const auto phaseScope = make_scope([this, restorePhase]
		{
			m_Sema.SetCurrentPhase(restorePhase);
		});

		// 真的在 1 阶段的话不会分析 CompilerAction，所以直接设为 2 阶段
		if (!HasAnyFlags(argType, CompilerActionArgumentType::MayBeUnresolved) && restorePhase == Semantic::Sema::Phase::Phase1)
		{
			assert(m_ResolveContext);
			m_Sema.SetCurrentPhase(Semantic::Sema::Phase::Phase2);
		}

		const auto decl = ParseDeclaration(context, end);
		if (decl)
		{
			actionContext->AddArgument(decl);
			return true;
		}
	}

	// 匹配声明失败了，还原 Preprocessor 状态
	m_Preprocessor.RestoreFromMemento(memento);
	m_CurrentToken = curToken;

	if (HasAnyFlags(argType, CompilerActionArgumentType::Statement))
	{
		const auto stmt = ParseStatement(context, true);
		if (stmt)
		{
			actionContext->AddArgument(stmt);
			return true;
		}
	}

	// 匹配全部失败，还原 Preprocessor 状态并报告错误
	m_Preprocessor.RestoreFromMemento(memento);
	m_CurrentToken = curToken;
	m_Diag.EnableDiag(true);
	m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
		.AddArgument(m_CurrentToken.GetType());
	return false;
}

std::size_t Parser::ParseCompilerActionArgumentList(const natRefPointer<IActionContext>& actionContext, Declaration::Context context, natRefPointer<IArgumentRequirement> argRequirement, CompilerActionArgumentType argType)
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));
	ConsumeParen();

	if (!argRequirement)
	{
		argRequirement = actionContext->GetArgumentRequirement();
	}

	if (argType == CompilerActionArgumentType::None)
	{
		argType = argRequirement->GetNextExpectedArgumentType();
		if (argType == CompilerActionArgumentType::None)
		{
			return 0;
		}
	}

	if (m_CurrentToken.Is(TokenType::RightParen))
	{
		ConsumeParen();
		if (argType == CompilerActionArgumentType::None || HasAnyFlags(argType, CompilerActionArgumentType::Optional))
		{
			return 0;
		}

		// TODO: 参数过少，或者之后的参数由其他形式补充
		return 0;
	}

	std::size_t i = 0;
	for (;; ++i)
	{
		if (argType == CompilerActionArgumentType::None)
		{
			break;
		}

		if (m_CurrentToken.Is(TokenType::Comma))
		{
			if ((argType & CompilerActionArgumentType::Optional) != CompilerActionArgumentType::None)
			{
				actionContext->AddArgument(nullptr);
				ConsumeToken();
				continue;
			}

			// TODO: 报告错误：未为非可选的参数提供值，假设这个逗号是多余的
			ConsumeToken();
		}

		if (!ParseCompilerActionArgument(actionContext, context, false, argRequirement, argType))
		{
			// 匹配失败，报告错误
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
				.AddArgument(m_CurrentToken.GetType());
			break;
		}

		if (m_CurrentToken.Is(TokenType::Comma))
		{
			ConsumeToken();
		}

		if (m_CurrentToken.Is(TokenType::RightParen))
		{
			ConsumeParen();
			if (argType == CompilerActionArgumentType::None || HasAnyFlags(argType, CompilerActionArgumentType::Optional))
			{
				break;
			}

			// TODO: 参数过少，或者之后的参数由其他形式补充
			break;
		}

		argType = argRequirement->GetNextExpectedArgumentType();
	}

	return i;
}

std::size_t Parser::ParseCompilerActionArgumentSequence(const natRefPointer<IActionContext>& actionContext, Declaration::Context context, natRefPointer<IArgumentRequirement> argRequirement, CompilerActionArgumentType argType)
{
	assert(m_CurrentToken.Is(TokenType::LeftBrace));
	ConsumeBrace();

	if (!argRequirement)
	{
		argRequirement = actionContext->GetArgumentRequirement();
	}

	if (argType == CompilerActionArgumentType::None)
	{
		argType = argRequirement->GetNextExpectedArgumentType();
		if (argType == CompilerActionArgumentType::None || !HasAnyFlags(argType, CompilerActionArgumentType::MayBeSeq))
		{
			return 0;
		}
	}

	if (m_CurrentToken.Is(TokenType::RightBrace))
	{
		if (argType == CompilerActionArgumentType::None || HasAnyFlags(argType, CompilerActionArgumentType::Optional))
		{
			ConsumeBrace();
			return 0;
		}

		// TODO: 参数过少，或者之后的参数由其他形式补充
		return 0;
	}

	std::size_t i = 0;
	for (;; ++i)
	{
		if (argType == CompilerActionArgumentType::None)
		{
			break;
		}

		if (!ParseCompilerActionArgument(actionContext, context, false, argRequirement, argType))
		{
			// 匹配失败，报告错误
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
				.AddArgument(m_CurrentToken.GetType());
			break;
		}

		if (m_CurrentToken.Is(TokenType::RightBrace))
		{
			if (argType == CompilerActionArgumentType::None || HasAnyFlags(argType, CompilerActionArgumentType::Optional))
			{
				ConsumeBrace();
				break;
			}

			// TODO: 参数过少，或者之后的参数由其他形式补充
			break;
		}

		argType = argRequirement->GetNextExpectedArgumentType();
	}

	return i;
}

// class-declaration:
//	'class' [specifier-seq] identifier '{' [member-specification] '}'
Declaration::DeclPtr Parser::ParseClassDeclaration()
{
	assert(m_CurrentToken.Is(TokenType::Kw_class));

	const auto classKeywordLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	auto accessSpecifier = Specifier::Access::None;
	switch (m_CurrentToken.GetType())
	{
	case TokenType::Kw_public:
		accessSpecifier = Specifier::Access::Public;
		ConsumeToken();
		break;
	case TokenType::Kw_protected:
		accessSpecifier = Specifier::Access::Protected;
		ConsumeToken();
		break;
	case TokenType::Kw_internal:
		accessSpecifier = Specifier::Access::Internal;
		ConsumeToken();
		break;
	case TokenType::Kw_private:
		accessSpecifier = Specifier::Access::Private;
		ConsumeToken();
		break;
	default:
		break;
	}

	if (!m_CurrentToken.Is(TokenType::Identifier))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::Identifier)
			  .AddArgument(m_CurrentToken.GetType());
		return ParseDeclError();
	}

	const auto classId = m_CurrentToken.GetIdentifierInfo();
	const auto classIdLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	auto classDecl = m_Sema.ActOnTag(m_Sema.GetCurrentScope(), Type::TagType::TagTypeClass::Class, classKeywordLoc,
									 accessSpecifier, classId, classIdLoc, nullptr);

	ParseMemberSpecification(classKeywordLoc, classDecl);

	return classDecl;
}

// member-specification:
//	member-declaration [member-specification]
void Parser::ParseMemberSpecification(SourceLocation startLoc, Declaration::DeclPtr const& tagDecl)
{
	ParseScope classScope{ this, Semantic::ScopeFlags::ClassScope | Semantic::ScopeFlags::DeclarableScope };

	m_Sema.ActOnTagStartDefinition(m_Sema.GetCurrentScope(), tagDecl);
	const auto tagScope = make_scope([this]
	{
		m_Sema.ActOnTagFinishDefinition();
	});

	if (m_CurrentToken.Is(TokenType::Colon))
	{
		ConsumeToken();
		// TODO: 实现的 Concept 说明
	}

	if (m_CurrentToken.Is(TokenType::LeftBrace))
	{
		ConsumeBrace();
	}
	else
	{
		// 可能缺失的左大括号
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::LeftBrace)
			  .AddArgument(m_CurrentToken.GetType());
	}

	// 开始成员声明
	while (!m_CurrentToken.Is(TokenType::RightBrace))
	{
		if (m_CurrentToken.Is(TokenType::Kw_def))
		{
			SourceLocation declEnd;
			ParseDeclaration(Declaration::Context::Member, declEnd);
		}
		else if (m_CurrentToken.Is(TokenType::Dollar))
		{
			SkipSimpleCompilerAction(Declaration::Context::Member);
		}
		else
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
				.AddArgument(m_CurrentToken.GetType());
			return;
		}
	}

	ConsumeBrace();
}

Declaration::DeclPtr Parser::ParseEnumDeclaration()
{
	assert(m_CurrentToken.Is(TokenType::Kw_enum));

	const auto enumLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	auto accessSpecifier = Specifier::Access::None;
	switch (m_CurrentToken.GetType())
	{
	case TokenType::Kw_public:
		accessSpecifier = Specifier::Access::Public;
		ConsumeToken();
		break;
	case TokenType::Kw_protected:
		accessSpecifier = Specifier::Access::Protected;
		ConsumeToken();
		break;
	case TokenType::Kw_internal:
		accessSpecifier = Specifier::Access::Internal;
		ConsumeToken();
		break;
	case TokenType::Kw_private:
		accessSpecifier = Specifier::Access::Private;
		ConsumeToken();
		break;
	default:
		break;
	}

	if (!m_CurrentToken.Is(TokenType::Identifier))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Identifier)
			.AddArgument(m_CurrentToken.GetType());
		return ParseDeclError();
	}

	auto enumId = m_CurrentToken.GetIdentifierInfo();
	const auto enumIdLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	Type::TypePtr underlyingType;
	if (m_CurrentToken.Is(TokenType::Colon))
	{
		ConsumeToken();
		const auto decl = make_ref<Declaration::Declarator>(Declaration::Context::TypeName);
		if (!ParseType(decl))
		{
			return ParseDeclError();
		}
		underlyingType = decl->GetType();
	}
	else
	{
		underlyingType = m_Sema.GetASTContext().GetBuiltinType(Type::BuiltinType::Int);
	}

	if (!m_CurrentToken.Is(TokenType::LeftBrace))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::LeftBrace)
			.AddArgument(m_CurrentToken.GetType());
		return ParseDeclError();
	}

	auto enumDecl = m_Sema.ActOnTag(m_Sema.GetCurrentScope(), Type::TagType::TagTypeClass::Enum, enumLoc, accessSpecifier, std::move(enumId), enumIdLoc, std::move(underlyingType));

	ParseEnumeratorList(enumDecl);

	return enumDecl;
}

void Parser::ParseEnumeratorList(natRefPointer<Declaration::EnumDecl> const& tagDecl)
{
	ParseScope scope{ this, Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::EnumScope };

	m_Sema.ActOnTagStartDefinition(m_Sema.GetCurrentScope(), tagDecl);
	const auto tagScope = make_scope([this]
	{
		m_Sema.ActOnTagFinishDefinition();
	});

	if (m_CurrentToken.Is(TokenType::LeftBrace))
	{
		ConsumeBrace();
	}
	else
	{
		// 可能缺失的左大括号
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::LeftBrace)
			.AddArgument(m_CurrentToken.GetType());
	}

	// 开始成员声明
	auto hasNextMemeber = true;
	natRefPointer<Declaration::EnumConstantDecl> lastDecl;
	while (!m_CurrentToken.Is(TokenType::RightBrace))
	{
		if (m_CurrentToken.Is(TokenType::Identifier))
		{
			if (!hasNextMemeber)
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					.AddArgument(TokenType::Comma)
					.AddArgument(m_CurrentToken.GetType());
			}

			auto id = m_CurrentToken.GetIdentifierInfo();
			const auto idLoc = m_CurrentToken.GetLocation();
			ConsumeToken();

			Expression::ExprPtr initializer;
			if (m_CurrentToken.Is(TokenType::Equal))
			{
				ConsumeToken();
				initializer = ParseExpression();
			}

			lastDecl = m_Sema.ActOnEnumerator(m_Sema.GetCurrentScope(), tagDecl, lastDecl, std::move(id), idLoc, std::move(initializer));

			if (m_CurrentToken.Is(TokenType::Comma))
			{
				ConsumeToken();
				hasNextMemeber = true;
			}
			else
			{
				hasNextMemeber = false;
			}
		}
		else
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
				.AddArgument(m_CurrentToken.GetType());
		}
	}

	ConsumeBrace();
}

std::vector<Declaration::DeclPtr> Parser::ParseModuleImport()
{
	assert(m_CurrentToken.Is(Lex::TokenType::Kw_import));
	const auto startLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	const auto qualifiedId = ParseMayBeQualifiedId();

	if (m_CurrentToken.Is(TokenType::CodeCompletion))
	{
		// TODO: 修改 Context
		m_Sema.ActOnCodeComplete(m_Sema.GetCurrentScope(), m_CurrentToken.GetLocation(), qualifiedId.first, qualifiedId.second.first, Declaration::Context::Block);
		ConsumeToken();
		return {};
	}

	if (!qualifiedId.second.first)
	{
		// TODO: 报告错误
		return {};
	}

	if (m_CurrentToken.Is(TokenType::Semi))
	{
		ConsumeToken();
	}
	else
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Semi)
			.AddArgument(m_CurrentToken.GetType());
	}

	const auto module = m_Sema.LookupModuleName(qualifiedId.second.first, {}, m_Sema.GetCurrentScope(), qualifiedId.first);

	if (!module)
	{
		// TODO: 报告错误
		return {};
	}

	return { m_Sema.ActOnModuleImport(m_Sema.GetCurrentScope(), startLoc, startLoc, module) };
}

// module-decl:
//	'module' module-name '{' declarations '}'
Declaration::DeclPtr Parser::ParseModuleDecl()
{
	assert(m_CurrentToken.Is(TokenType::Kw_module));

	const auto startLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	if (!m_CurrentToken.Is(TokenType::Identifier))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::Identifier)
			  .AddArgument(m_CurrentToken.GetType());
		return {};
	}

	auto moduleName = m_CurrentToken.GetIdentifierInfo();
	auto moduleDecl = m_Sema.ActOnModuleDecl(m_Sema.GetCurrentScope(), startLoc, std::move(moduleName));

	{
		ParseScope moduleScope{ this, Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::ModuleScope };
		m_Sema.ActOnStartModule(m_Sema.GetCurrentScope(), moduleDecl);
		const auto scope = make_scope([this]
		{
			m_Sema.ActOnFinishModule();
		});

		ConsumeToken();
		if (m_CurrentToken.Is(TokenType::LeftBrace))
		{
			ConsumeBrace();
		}
		else
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				  .AddArgument(TokenType::LeftBrace)
				  .AddArgument(m_CurrentToken.GetType());
		}

		while (!m_CurrentToken.IsAnyOf({ TokenType::RightBrace, TokenType::Eof }))
		{
			ParseExternalDeclaration(Declaration::Context::Member);
		}

		if (m_CurrentToken.Is(TokenType::Eof))
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF, m_CurrentToken.GetLocation());
		}
		else
		{
			ConsumeBrace();
		}
	}

	return moduleDecl;
}

// declaration:
//	simple-declaration
//	special-member-function-declaration
// simple-declaration:
//	'def' [specifier-seq] declarator [;]
// special-member-function-declaration:
//	'def' 'this' ':' '(' [parameter-declaration-list] ')' function-body
//	'def' '~' 'this' ':' '(' ')' function-body
// function-body:
//	compound-statement
Declaration::DeclPtr Parser::ParseDeclaration(Declaration::Context context, SourceLocation& declEnd)
{
	const auto tokenType = m_CurrentToken.GetType();

	switch (tokenType)
	{
	case TokenType::Kw_def:
	{
		// 吃掉 def
		ConsumeToken();

		const auto decl = make_ref<Declaration::Declarator>(context);
		// 这不意味着 specifier 是 declarator 的一部分，至少目前如此
		if (!ParseSpecifier(decl) || !ParseDeclarator(decl))
		{
			return {};
		}

		auto declaration = m_Sema.HandleDeclarator(m_Sema.GetCurrentScope(), decl);
		return std::move(declaration);
	}
	case TokenType::Kw_alias:
		return ParseAliasDeclaration(context, declEnd);
	default:
		return {};
	}
}

// alias-declaration:
//	'alias' alias-id '=' type-name ';'
//	'alias' alias-id '=' '$' compiler-action-name ';'
// TODO: 若要求别名 CompilerAction 需要特殊符号/语法调用的话会简单很多，以后再改。。。
Declaration::DeclPtr Parser::ParseAliasDeclaration(Declaration::Context context, SourceLocation& declEnd)
{
	assert(m_CurrentToken.Is(TokenType::Kw_alias));

	const auto aliasLoc = m_CurrentToken.GetLocation();

	ConsumeToken();

	if (!m_CurrentToken.Is(TokenType::Identifier))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Identifier)
			.AddArgument(m_CurrentToken.GetType());
		return nullptr;
	}

	auto aliasId = m_CurrentToken.GetIdentifierInfo();
	const auto aliasIdLoc = m_CurrentToken.GetLocation();

	ConsumeToken();

	if (!m_CurrentToken.Is(TokenType::Equal))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Equal)
			.AddArgument(m_CurrentToken.GetType());
		return nullptr;
	}

	ConsumeToken();

	if (m_Sema.GetCurrentPhase() == Semantic::Sema::Phase::Phase1)
	{
		// 假 declarator
		auto declarator = make_ref<Declaration::Declarator>(context);
		declarator->SetRange({ aliasLoc, aliasLoc });
		declarator->SetAlias(true);
		declarator->SetIdentifier(std::move(aliasId));
		declarator->SetIdentifierLocation(aliasIdLoc);
		std::vector<Token> cachedTokens;
		SkipUntil({ TokenType::Semi }, false, &cachedTokens);
		declarator->SetCachedTokens(std::move(cachedTokens));
		return m_Sema.HandleDeclarator(m_Sema.GetCurrentScope(), declarator);
	}

	return ParseAliasBody(aliasLoc, std::move(aliasId), aliasIdLoc, context, declEnd);
}

Declaration::DeclPtr Parser::ParseAliasBody(SourceLocation aliasLoc, Identifier::IdPtr aliasId, SourceLocation aliasIdLoc, Declaration::Context context, SourceLocation& declEnd)
{
	if (m_CurrentToken.Is(TokenType::Dollar))
	{
		ConsumeToken();
		auto compilerAction = ParseCompilerActionName();
		if (!compilerAction)
		{
			// TODO: 报告错误
			return nullptr;
		}

		if (m_CurrentToken.Is(TokenType::Semi))
		{
			ConsumeToken();
			declEnd = m_CurrentToken.GetLocation();
			return m_Sema.ActOnAliasDeclaration(m_Sema.GetCurrentScope(), aliasLoc, std::move(aliasId), aliasIdLoc, std::move(compilerAction));
		}

		const auto actionContext = compilerAction->StartAction(CompilerActionContext{ *this });
		ParseCompilerActionArguments(context, actionContext);
		ASTNodePtr astNode;
		compilerAction->EndAction(actionContext, [&astNode](ASTNodePtr ast)
		{
			if (!astNode)
			{
				astNode = std::move(ast);
			}

			return true;
		});

		if (m_CurrentToken.Is(TokenType::Semi))
		{
			ConsumeToken();
		}
		else
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				.AddArgument(TokenType::Semi)
				.AddArgument(m_CurrentToken.GetType());
		}

		// 直接拿返回值做别名
		return m_Sema.ActOnAliasDeclaration(m_Sema.GetCurrentScope(), aliasLoc, std::move(aliasId), aliasIdLoc, std::move(astNode));
	}

	// 假设是类型

	const auto decl = make_ref<Declaration::Declarator>(Declaration::Context::TypeName);
	ParseType(decl);
	auto type = decl->GetType();
	if (!type)
	{
		// TODO: 报告错误
		return nullptr;
	}

	declEnd = m_CurrentToken.GetLocation();
	return m_Sema.ActOnAliasDeclaration(m_Sema.GetCurrentScope(), aliasLoc, std::move(aliasId), aliasIdLoc, std::move(type));
}

Declaration::DeclPtr Parser::ParseFunctionBody(Declaration::DeclPtr decl, ParseScope& scope)
{
	assert(m_CurrentToken.Is(TokenType::LeftBrace));

	const auto loc = m_CurrentToken.GetLocation();

	auto body{ ParseCompoundStatement() };
	if (!body)
	{
		return ParseDeclError();
	}

	scope.ExplicitExit();
	return m_Sema.ActOnFinishFunctionBody(std::move(decl), std::move(body));
}

Statement::StmtPtr Parser::ParseStatement(Declaration::Context context, nBool mayBeExpr)
{
	Statement::StmtPtr result;
	const auto tokenType = m_CurrentToken.GetType();

	// TODO: 完成根据 tokenType 判断语句类型的过程
	switch (tokenType)
	{
	case TokenType::At:
	{
		ConsumeToken();
		if (m_CurrentToken.Is(TokenType::Identifier))
		{
			auto id = m_CurrentToken.GetIdentifierInfo();
			const auto loc = m_CurrentToken.GetLocation();

			ConsumeToken();

			if (m_CurrentToken.Is(TokenType::Colon))
			{
				return ParseLabeledStatement(std::move(id), loc);
			}
		}

		return ParseStmtError();
	}
	case TokenType::Kw_unsafe:
		ConsumeToken();
		if (!m_CurrentToken.Is(TokenType::LeftBrace))
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				  .AddArgument(TokenType::LeftBrace)
				  .AddArgument(m_CurrentToken.GetType());
			return nullptr;
		}
		return ParseCompoundStatement(
			Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::CompoundStmtScope | Semantic::ScopeFlags::UnsafeScope);
	case TokenType::LeftBrace:
		return ParseCompoundStatement();
	case TokenType::Semi:
	{
		const auto loc = m_CurrentToken.GetLocation();
		ConsumeToken();
		return m_Sema.ActOnNullStmt(loc);
	}
	case TokenType::Kw_def:
	case TokenType::Kw_alias:
	{
		const auto declBegin = m_CurrentToken.GetLocation();
		SourceLocation declEnd;
		auto decl = ParseDeclaration(context, declEnd);
		return m_Sema.ActOnDeclStmt(std::move(decl), declBegin, declEnd);
	}
	case TokenType::Kw_if:
		return ParseIfStatement();
	case TokenType::Kw_while:
		return ParseWhileStatement();
	case TokenType::Kw_for:
		return ParseForStatement();
	case TokenType::Kw_goto:
		break;
	case TokenType::Kw_continue:
		return ParseContinueStatement();
	case TokenType::Kw_break:
		return ParseBreakStatement();
	case TokenType::Kw_return:
		return ParseReturnStatement();
	case TokenType::Kw_try:
		break;
	case TokenType::Kw_catch:
		break;
	case TokenType::Dollar:
		ParseCompilerAction(context, [this, &result](natRefPointer<ASTNode> const& node)
		{
			if (result)
			{
				// 多个语句是不允许的
				return true;
			}

			if (const auto decl = static_cast<Declaration::DeclPtr>(node))
			{
				result = m_Sema.ActOnDeclStmt({ decl }, {}, {});
			}
			else
			{
				result = node;
			}

			return false;
		});
		return result;
	case TokenType::CodeCompletion:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::CodeCompletion);
		ConsumeToken();
		return nullptr;
	case TokenType::Identifier:
	{
		auto curToken = m_CurrentToken;
		const auto memento = m_Preprocessor.SaveToMemento();

		const auto qualifiedId = ParseMayBeQualifiedId();

		if (m_CurrentToken.Is(TokenType::CodeCompletion))
		{
			m_Sema.ActOnCodeComplete(m_Sema.GetCurrentScope(), m_CurrentToken.GetLocation(), qualifiedId.first, qualifiedId.second.first, context);
			ConsumeToken();
			return nullptr;
		}

		if (qualifiedId.second.first)
		{
			const auto foundAlias = m_Sema.LookupAliasName(qualifiedId.second.first, qualifiedId.second.second, m_Sema.GetCurrentScope(), qualifiedId.first, m_ResolveContext);
			if (foundAlias)
			{
				const auto astNode = foundAlias->GetAliasAsAst();
				if (const auto compilerAction = astNode.Cast<ICompilerAction>())
				{
					ConsumeToken();
					const auto actionContext = compilerAction->StartAction(CompilerActionContext{ *this });
					ParseCompilerActionArguments(context, actionContext);
					compilerAction->EndAction(actionContext, [this, &result](natRefPointer<ASTNode> const& node)
					{
						if (result)
						{
							// 多个语句是不允许的
							return true;
						}

						if (const auto decl = static_cast<Declaration::DeclPtr>(node))
						{
							result = m_Sema.ActOnDeclStmt({ decl }, {}, {});
						}
						else
						{
							result = node;
						}

						return false;
					});

					return result;
				}
			}
		}

		// 匹配失败，还原 Preprocessor 状态以及 m_CurrentToken 状态
		// TODO: 是否可以重用以上信息？
		m_Preprocessor.RestoreFromMemento(memento);
		m_CurrentToken = std::move(curToken);
	}
		[[fallthrough]];
	default:
		return ParseExprStatement(mayBeExpr);
	}

	// TODO
	nat_Throw(NotImplementedException);
}

Statement::StmtPtr Parser::ParseLabeledStatement(Identifier::IdPtr labelId, SourceLocation labelLoc)
{
	assert(m_CurrentToken.Is(TokenType::Colon));

	const auto colonLoc = m_CurrentToken.GetLocation();

	ConsumeToken();

	auto stmt = ParseStatement();
	if (!stmt)
	{
		stmt = m_Sema.ActOnNullStmt(colonLoc);
	}

	auto labelDecl = m_Sema.LookupOrCreateLabel(std::move(labelId), labelLoc);
	return m_Sema.ActOnLabelStmt(labelLoc, std::move(labelDecl), colonLoc, std::move(stmt));
}

Statement::StmtPtr Parser::ParseCompoundStatement()
{
	return ParseCompoundStatement(Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::CompoundStmtScope);
}

Statement::StmtPtr Parser::ParseCompoundStatement(Semantic::ScopeFlags flags)
{
	ParseScope scope{ this, flags };

	assert(m_CurrentToken.Is(TokenType::LeftBrace));
	const auto beginLoc = m_CurrentToken.GetLocation();
	ConsumeBrace();
	const auto braceScope = make_scope([this] { ConsumeBrace(); });

	std::vector<Statement::StmtPtr> stmtVec;

	while (!m_CurrentToken.IsAnyOf({ TokenType::RightBrace, TokenType::Eof }))
	{
		if (auto stmt = ParseStatement())
		{
			stmtVec.emplace_back(std::move(stmt));
		}
	}

	return m_Sema.ActOnCompoundStmt(move(stmtVec), beginLoc, m_CurrentToken.GetLocation());
}

Statement::StmtPtr Parser::ParseIfStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_if));
	const auto ifLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	if (!m_CurrentToken.Is(TokenType::LeftParen))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::LeftParen)
			  .AddArgument(m_CurrentToken.GetType());
		return ParseStmtError();
	}

	ParseScope ifScope{ this, Semantic::ScopeFlags::ControlScope };

	auto cond = ParseParenExpression();
	if (!cond)
	{
		return ParseStmtError();
	}

	const auto thenLoc = m_CurrentToken.GetLocation();
	Statement::StmtPtr thenStmt, elseStmt;

	{
		ParseScope thenScope{ this, Semantic::ScopeFlags::DeclarableScope };
		thenStmt = ParseStatement();
	}

	SourceLocation elseLoc, elseStmtLoc;

	if (m_CurrentToken.Is(TokenType::Kw_else))
	{
		elseLoc = m_CurrentToken.GetLocation();

		ConsumeToken();

		elseStmtLoc = m_CurrentToken.GetLocation();

		ParseScope thenScope{ this, Semantic::ScopeFlags::DeclarableScope };
		elseStmt = ParseStatement();
	}

	ifScope.ExplicitExit();

	if (!thenStmt && !elseStmt)
	{
		return ParseStmtError();
	}

	if (!thenStmt)
	{
		thenStmt = m_Sema.ActOnNullStmt(thenLoc);
	}

	if (!elseStmt)
	{
		elseStmt = m_Sema.ActOnNullStmt(elseLoc);
	}

	return m_Sema.ActOnIfStmt(ifLoc, std::move(cond), std::move(thenStmt), elseLoc, std::move(elseStmt));
}

Statement::StmtPtr Parser::ParseWhileStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_while));
	const auto whileLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	if (!m_CurrentToken.Is(TokenType::LeftParen))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::LeftParen)
			  .AddArgument(m_CurrentToken.GetType());
		return ParseStmtError();
	}

	ParseScope whileScope{ this, Semantic::ScopeFlags::BreakableScope | Semantic::ScopeFlags::ContinuableScope };

	auto cond = ParseParenExpression();
	if (!cond)
	{
		return ParseStmtError();
	}

	Statement::StmtPtr body;
	{
		ParseScope innerScope{ this, Semantic::ScopeFlags::DeclarableScope };
		body = ParseStatement();
	}

	whileScope.ExplicitExit();

	if (!body)
	{
		return ParseStmtError();
	}

	return m_Sema.ActOnWhileStmt(whileLoc, std::move(cond), std::move(body));
}

Statement::StmtPtr Parser::ParseForStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_for));
	const auto forLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	if (!m_CurrentToken.Is(TokenType::LeftParen))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::LeftParen)
			  .AddArgument(m_CurrentToken.GetType());
		return ParseStmtError();
	}

	ParseScope forScope{ this, Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::ControlScope };

	SourceLocation leftParenLoc, rightParenLoc;
	Statement::StmtPtr initPart;
	Expression::ExprPtr condPart;
	Expression::ExprPtr thirdPart;

	{
		leftParenLoc = m_CurrentToken.GetLocation();
		ConsumeParen();

		if (!m_CurrentToken.Is(TokenType::Semi))
		{
			// TODO: 不能知道是否吃掉过分号，分号是必要的吗？
			// 由于如果下一个是分号的话会被吃掉，所以至少不需要担心误判空语句
			initPart = ParseStatement(Declaration::Context::For);
		}
		else
		{
			ConsumeToken();
		}

		// 从这里开始可以 break 和 continue 了
		m_Sema.GetCurrentScope()->AddFlags(Semantic::ScopeFlags::BreakableScope | Semantic::ScopeFlags::ContinuableScope);
		if (!m_CurrentToken.Is(TokenType::Semi))
		{
			condPart = ParseExpression();
			if (m_CurrentToken.Is(TokenType::Semi))
			{
				ConsumeToken();
			}
		}
		else
		{
			ConsumeToken();
		}

		if (!m_CurrentToken.Is(TokenType::RightParen))
		{
			thirdPart = ParseExpression();
		}

		if (!m_CurrentToken.Is(TokenType::RightParen))
		{
			return ParseStmtError();
		}

		rightParenLoc = m_CurrentToken.GetLocation();
		ConsumeParen();
	}

	Statement::StmtPtr body;

	{
		ParseScope innerScope{ this, Semantic::ScopeFlags::DeclarableScope };
		body = ParseStatement();
	}

	forScope.ExplicitExit();

	return m_Sema.ActOnForStmt(forLoc, leftParenLoc, std::move(initPart), std::move(condPart), std::move(thirdPart),
							   rightParenLoc, std::move(body));
}

Statement::StmtPtr Parser::ParseContinueStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_continue));
	const auto loc = m_CurrentToken.GetLocation();
	ConsumeToken();
	if (m_CurrentToken.Is(TokenType::Semi))
	{
		ConsumeToken();
	}
	else
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::Semi)
			  .AddArgument(m_CurrentToken.GetType());
	}
	return m_Sema.ActOnContinueStmt(loc, m_Sema.GetCurrentScope());
}

Statement::StmtPtr Parser::ParseBreakStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_break));
	const auto loc = m_CurrentToken.GetLocation();
	ConsumeToken();
	if (m_CurrentToken.Is(TokenType::Semi))
	{
		ConsumeToken();
	}
	else
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::Semi)
			  .AddArgument(m_CurrentToken.GetType());
	}
	return m_Sema.ActOnBreakStmt(loc, m_Sema.GetCurrentScope());
}

// return-statement:
//	'return' [expression] ;
Statement::StmtPtr Parser::ParseReturnStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_return));
	const auto loc = m_CurrentToken.GetLocation();
	ConsumeToken();

	Expression::ExprPtr returnedExpr;

	if (const auto funcDecl = m_Sema.GetParsingFunction())
	{
		const auto funcType = funcDecl->GetValueType().Cast<Type::FunctionType>();
		assert(funcType);
		const auto retType = funcType->GetResultType();
		if ((!retType && !m_CurrentToken.Is(TokenType::Semi)) || !retType->IsVoid())
		{
			returnedExpr = ParseExpression();
			if (m_CurrentToken.Is(TokenType::Semi))
			{
				ConsumeToken();
			}
			else
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					  .AddArgument(TokenType::Semi)
					  .AddArgument(m_CurrentToken.GetType());
			}
		}

		return m_Sema.ActOnReturnStmt(loc, std::move(returnedExpr), m_Sema.GetCurrentScope());
	}

	// TODO: 报告错误：仅能在函数中返回
	return nullptr;
}

Statement::StmtPtr Parser::ParseExprStatement(nBool mayBeExpr)
{
	auto expr = ParseExpression();

	if (m_CurrentToken.Is(TokenType::Semi))
	{
		ConsumeToken();
		return m_Sema.ActOnExprStmt(std::move(expr));
	}

	if (!mayBeExpr)
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Semi)
			.AddArgument(m_CurrentToken.GetType());
	}

	return expr;
}

Expression::ExprPtr Parser::ParseExpression()
{
	return ParseRightOperandOfBinaryExpression(ParseAssignmentExpression());
}

Expression::ExprPtr Parser::ParseIdExpr()
{
	auto qualifiedId = ParseMayBeQualifiedId();

	if (m_CurrentToken.Is(TokenType::CodeCompletion))
	{
		// TODO: 修改 Context
		m_Sema.ActOnCodeComplete(m_Sema.GetCurrentScope(), m_CurrentToken.GetLocation(), qualifiedId.first, qualifiedId.second.first, Declaration::Context::Block);
		ConsumeToken();
		return nullptr;
	}

	// 注意可能是成员访问操作符，这里可能误判
	if (!qualifiedId.second.first)
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
		      .AddArgument(TokenType::Identifier)
		      .AddArgument(m_CurrentToken.GetType());
		return ParseExprError();
	}
	return m_Sema.ActOnIdExpr(m_Sema.GetCurrentScope(), qualifiedId.first, std::move(qualifiedId.second.first),
		qualifiedId.second.second, m_CurrentToken.Is(TokenType::LeftParen), m_ResolveContext);
}

Expression::ExprPtr Parser::ParseUnaryExpression()
{
	Expression::ExprPtr result;
	const auto tokenType = m_CurrentToken.GetType();

	switch (tokenType)
	{
	case TokenType::LeftParen:
		result = ParseParenExpression();
		break;
	case TokenType::NumericLiteral:
		result = m_Sema.ActOnNumericLiteral(m_CurrentToken);
		ConsumeToken();
		break;
	case TokenType::CharLiteral:
		result = m_Sema.ActOnCharLiteral(m_CurrentToken);
		ConsumeToken();
		break;
	case TokenType::StringLiteral:
		result = m_Sema.ActOnStringLiteral(m_CurrentToken);
		ConsumeToken();
		break;
	case TokenType::Kw_true:
	case TokenType::Kw_false:
		result = m_Sema.ActOnBooleanLiteral(m_CurrentToken);
		ConsumeToken();
		break;
	case TokenType::Kw_null:
		result = m_Sema.ActOnNullPointerLiteral(m_CurrentToken.GetLocation());
		ConsumeToken();
		break;
	case TokenType::Identifier:
	{
		result = ParseIdExpr();
		break;
	}
	case TokenType::PlusPlus:
	case TokenType::MinusMinus:
	case TokenType::Star:
	case TokenType::Amp:
	case TokenType::Plus:
	case TokenType::Minus:
	case TokenType::Exclaim:
	case TokenType::Tilde:
	{
		const auto loc = m_CurrentToken.GetLocation();
		ConsumeToken();
		result = ParseUnaryExpression();
		if (!result)
		{
			// TODO: 报告错误
			result = ParseExprError();
			break;
		}

		result = m_Sema.ActOnUnaryOp(m_Sema.GetCurrentScope(), loc, tokenType, std::move(result));
		break;
	}
	case TokenType::Kw_this:
		return m_Sema.ActOnThis(m_CurrentToken.GetLocation());
	case TokenType::Dollar:
		// TODO: 分清 Context
		ParseCompilerAction(Declaration::Context::Block, [&result](natRefPointer<ASTNode> const& node)
		{
			if (result)
			{
				// 多个表达式是不允许的
				return true;
			}

			result = node;
			return false;
		});
		break;
	case TokenType::Eof:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF, m_CurrentToken.GetLocation());
		return ParseExprError();
	default:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
			  .AddArgument(tokenType);
		return ParseExprError();
	}

	return ParsePostfixExpressionSuffix(std::move(result));
}

Expression::ExprPtr Parser::ParseRightOperandOfBinaryExpression(Expression::ExprPtr leftOperand,
																OperatorPrecedence minPrec)
{
	auto tokenPrec = GetOperatorPrecedence(m_CurrentToken.GetType());
	SourceLocation colonLoc;

	while (true)
	{
		if (tokenPrec < minPrec)
		{
			return leftOperand;
		}

		auto opToken = m_CurrentToken;
		ConsumeToken();

		Expression::ExprPtr ternaryMiddle;
		if (tokenPrec == OperatorPrecedence::Conditional)
		{
			ternaryMiddle = ParseAssignmentExpression();
			if (!ternaryMiddle)
			{
				// TODO: 报告错误
				leftOperand = nullptr;
			}

			if (!m_CurrentToken.Is(TokenType::Colon))
			{
				// TODO: 报告可能缺失的':'记号
			}

			colonLoc = m_CurrentToken.GetLocation();
			ConsumeToken();
		}

		auto rightOperand = tokenPrec <= OperatorPrecedence::Conditional
								? ParseAssignmentExpression()
								: ParseUnaryExpression();
		if (!rightOperand)
		{
			// TODO: 报告错误
			leftOperand = nullptr;
		}

		auto prevPrec = tokenPrec;
		tokenPrec = GetOperatorPrecedence(m_CurrentToken.GetType());

		const auto isRightAssoc = prevPrec == OperatorPrecedence::Assignment || prevPrec == OperatorPrecedence::Conditional;

		if (prevPrec < tokenPrec || (prevPrec == tokenPrec && isRightAssoc))
		{
			rightOperand = ParseRightOperandOfBinaryExpression(std::move(rightOperand),
															   static_cast<OperatorPrecedence>(static_cast<std::underlying_type_t<OperatorPrecedence>>(prevPrec) + !isRightAssoc));
			if (!rightOperand)
			{
				// TODO: 报告错误
				leftOperand = nullptr;
			}

			tokenPrec = GetOperatorPrecedence(m_CurrentToken.GetType());
		}

		// TODO: 如果之前的分析发现出错的话条件将不会被满足，由于之前已经报告了错误在此可以不进行报告，但必须继续执行以保证分析完整个表达式
		if (leftOperand)
		{
			leftOperand = ternaryMiddle
							  ? m_Sema.ActOnConditionalOp(opToken.GetLocation(), colonLoc, std::move(leftOperand),
														  std::move(ternaryMiddle), std::move(rightOperand))
							  : m_Sema.ActOnBinaryOp(m_Sema.GetCurrentScope(), opToken.GetLocation(), opToken.GetType(),
													 std::move(leftOperand), std::move(rightOperand));
		}
	}
}

Expression::ExprPtr Parser::ParsePostfixExpressionSuffix(Expression::ExprPtr prefix)
{
	while (true)
	{
		switch (m_CurrentToken.GetType())
		{
		case TokenType::LeftSquare:
		{
			const auto lloc = m_CurrentToken.GetLocation();
			ConsumeBracket();
			auto index = ParseExpression();

			if (!m_CurrentToken.Is(TokenType::RightSquare))
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					  .AddArgument(TokenType::RightSquare)
					  .AddArgument(m_CurrentToken.GetType());

				prefix = ParseExprError();
				break;
			}

			const auto rloc = m_CurrentToken.GetLocation();
			prefix = m_Sema.ActOnArraySubscriptExpr(m_Sema.GetCurrentScope(), std::move(prefix), lloc, std::move(index), rloc);
			ConsumeBracket();

			break;
		}
		case TokenType::LeftParen:
		{
			std::vector<Expression::ExprPtr> argExprs;
			std::vector<SourceLocation> commaLocs;

			const auto lloc = m_CurrentToken.GetLocation();
			ConsumeParen();

			if (!m_CurrentToken.Is(TokenType::RightParen) && !ParseExpressionList(argExprs, commaLocs, TokenType::RightParen))
			{
				// TODO: 报告错误
				prefix = ParseExprError();
				break;
			}

			if (!m_CurrentToken.Is(TokenType::RightParen))
			{
				// TODO: 报告错误
				prefix = ParseExprError();
				break;
			}

			ConsumeParen();

			prefix = m_Sema.ActOnCallExpr(m_Sema.GetCurrentScope(), std::move(prefix), lloc, from(argExprs),
										  m_CurrentToken.GetLocation());

			break;
		}
		case TokenType::Period:
		{
			const auto periodLoc = m_CurrentToken.GetLocation();
			ConsumeToken();
			if (!m_CurrentToken.Is(TokenType::Identifier))
			{
				prefix = ParseExprError();
				break;
			}
			prefix = m_Sema.ActOnMemberAccessExpr(m_Sema.GetCurrentScope(), std::move(prefix), periodLoc, nullptr,
				m_CurrentToken.GetIdentifierInfo());
			ConsumeToken();

			break;
		}
		case TokenType::PlusPlus:
		case TokenType::MinusMinus:
			prefix = m_Sema.ActOnPostfixUnaryOp(m_Sema.GetCurrentScope(), m_CurrentToken.GetLocation(), m_CurrentToken.GetType(),
												std::move(prefix));
			ConsumeToken();
			break;
		case TokenType::Kw_as:
		{
			const auto asLoc = m_CurrentToken.GetLocation();

			// 吃掉 as
			ConsumeToken();

			const auto decl = make_ref<Declaration::Declarator>(Declaration::Context::TypeName);
			if (!ParseDeclarator(decl) || !decl->IsValid())
			{
				// TODO: 报告错误
				prefix = ParseExprError();
				break;
			}

			auto type = decl->GetType();
			if (!type)
			{
				// TODO: 报告错误
				prefix = ParseExprError();
				break;
			}

			prefix = m_Sema.ActOnAsTypeExpr(m_Sema.GetCurrentScope(), std::move(prefix), std::move(type), asLoc);

			break;
		}
		default:
			return prefix;
		}
	}
}

Expression::ExprPtr Parser::ParseConstantExpression()
{
	return ParseRightOperandOfBinaryExpression(ParseUnaryExpression(), OperatorPrecedence::Conditional);
}

Expression::ExprPtr Parser::ParseAssignmentExpression()
{
	if (m_CurrentToken.Is(TokenType::Kw_throw))
	{
		return ParseThrowExpression();
	}

	return ParseRightOperandOfBinaryExpression(ParseUnaryExpression());
}

Expression::ExprPtr Parser::ParseThrowExpression()
{
	assert(m_CurrentToken.Is(TokenType::Kw_throw));
	const auto throwLocation = m_CurrentToken.GetLocation();
	ConsumeToken();

	switch (m_CurrentToken.GetType())
	{
	case TokenType::Semi:
	case TokenType::RightParen:
	case TokenType::RightSquare:
	case TokenType::RightBrace:
	case TokenType::Colon:
	case TokenType::Comma:
		return m_Sema.ActOnThrow(m_Sema.GetCurrentScope(), throwLocation, {});
	case TokenType::Eof:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF, m_CurrentToken.GetLocation());
		return ParseExprError();
	default:
		auto expr = ParseAssignmentExpression();
		if (!expr)
		{
			return ParseExprError();
		}
		return m_Sema.ActOnThrow(m_Sema.GetCurrentScope(), throwLocation, std::move(expr));
	}
}

Expression::ExprPtr Parser::ParseParenExpression()
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));

	ConsumeParen();
	auto ret = ParseExpression();
	if (m_CurrentToken.Is(TokenType::RightParen))
	{
		ConsumeParen();
	}
	else
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::RightParen)
			.AddArgument(m_CurrentToken.GetType());
	}

	return ret;
}

// TODO: 可能找到多个声明。。。先假设只能找到一个否则报错
// 会至少吃掉一个 Token，会吃掉最后一个 Identifier Token，若最后一个不是 Identifier 则不会吃掉
std::pair<natRefPointer<NestedNameSpecifier>, std::pair<Identifier::IdPtr, SourceLocation>> Parser::ParseMayBeQualifiedId()
{
	assert(m_CurrentToken.Is(TokenType::Identifier));

	natRefPointer<NestedNameSpecifier> nns;
	while (m_CurrentToken.Is(TokenType::Identifier))
	{
		auto id = m_CurrentToken.GetIdentifierInfo();
		ConsumeToken();

		if (!m_CurrentToken.Is(TokenType::Period))
		{
			return { std::move(nns), { std::move(id), m_CurrentToken.GetLocation() } };
		}

		Semantic::LookupResult r{ m_Sema, id, m_CurrentToken.GetLocation(), Semantic::Sema::LookupNameType::LookupAnyName };
		// TODO: 处理其他情况
		if (!m_Sema.LookupNestedName(r, m_Sema.GetCurrentScope(), nns) || r.GetResultType() != Semantic::LookupResult::LookupResultType::Found || r.GetDeclSize() != 1)
		{
			// 分析失败，返回已经分析的部分
			return { std::move(nns), { std::move(id), m_CurrentToken.GetLocation() } };
		}

		auto decl = r.GetDecls().first();
		assert(decl);

		if (!Declaration::Decl::CastToDeclContext(decl.Get()))
		{
			// 分析失败，返回已经分析的部分
			return { std::move(nns), { std::move(id), m_CurrentToken.GetLocation() } };
		}

		nns = NestedNameSpecifier::Create(m_Sema.GetASTContext(), std::move(nns), std::move(decl));  // NOLINT

		// 延迟吃掉这个点
		ConsumeToken();
	}

	return { std::move(nns), { nullptr, {} } };  // NOLINT
}

nBool Parser::ParseExpressionList(std::vector<Expression::ExprPtr>& exprs, std::vector<SourceLocation>& commaLocs,
								  Lex::TokenType endToken)
{
	while (true)
	{
		auto expr = ParseAssignmentExpression();
		if (!expr)
		{
			SkipUntil({ endToken }, true);
			return false;
		}
		exprs.emplace_back(std::move(expr));
		if (!m_CurrentToken.Is(TokenType::Comma))
		{
			break;
		}
		commaLocs.emplace_back(m_CurrentToken.GetLocation());
		ConsumeToken();
	}

	if (endToken != TokenType::Eof && !m_CurrentToken.Is(endToken))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			  .AddArgument(endToken)
			  .AddArgument(m_CurrentToken.GetType());
		return false;
	}

	return true;
}

// declarator:
//	[identifier] [< template-parameter >] [: type] [initializer]
nBool Parser::ParseDeclarator(Declaration::DeclaratorPtr const& decl, nBool skipIdentifier)
{
	const auto context = decl->GetContext();
	if (!skipIdentifier && context != Declaration::Context::TypeName)
	{
		if (m_CurrentToken.Is(TokenType::Identifier))
		{
			auto id = m_CurrentToken.GetIdentifierInfo();
			const auto idLoc = m_CurrentToken.GetLocation();
			if (decl->GetContext() == Declaration::Context::Prototype)
			{
				// 函数原型上下文中允许没有标识符只有类型的参数声明，因此先尝试匹配类型，失败则会回滚
				// TODO: 禁止函数指针中的函数类型的参数带名称，这样可以无歧义地分析
				const auto memento = m_Preprocessor.SaveToMemento();

				const auto typeDecl = make_ref<Declaration::Declarator>(Declaration::Context::TypeName);
				const auto scope = make_scope([this, isEnabled = m_Diag.IsDiagEnabled()]
				{
					m_Diag.EnableDiag(isEnabled);
				});
				m_Diag.EnableDiag(false);

				if (ParseType(typeDecl) && typeDecl->GetType())
				{
					decl->SetType(typeDecl->GetType());
					return true;
				}
				m_Preprocessor.RestoreFromMemento(memento);
			}
			decl->SetIdentifier(std::move(id));
			decl->SetIdentifierLocation(idLoc);
			ConsumeToken();
		}
		else if (m_CurrentToken.IsAnyOf({ TokenType::Tilde, TokenType::Kw_this }))
		{
			if (m_CurrentToken.Is(TokenType::Tilde))
			{
				ConsumeToken();
				if (!m_CurrentToken.Is(TokenType::Kw_this))
				{
					m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
						  .AddArgument(TokenType::Kw_this)
						  .AddArgument(m_CurrentToken.GetType());
					return false;
				}

				decl->SetDestructor();
			}
			else
			{
				decl->SetConstructor();
			}

			ConsumeToken();
		}
		else if (context != Declaration::Context::Prototype && context != Declaration::Context::TypeName)
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				.AddArgument(TokenType::Identifier)
				.AddArgument(m_CurrentToken.GetType());
			return false;
		}
	}

	// TODO: 验证其他上下文下是否需要延迟分析
	if (m_Sema.GetCurrentPhase() == Semantic::Sema::Phase::Phase1 && context != Declaration::Context::Prototype && context
		!= Declaration::Context::TypeName)
	{
		skipTypeAndInitializer(decl);
	}
	else
	{
		// 对于有 unsafe 说明符的声明符，允许在类型和初始化器中使用不安全的功能
		const auto curScope = m_Sema.GetCurrentScope();
		const auto flags = curScope->GetFlags();
		if (decl->GetSafety() == Specifier::Safety::Unsafe)
		{
			curScope->AddFlags(Semantic::ScopeFlags::UnsafeScope);
		}

		const auto scope = make_scope([curScope, flags]
		{
			curScope->SetFlags(flags);
		});

		// (: int)也可以？
		if (m_CurrentToken.Is(TokenType::Colon) || ((context == Declaration::Context::Prototype || context == Declaration::
			Context::TypeName) && !decl->GetIdentifier()))
		{
			if (!ParseType(decl))
			{
				return false;
			}
		}

		// 声明函数原型时也可以指定initializer？
		if (context != Declaration::Context::TypeName &&
			decl->GetStorageClass() != Specifier::StorageClass::Extern &&
			m_CurrentToken.IsAnyOf({ TokenType::Equal, TokenType::LeftBrace }))
		{
			if (!ParseInitializer(decl))
			{
				return false;
			}
		}
	}

	return true;
}

// specifier-seq:
//	specifier
//	specifier-seq specifier
// specifier:
//	storage-class-specifier
//	access-specifier
nBool Parser::ParseSpecifier(Declaration::DeclaratorPtr const& decl)
{
	while (true)
	{
		switch (m_CurrentToken.GetType())
		{
		case TokenType::Kw_extern:
			if (decl->GetStorageClass() != Specifier::StorageClass::None)
			{
				// TODO: 报告错误：多个存储类说明符
				return false;
			}
			decl->SetStorageClass(Specifier::StorageClass::Extern);
			break;
		case TokenType::Kw_static:
			if (decl->GetStorageClass() != Specifier::StorageClass::None)
			{
				// TODO: 报告错误：多个存储类说明符
				return false;
			}
			decl->SetStorageClass(Specifier::StorageClass::Static);
			break;
		case TokenType::Kw_const:
			if (decl->GetStorageClass() != Specifier::StorageClass::None)
			{
				// TODO: 报告错误：多个存储类说明符
				return false;
			}
			decl->SetStorageClass(Specifier::StorageClass::Const);
			break;
		case TokenType::Kw_public:
			if (decl->GetAccessibility() != Specifier::Access::None)
			{
				// TODO: 报告错误：多个访问说明符
				return false;
			}
			decl->SetAccessibility(Specifier::Access::Public);
			break;
		case TokenType::Kw_protected:
			if (decl->GetAccessibility() != Specifier::Access::None)
			{
				// TODO: 报告错误：多个访问说明符
				return false;
			}
			decl->SetAccessibility(Specifier::Access::Protected);
			break;
		case TokenType::Kw_internal:
			if (decl->GetAccessibility() != Specifier::Access::None)
			{
				// TODO: 报告错误：多个访问说明符
				return false;
			}
			decl->SetAccessibility(Specifier::Access::Internal);
			break;
		case TokenType::Kw_private:
			if (decl->GetAccessibility() != Specifier::Access::None)
			{
				// TODO: 报告错误：多个访问说明符
				return false;
			}
			decl->SetAccessibility(Specifier::Access::Private);
			break;
		case TokenType::Kw_unsafe:
			if (decl->GetSafety() != Specifier::Safety::None)
			{
				// TODO: 报告错误：多个安全说明符
				return false;
			}
			decl->SetSafety(Specifier::Safety::Unsafe);
			break;
		default:
			// 不是错误
			return true;
		}

		ConsumeToken();
	}
}

// template-parameter-list:
//	template-parameter
//	template-parameter-list , template-parameter
// template-parameter:
//	type-parameter
//	non-type-parameter
// type-parameter:
//	identifier [type-initializer]
// type-initializer:
//	= type
// non-type-parameter:
//	identifier : type [initializer]
nBool Parser::ParseTemplateParameterList(Declaration::DeclaratorPtr const& decl)
{
	assert(m_CurrentToken.Is(TokenType::Less));
	ConsumeToken();
	nat_Throw(NotImplementedException);
}

// type-specifier:
//	[auto]
//	type-identifier
nBool Parser::ParseType(Declaration::DeclaratorPtr const& decl)
{
	const auto context = decl->GetContext();

	if (!m_CurrentToken.Is(TokenType::Colon))
	{
		if (context != Declaration::Context::Prototype && context != Declaration::Context::TypeName)
		{
			// 在非声明函数原型的上下文中不显式写出类型，视为隐含auto
			// auto的声明符在指定initializer之后决定实际类型
			return true;
		}
	}
	else
	{
		ConsumeToken();
	}

	const auto tokenType = m_CurrentToken.GetType();
	switch (tokenType)
	{
	case TokenType::Identifier:
	{
		// 用户自定义类型

		const auto qualifiedId = ParseMayBeQualifiedId();

		if (m_CurrentToken.Is(TokenType::CodeCompletion))
		{
			m_Sema.ActOnCodeComplete(m_Sema.GetCurrentScope(), m_CurrentToken.GetLocation(), qualifiedId.first, qualifiedId.second.first, context);
			ConsumeToken();
			return false;
		}

		if (!qualifiedId.second.first)
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedTypeSpecifierGot, m_CurrentToken.GetLocation())
				.AddArgument(m_CurrentToken.GetType());
			return false;
		}

		if (auto type = m_Sema.LookupTypeName(qualifiedId.second.first, m_CurrentToken.GetLocation(),
								m_Sema.GetCurrentScope(), qualifiedId.first))
		{
			decl->SetType(std::move(type));
		}
		else if (const auto alias = m_Sema.LookupAliasName(qualifiedId.second.first, m_CurrentToken.GetLocation(), m_Sema.GetCurrentScope(), qualifiedId.first, m_ResolveContext))
		{
			const auto aliasAsAst = alias->GetAliasAsAst();
			if (auto aliasType = aliasAsAst.Cast<Type::Type>())
			{
				decl->SetType(std::move(aliasType));
			}
			else if (const auto compilerAction = aliasAsAst.Cast<ICompilerAction>())
			{
				ConsumeToken();
				const auto actionContext = compilerAction->StartAction(CompilerActionContext{ *this });
				ParseCompilerActionArguments(context, actionContext);
				compilerAction->EndAction(actionContext, [&decl](ASTNodePtr node)
				{
					if (decl->GetType())
					{
						// 多个类型是不允许的
						return true;
					}

					if (auto retType = node.Cast<Type::Type>())
					{
						decl->SetType(node);
					}
					else
					{
						// TODO: 报告错误
						return true;
					}

					return false;
				});
			}
			else
			{
				// TODO: 报告错误
				return false;
			}
		}
		else
		{
			return false;
		}

		break;
	}
	case TokenType::LeftParen:
	{
		// 函数类型或者括号类型
		ParseFunctionType(decl);
		break;
	}
	case TokenType::RightParen:
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
			  .AddArgument(TokenType::RightParen);
		ConsumeParen();

		return false;
	}
	case TokenType::Eof:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF, m_CurrentToken.GetLocation());
		break;
	case TokenType::Dollar:
		// TODO: 分清 Context
		ParseCompilerAction(Declaration::Context::TypeName, [&decl](natRefPointer<ASTNode> const& node)
		{
			if (decl->GetType())
			{
				// 多个类型是不允许的
				return true;
			}

			if (auto retType = node.Cast<Type::Type>())
			{
				decl->SetType(node);
			}
			else
			{
				// TODO: 报告错误
			}

			return false;
		});
		break;
	default:
	{
		const auto builtinClass = Type::BuiltinType::GetBuiltinClassFromTokenType(tokenType);
		if (builtinClass == Type::BuiltinType::Invalid)
		{
			// 对于无效类型的处理
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedTypeSpecifierGot, m_CurrentToken.GetLocation())
				  .AddArgument(tokenType);
			return false;
		}
		decl->SetType(m_Sema.GetASTContext().GetBuiltinType(builtinClass));
		ConsumeToken();
		break;
	}
	}

	// 数组的指针和指针的数组和指针的数组的指针
	ParseArrayOrPointerType(decl);
	return true;
}

void Parser::ParseParenType(Declaration::DeclaratorPtr const& decl)
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));
	// 吃掉左括号
	ConsumeParen();

	ParseType(decl);
	if (!decl->GetType())
	{
		if (m_CurrentToken.Is(TokenType::Identifier))
		{
			// 函数类型
			ParseFunctionType(decl);
			return;
		}

		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier, m_CurrentToken.GetLocation());
	}
}

void Parser::ParseFunctionType(Declaration::DeclaratorPtr const& decl)
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));
	ConsumeParen();

	std::vector<Declaration::DeclaratorPtr> paramDecls;
	auto hasVarArg = false;

	auto mayBeParenType = true;

	ParseScope prototypeScope{
		this,
		Semantic::ScopeFlags::FunctionDeclarationScope | Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::
		FunctionPrototypeScope
	};

	if (!m_CurrentToken.Is(TokenType::RightParen))
	{
		if (decl->IsDestructor())
		{
			// TODO: 报告错误：析构函数不可以具有参数
			// ↑那为什么要写这个括号呢？【x
			return;
		}

		while (true)
		{
			if (m_CurrentToken.Is(TokenType::Ellipsis))
			{
				if (decl->GetSafety() != Specifier::Safety::Unsafe && !m_Sema.GetCurrentScope()->HasFlags(Semantic::ScopeFlags::UnsafeScope))
				{
					m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnsafeOperationInSafeScope, m_CurrentToken.GetLocation());
				}

				hasVarArg = true;
				mayBeParenType = false;
				ConsumeToken();

				if (m_CurrentToken.Is(TokenType::RightParen))
				{
					ConsumeParen();
				}
				else
				{
					// TODO: 报告错误：可变参数只能位于最后一个参数
					m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
						.AddArgument(TokenType::RightParen)
						.AddArgument(m_CurrentToken.GetType());
				}

				break;
			}

			auto param = make_ref<Declaration::Declarator>(Declaration::Context::Prototype);
			ParseDeclarator(param);
			if ((mayBeParenType && param->GetIdentifier()) || !param->GetType())
			{
				mayBeParenType = false;
			}

			if (param->IsValid())
			{
				paramDecls.emplace_back(param);
			}
			else
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedDeclarator, m_CurrentToken.GetLocation());
			}

			if (!param->GetType() && !param->GetInitializer())
			{
				// TODO: 报告错误：参数的类型和初始化器至少要存在一个
			}

			if (m_CurrentToken.Is(TokenType::RightParen))
			{
				ConsumeParen();
				break;
			}

			mayBeParenType = false;

			if (m_CurrentToken.Is(TokenType::Comma))
			{
				ConsumeToken();
			}
			else
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					.AddArgument(TokenType::Comma)
					.AddArgument(m_CurrentToken.GetType());
			}
		}
	}
	else
	{
		mayBeParenType = false;
		ConsumeParen();
	}

	// 读取完函数参数信息，开始读取返回类型

	Type::TypePtr retType;
	// 构造和析构函数没有返回类型
	if (decl->IsConstructor() || decl->IsDestructor())
	{
		retType = m_Sema.GetASTContext().GetBuiltinType(Type::BuiltinType::Void);
	}
	else
	{
		// 如果不是->且只有一个无名称参数，并且不是构造或者析构函数说明是普通的括号类型
		if (!m_CurrentToken.Is(TokenType::Arrow))
		{
			if (mayBeParenType)
			{
				// 是括号类型，但是我们已经把Token处理完毕了。。。
				decl->SetType(paramDecls.front()->GetType());
				return;
			}

			// 以后会加入元组或匿名类型的支持吗？
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				  .AddArgument(TokenType::Arrow)
				  .AddArgument(m_CurrentToken.GetType());
		}

		ConsumeToken();

		const auto retTypeDeclarator = make_ref<Declaration::Declarator>(Declaration::Context::Prototype);
		ParseType(retTypeDeclarator);

		retType = retTypeDeclarator->GetType();
	}

	decl->SetType(m_Sema.BuildFunctionType(retType, from(paramDecls)
										   .select([](Declaration::DeclaratorPtr const& paramDecl)
										   {
											   return paramDecl->GetType();
										   }), hasVarArg));

	decl->SetParams(from(paramDecls)
		.select([this](Declaration::DeclaratorPtr const& paramDecl)
		{
			return m_Sema.ActOnParamDeclarator(m_Sema.GetCurrentScope(), paramDecl);
		}));
}

// TODO: def arr: int[1, 2, 3];
// arr[0, 1, 2] = 1;
// 这样的语法比较好看，列入计划
void Parser::ParseArrayOrPointerType(Declaration::DeclaratorPtr const& decl)
{
	auto lastIsUnknownSizedArray = false;

	while (true)
	{
		switch (m_CurrentToken.GetType())
		{
		case TokenType::LeftSquare:
		{
			if (lastIsUnknownSizedArray)
			{
				// TODO: 报告错误：未知大小数组类型仅能出现在顶层
			}

			ConsumeBracket();

			if (m_CurrentToken.Is(TokenType::RightSquare))
			{
				ConsumeBracket();
				lastIsUnknownSizedArray = true;
				decl->SetType(m_Sema.ActOnArrayType(decl->GetType(), 0));
				continue;
			}

			const auto sizeExpr = ParseConstantExpression();
			nuLong result;
			if (!sizeExpr->EvaluateAsInt(result, m_Sema.GetASTContext()))
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpressionCannotEvaluateAsConstant, m_CurrentToken.GetLocation());
			}

			if (!result)
			{
				// TODO: 报告错误：禁止 0 大小数组
			}

			decl->SetType(m_Sema.ActOnArrayType(decl->GetType(), result));

			if (m_CurrentToken.Is(TokenType::RightSquare))
			{
				ConsumeBracket();
			}
			else
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					.AddArgument(TokenType::RightSquare)
					.AddArgument(m_CurrentToken.GetType());
			}

			break;
		}
		case TokenType::Star:
			if (lastIsUnknownSizedArray)
			{
				// TODO: 报告错误：未知大小数组类型仅能出现在顶层
			}

			ConsumeToken();
			decl->SetType(m_Sema.ActOnPointerType(m_Sema.GetCurrentScope(), decl->GetType()));
			break;
		default:
			return;
		}
	}
}

// initializer:
//	= expression
//	= { expression-list }
//	compound-statement
nBool Parser::ParseInitializer(Declaration::DeclaratorPtr const& decl)
{
	if (m_CurrentToken.Is(TokenType::Equal))
	{
		ConsumeToken();
		if (m_CurrentToken.Is(TokenType::LeftBrace))
		{
			const auto leftBraceLoc = m_CurrentToken.GetLocation();
			ConsumeBrace();

			if (!decl->GetType())
			{
				// TODO: 报告错误：此形式要求指定类型
				return false;
			}

			std::vector<Expression::ExprPtr> argExprs;
			std::vector<SourceLocation> commaLocs;

			if (!ParseExpressionList(argExprs, commaLocs, TokenType::RightBrace))
			{
				// 应该已经报告了错误，仅返回即可
				return false;
			}

			if (const auto arrayType = decl->GetType().Cast<Type::ArrayType>(); arrayType && !arrayType->GetSize())
			{
				// 不要直接 SetSize，因为每个类型都会在 ASTContext 缓存
				decl->SetType(m_Sema.ActOnArrayType(arrayType->GetElementType(), argExprs.size()));
			}

			// 如果到达此处说明当前 Token 是右大括号
			const auto rightBraceLoc = m_CurrentToken.GetLocation();
			ConsumeBrace();

			decl->SetInitializer(m_Sema.ActOnInitExpr(decl->GetType(), leftBraceLoc, std::move(argExprs), rightBraceLoc));
		}
		else
		{
			decl->SetInitializer(ParseExpression());
		}

		if (m_CurrentToken.Is(TokenType::Semi))
		{
			ConsumeToken();
		}
		else
		{
			if (decl->GetContext() != Declaration::Context::Prototype)
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					.AddArgument(TokenType::Semi)
					.AddArgument(m_CurrentToken.GetType());
				return false;
			}
		}

		return true;
	}

	if (m_CurrentToken.Is(TokenType::LeftBrace))
	{
		if (decl->GetType()->GetType() != Type::Type::Function)
		{
			// TODO: 报告错误
			return false;
		}

		ParseScope bodyScope{
			this,
			Semantic::ScopeFlags::FunctionScope | Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::CompoundStmtScope
		};
		auto funcDecl = m_Sema.ActOnStartOfFunctionDef(m_Sema.GetCurrentScope(), decl);
		decl->SetDecl(ParseFunctionBody(std::move(funcDecl), bodyScope));
	}

	// 不是 initializer，返回
	return true;
}

nBool Parser::SkipUntil(std::initializer_list<Lex::TokenType> list, nBool dontConsume,
						std::vector<Token>* skippedTokens)
{
	// 特例，如果调用者只是想跳到文件结尾，我们不需要再另外判断其他信息
	if (list.size() == 1 && *list.begin() == TokenType::Eof)
	{
		while (!m_CurrentToken.Is(TokenType::Eof))
		{
			skipToken(skippedTokens);
		}
		return true;
	}

	while (true)
	{
		const auto currentType = m_CurrentToken.GetType();
		for (auto type : list)
		{
			if (currentType == type)
			{
				if (!dontConsume)
				{
					skipToken(skippedTokens);
				}

				return true;
			}
		}

		switch (currentType)
		{
		case TokenType::Eof:
			return false;
		case TokenType::LeftParen:
			skipToken(skippedTokens);
			SkipUntil({ TokenType::RightParen }, false, skippedTokens);
			break;
		case TokenType::LeftSquare:
			skipToken(skippedTokens);
			SkipUntil({ TokenType::RightSquare }, false, skippedTokens);
			break;
		case TokenType::LeftBrace:
			skipToken(skippedTokens);
			SkipUntil({ TokenType::RightBrace }, false, skippedTokens);
			break;
		case TokenType::RightParen: // 可能的不匹配括号，下同
			skipToken(skippedTokens);
			break;
		case TokenType::RightSquare:
			skipToken(skippedTokens);
			break;
		case TokenType::RightBrace:
			skipToken(skippedTokens);
			break;
		default:
			skipToken(skippedTokens);
			break;
		}
	}
}

void Parser::pushCachedTokens(std::vector<Token> tokens)
{
	m_Preprocessor.PushCachedTokens(move(tokens));
	ConsumeToken();
}

void Parser::popCachedTokens()
{
	m_Preprocessor.PopCachedTokens();
}

void Parser::skipTypeAndInitializer(Declaration::DeclaratorPtr const& decl)
{
	std::vector<Token> cachedTokens;

	// 逗号和右括号是在 CompilerAction 参数中的情况
	SkipUntil({ TokenType::Equal, TokenType::LeftBrace, TokenType::Semi, TokenType::Comma, TokenType::RightParen }, true, &cachedTokens);

	switch (m_CurrentToken.GetType())
	{
	case TokenType::Equal:
		skipToken(&cachedTokens);
		SkipUntil({ TokenType::Semi }, false, &cachedTokens);
		break;
	case TokenType::LeftBrace:
		skipToken(&cachedTokens);
		SkipUntil({ TokenType::RightBrace }, false, &cachedTokens);
		break;
	case TokenType::Semi:
		skipToken(&cachedTokens);
		[[fallthrough]];
	case TokenType::Comma:
	case TokenType::RightParen:
		break;
	default:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpect, m_CurrentToken.GetLocation())
			  .AddArgument(m_CurrentToken.GetType());
		break;
	}

	decl->SetCachedTokens(move(cachedTokens));
}

Declaration::DeclPtr Parser::ResolveDeclarator(const Declaration::DeclaratorPtr& decl)
{
	assert(m_Sema.GetCurrentPhase() == Semantic::Sema::Phase::Phase2 && m_ResolveContext);
	assert(!decl->GetType() && !decl->GetInitializer());

	const auto oldUnresolvedDecl = decl->GetDecl();
	assert(oldUnresolvedDecl);

	auto curToken = m_CurrentToken;
	pushCachedTokens(decl->GetAndClearCachedTokens());
	const auto tokensScope = make_scope([this, curToken = std::move(curToken)]
	{
		popCachedTokens();
		m_CurrentToken = curToken;
	});

	m_ResolveContext->StartResolvingDeclarator(decl);
	const auto resolveContextScope = make_scope([this, decl]
	{
		m_ResolveContext->EndResolvingDeclarator(decl);
	});

	auto declScope = decl->GetDeclarationScope();
	const auto tempUnsafe = decl->GetSafety() == Specifier::Safety::Unsafe && !declScope->HasFlags(Semantic::ScopeFlags::UnsafeScope);
	const auto recoveryScope = make_scope(
		[this, curScope = m_Sema.GetCurrentScope(), curDeclContext = m_Sema.GetDeclContext(), tempUnsafe]
		{
			if (tempUnsafe)
			{
				m_Sema.GetCurrentScope()->RemoveFlags(Semantic::ScopeFlags::UnsafeScope);
			}
			m_Sema.SetDeclContext(curDeclContext);
			m_Sema.SetCurrentScope(curScope);
		});
	m_Sema.SetCurrentScope(declScope);
	m_Sema.SetDeclContext(decl->GetDeclarationContext());
	if (tempUnsafe)
	{
		declScope->AddFlags(Semantic::ScopeFlags::UnsafeScope);
	}

	if (decl->IsAlias())
	{
		SourceLocation dummy;
		m_Sema.RemoveOldUnresolvedDecl(decl, oldUnresolvedDecl);
		return ParseAliasBody(decl->GetRange().GetBegin(), decl->GetIdentifier(), decl->GetIdentifierLocation(), decl->GetContext(), dummy);
	}

	ParseDeclarator(decl, true);
	auto ret = m_Sema.HandleDeclarator(m_Sema.GetCurrentScope(), decl, oldUnresolvedDecl);

	for (const auto& postProcessor : decl->GetPostProcessors())
	{
		const auto context = postProcessor->StartAction(CompilerActionContext{ *this });
		const auto argumentRequirement = context->GetArgumentRequirement();
		if (!HasAnyFlags(argumentRequirement->GetNextExpectedArgumentType(), CompilerActionArgumentType::Declaration))
		{
			// TODO: 报告错误：附加的后处理器不接受声明参数
			postProcessor->EndAction(context);
			continue;
		}
		context->AddArgument(ret);
		const auto secondArgumentType = argumentRequirement->GetNextExpectedArgumentType();
		if (secondArgumentType != CompilerActionArgumentType::None && !HasAllFlags(secondArgumentType, CompilerActionArgumentType::Optional))
		{
			// TODO: 报告错误：附加的后处理器必须能够只接受一个声明参数
		}
		postProcessor->EndAction(context);
	}

	return ret;
}

IUnknownTokenHandler::~IUnknownTokenHandler()
{
}

Parser::ParseScope::ParseScope(Parser* self, Semantic::ScopeFlags flags)
	: m_Self{ self }
{
	assert(m_Self);

	m_Self->m_Sema.PushScope(flags);
}

Parser::ParseScope::~ParseScope()
{
	ExplicitExit();
}

void Parser::ParseScope::ExplicitExit()
{
	if (m_Self)
	{
		m_Self->m_Sema.PopScope();
		m_Self = nullptr;
	}
}

void NatsuLang::ParseAST(Preprocessor& pp, ASTContext& astContext, natRefPointer<ASTConsumer> astConsumer)
{
	Semantic::Sema sema{ pp, astContext, astConsumer };
	Parser parser{ pp, sema };

	ParseAST(parser);
	EndParsingAST(parser);
}

void NatsuLang::ParseAST(Parser& parser)
{
	auto& sema = parser.GetSema();
	auto const& consumer = sema.GetASTConsumer();

	std::vector<Declaration::DeclPtr> decls;

	while (!parser.ParseTopLevelDecl(decls))
	{
		if (!consumer->HandleTopLevelDecl(from(decls)))
		{
			return;
		}
	}

	consumer->HandleTranslationUnit(sema.GetASTContext());
}

void NatsuLang::EndParsingAST(Parser& parser)
{
	auto& sema = parser.GetSema();
	auto const& consumer = sema.GetASTConsumer();
	std::vector<Declaration::DeclPtr> decls;

	// 进入2阶段，解析所有声明符
	parser.DivertPhase(decls);

	if (!consumer->HandleTopLevelDecl(from(decls)))
	{
		return;
	}

	consumer->HandleTranslationUnit(sema.GetASTContext());
}
