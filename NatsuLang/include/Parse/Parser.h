﻿#pragma once
#include "AST/OperatorPrecedence.h"
#include "Lex/Preprocessor.h"
#include "Basic/Config.h"
#include "Sema/Declarator.h"
#include <unordered_set>
#include "Sema/CompilerAction.h"

namespace NatsuLang
{
	class NestedNameSpecifier;
	struct ICompilerAction;
	struct ASTNode;
}

namespace NatsuLang::Identifier
{
	class IdentifierInfo;
	using IdPtr = NatsuLib::natRefPointer<IdentifierInfo>;
}

namespace NatsuLang::Declaration
{
	class Decl;
	using DeclPtr = NatsuLib::natRefPointer<Decl>;
	class EnumDecl;
}

namespace NatsuLang::Statement
{
	class Stmt;
	using StmtPtr = NatsuLib::natRefPointer<Stmt>;
}

namespace NatsuLang::Expression
{
	class Expr;
	using ExprPtr = NatsuLib::natRefPointer<Expr>;
}

namespace NatsuLang::Diag
{
	class DiagnosticsEngine;
}

namespace NatsuLang::Semantic
{
	enum class ScopeFlags : nuShort;
	class Sema;
}

namespace NatsuLang::Type
{
	class Type;
	using TypePtr = NatsuLib::natRefPointer<Type>;
}

namespace NatsuLang::Syntax
{
	DeclareException(ParserException, NatsuLib::natException, "Exception generated by parser.");

	class Parser;

	// 用于检测环形依赖
	class ResolveContext
		: public NatsuLib::natRefObjImpl<ResolveContext>
	{
	public:
		enum class ResolvingState
		{
			Unknown,
			Resolving,
			Resolved
		};

		explicit ResolveContext(Parser& parser) noexcept
			: m_Parser{parser}
		{
		}

		~ResolveContext();

		Parser& GetParser() const noexcept
		{
			return m_Parser;
		}

		void StartResolvingDeclarator(Declaration::DeclaratorPtr decl);
		void EndResolvingDeclarator(Declaration::DeclaratorPtr const& decl);

		ResolvingState GetDeclaratorResolvingState(Declaration::DeclaratorPtr const& decl) const noexcept;

		std::unordered_set<Declaration::DeclaratorPtr> const& GetResolvedDeclarators() const noexcept
		{
			return m_ResolvedDeclarators;
		}

	private:
		Parser& m_Parser;
		std::unordered_set<Declaration::DeclaratorPtr> m_ResolvingDeclarators;
		std::unordered_set<Declaration::DeclaratorPtr> m_ResolvedDeclarators;
	};

	///	@brief	用于处理未知 Token，未知 Token 可以通过注册来表示特殊含义
	struct IUnknownTokenHandler
		: NatsuLib::natRefObj
	{
		using ResultCallback = std::function<nBool(ASTNodePtr)>;

		virtual ~IUnknownTokenHandler();

		///	@brief	处理未知 Token
		///	@param	parser		Parser
		///	@param	token		要处理的 Token
		///	@param	callback	处理回调
		///	@return	是否成功处理该 Token，若所有已注册的 Handler 都返回 false 则 Parser 将会报错，返回 true 将会阻止之后的 Handler 尝试处理
		virtual nBool HandleToken(Parser& parser, Lex::Token const& token, ResultCallback const& callback) = 0;
	};

	class Parser
	{
	public:
		class ParseScope
			: NatsuLib::nonmovable
		{
		public:
			ParseScope(Parser* self, Semantic::ScopeFlags flags);
			~ParseScope();

			void ExplicitExit();

		private:
			Parser* m_Self;
		};

		Parser(Preprocessor& preprocessor, Semantic::Sema& sema);
		~Parser();

		Preprocessor& GetPreprocessor() const noexcept;
		Diag::DiagnosticsEngine& GetDiagnosticsEngine() const noexcept;
		Semantic::Sema& GetSema() const noexcept;

		void ConsumeToken()
		{
			m_Preprocessor.Lex(m_CurrentToken);
		}

		Lex::Token const& GetCurrentToken() const noexcept
		{
			return m_CurrentToken;
		}

		void ConsumeParen()
		{
			assert(IsParen(m_CurrentToken.GetType()));
			if (m_CurrentToken.Is(Lex::TokenType::LeftParen))
			{
				++m_ParenCount;
			}
			else if (m_ParenCount)
			{
				--m_ParenCount;
			}
			ConsumeToken();
		}

		void ConsumeBracket()
		{
			assert(IsBracket(m_CurrentToken.GetType()));
			if (m_CurrentToken.Is(Lex::TokenType::LeftSquare))
			{
				++m_BracketCount;
			}
			else if (m_BracketCount)
			{
				--m_BracketCount;
			}
			ConsumeToken();
		}

		void ConsumeBrace()
		{
			assert(IsBrace(m_CurrentToken.GetType()));
			if (m_CurrentToken.Is(Lex::TokenType::LeftBrace))
			{
				++m_BraceCount;
			}
			else if (m_BraceCount)
			{
				--m_BraceCount;
			}
			ConsumeToken();
		}

		void ConsumeAnyToken()
		{
			const auto type = m_CurrentToken.GetType();
			if (IsParen(type))
			{
				ConsumeParen();
			}
			else if (IsBracket(type))
			{
				ConsumeBracket();
			}
			else if (IsBrace(type))
			{
				ConsumeBrace();
			}
			else
			{
				ConsumeToken();
			}
		}

#if PARSER_USE_EXCEPTION
		[[noreturn]] static Expression::ExprPtr ParseExprError();
		[[noreturn]] static Statement::StmtPtr ParseStmtError();
		[[noreturn]] static Declaration::DeclPtr ParseDeclError();
#else
		static Expression::ExprPtr ParseExprError() noexcept;
		static Statement::StmtPtr ParseStmtError() noexcept;
		static Declaration::DeclPtr ParseDeclError() noexcept;
#endif

		void DivertPhase(std::vector<Declaration::DeclPtr>& decls);

		///	@brief	分析顶层声明
		///	@param	decls	输出分析得到的顶层声明
		///	@return	是否遇到EOF
		nBool ParseTopLevelDecl(std::vector<Declaration::DeclPtr>& decls);
		void SkipSimpleCompilerAction(Declaration::Context context);
		std::vector<Declaration::DeclPtr> ParseExternalDeclaration(Declaration::Context context = Declaration::Context::Global);
		void ParseCompilerActionArguments(Declaration::Context context, const NatsuLib::natRefPointer<IActionContext>& actionContext);

		void ParseCompilerAction(Declaration::Context context, std::function<nBool(NatsuLib::natRefPointer<ASTNode>)> const& output = {});
		NatsuLib::natRefPointer<ICompilerAction> ParseCompilerActionName();
		nBool ParseCompilerActionArgument(const NatsuLib::natRefPointer<IActionContext>& actionContext, Declaration::Context context, nBool isSingle = true, NatsuLib::natRefPointer<IArgumentRequirement> argRequirement = nullptr, CompilerActionArgumentType argType = CompilerActionArgumentType::None);
		std::size_t ParseCompilerActionArgumentList(const NatsuLib::natRefPointer<IActionContext>& actionContext, Declaration::Context context, NatsuLib::natRefPointer<IArgumentRequirement> argRequirement = nullptr, CompilerActionArgumentType argType = CompilerActionArgumentType::None);
		std::size_t ParseCompilerActionArgumentSequence(const NatsuLib::natRefPointer<IActionContext>& actionContext, Declaration::Context context, NatsuLib::natRefPointer<IArgumentRequirement> argRequirement = nullptr, CompilerActionArgumentType argType = CompilerActionArgumentType::None);

		Declaration::DeclPtr ParseClassDeclaration();
		void ParseMemberSpecification(SourceLocation startLoc, Declaration::DeclPtr const& tagDecl);

		Declaration::DeclPtr ParseEnumDeclaration();
		void ParseEnumeratorList(NatsuLib::natRefPointer<Declaration::EnumDecl> const& tagDecl);

		std::vector<Declaration::DeclPtr> ParseModuleImport();
		Declaration::DeclPtr ParseModuleDecl();

		Declaration::DeclPtr ParseDeclaration(Declaration::Context context, SourceLocation& declEnd);
		Declaration::DeclPtr ParseAliasDeclaration(Declaration::Context context, SourceLocation& declEnd);
		Declaration::DeclPtr ParseAliasBody(SourceLocation aliasLoc, Identifier::IdPtr aliasId, SourceLocation aliasIdLoc, Declaration::Context context, SourceLocation& declEnd);

		Declaration::DeclPtr ParseFunctionBody(Declaration::DeclPtr decl, ParseScope& scope);

		Statement::StmtPtr ParseStatement(Declaration::Context context = Declaration::Context::Block, nBool mayBeExpr = false);
		Statement::StmtPtr ParseLabeledStatement(Identifier::IdPtr labelId, SourceLocation labelLoc);
		Statement::StmtPtr ParseCompoundStatement();
		Statement::StmtPtr ParseCompoundStatement(Semantic::ScopeFlags flags);
		Statement::StmtPtr ParseIfStatement();
		Statement::StmtPtr ParseWhileStatement();
		Statement::StmtPtr ParseForStatement();

		Statement::StmtPtr ParseContinueStatement();
		Statement::StmtPtr ParseBreakStatement();
		Statement::StmtPtr ParseReturnStatement();

		Statement::StmtPtr ParseExprStatement(nBool mayBeExpr = false);

		Expression::ExprPtr ParseExpression();
		Expression::ExprPtr ParseIdExpr();

		// unary-expression:
		//	postfix-expression
		//	'++' unary-expression
		//	'--' unary-expression
		//	unary-operator unary-expression
		//	new-expression
		//	delete-expression
		// unary-operator: one of
		//	'+' '-' '!' '~'
		// primary-expression:
		//	id-expression
		//	literal
		//	this
		//	'(' expression ')'
		// id-expression:
		//	unqualified-id
		//	qualified-id
		// unqualified-id:
		//	identifier
		Expression::ExprPtr ParseUnaryExpression();

		Expression::ExprPtr ParseRightOperandOfBinaryExpression(Expression::ExprPtr leftOperand,
		                                                        OperatorPrecedence minPrec = OperatorPrecedence::Assignment);

		// postfix-expression:
		//	primary-expression
		//	postfix-expression '[' expression ']'
		//	postfix-expression '(' argument-expression-list[opt] ')'
		//	postfix-expression '.' identifier
		//	postfix-expression '++'
		//	postfix-expression '--'
		//	postfix-expression 'as' type-name
		Expression::ExprPtr ParsePostfixExpressionSuffix(Expression::ExprPtr prefix);
		Expression::ExprPtr ParseConstantExpression();
		Expression::ExprPtr ParseAssignmentExpression();
		Expression::ExprPtr ParseThrowExpression();
		Expression::ExprPtr ParseParenExpression();

		std::pair<NatsuLib::natRefPointer<NestedNameSpecifier>, std::pair<Identifier::IdPtr, SourceLocation>> ParseMayBeQualifiedId();

		// argument-expression-list:
		//	argument-expression
		//	argument-expression-list ',' argument-expression
		nBool ParseExpressionList(std::vector<Expression::ExprPtr>& exprs, std::vector<SourceLocation>& commaLocs,
		                          Lex::TokenType endToken = Lex::TokenType::Eof);

		nBool ParseDeclarator(Declaration::DeclaratorPtr const& decl, nBool skipIdentifier = false);
		nBool ParseSpecifier(Declaration::DeclaratorPtr const& decl);
		nBool ParseTemplateParameterList(Declaration::DeclaratorPtr const& decl);

		nBool ParseType(Declaration::DeclaratorPtr const& decl);
		void ParseParenType(Declaration::DeclaratorPtr const& decl);
		void ParseFunctionType(Declaration::DeclaratorPtr const& decl);
		void ParseArrayOrPointerType(Declaration::DeclaratorPtr const& decl);

		nBool ParseInitializer(Declaration::DeclaratorPtr const& decl);

		nBool SkipUntil(std::initializer_list<Lex::TokenType> list, nBool dontConsume = false,
		                std::vector<Lex::Token>* skippedTokens = nullptr);

		Declaration::DeclPtr ResolveDeclarator(const Declaration::DeclaratorPtr& decl);

	private:
		Preprocessor& m_Preprocessor;
		Diag::DiagnosticsEngine& m_Diag;
		Semantic::Sema& m_Sema;

		Lex::Token m_CurrentToken;
		nuInt m_ParenCount, m_BracketCount, m_BraceCount;

		NatsuLib::natRefPointer<ResolveContext> m_ResolveContext;

		struct CachedCompilerAction
		{
			Declaration::Context Context;
			NatsuLib::natRefPointer<Semantic::Scope> Scope;
			Declaration::DeclPtr DeclContext;
			nBool InUnsafeScope;
			std::vector<Lex::Token> Tokens;
		};

		std::vector<CachedCompilerAction> m_CachedCompilerActions;

		void pushCachedTokens(std::vector<Lex::Token> tokens);
		void popCachedTokens();

		void skipToken(std::vector<Lex::Token>* skippedTokens = nullptr)
		{
			if (skippedTokens)
			{
				skippedTokens->emplace_back(m_CurrentToken);
			}

			ConsumeAnyToken();
		}

		void skipTypeAndInitializer(Declaration::DeclaratorPtr const& decl);
	};
}

namespace NatsuLang
{
	class ASTContext;
	struct ASTConsumer;

	void ParseAST(Preprocessor& pp, ASTContext& astContext, NatsuLib::natRefPointer<ASTConsumer> astConsumer);
	void ParseAST(Syntax::Parser& parser);
	void EndParsingAST(Syntax::Parser& parser);
}
