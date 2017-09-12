﻿#include "Parse/Parser.h"
#include "Sema/Sema.h"
#include "Sema/Scope.h"
#include "Sema/Declarator.h"
#include "AST/Type.h"
#include "AST/ASTContext.h"
#include "AST/ASTConsumer.h"
#include "AST/Expression.h"
#include "AST/Declaration.h"

using namespace NatsuLib;
using namespace NatsuLang;
using namespace NatsuLang::Syntax;
using namespace NatsuLang::Lex;
using namespace NatsuLang::Diag;

Parser::Parser(Preprocessor& preprocessor, Semantic::Sema& sema)
	: m_Preprocessor{ preprocessor }, m_Diag{ preprocessor.GetDiag() }, m_Sema{ sema }, m_ParenCount{}, m_BracketCount{}, m_BraceCount{}
{
	ConsumeToken();
}

Parser::~Parser()
{
}

NatsuLang::Preprocessor& Parser::GetPreprocessor() const noexcept
{
	return m_Preprocessor;
}

DiagnosticsEngine& Parser::GetDiagnosticsEngine() const noexcept
{
	return m_Diag;
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

nBool Parser::ParseTopLevelDecl(std::vector<Declaration::DeclPtr>& decls)
{
	switch (m_CurrentToken.GetType())
	{
	case TokenType::Kw_import:
		decls = ParseModuleImport();
		return false;
	case TokenType::Kw_module:
		decls = ParseModuleDecl();
		return false;
	case TokenType::Eof:
		return true;
	default:
		break;
	}

	decls = ParseExternalDeclaration();
	return false;
}

std::vector<NatsuLang::Declaration::DeclPtr> Parser::ParseExternalDeclaration()
{
	switch (m_CurrentToken.GetType())
	{
	case TokenType::Semi:
		// Empty Declaration
		ConsumeToken();
		return {};
	case TokenType::RightBrace:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExtraneousClosingBrace);
		ConsumeBrace();
		return {};
	case TokenType::Eof:
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF);
		return {};
	case TokenType::Kw_def:
	{
		SourceLocation declEnd;
		return ParseDeclaration(Declaration::Context::Global, declEnd);
	}
	default:
		// TODO: 报告错误
		return {};
	}
}

std::vector<NatsuLang::Declaration::DeclPtr> Parser::ParseModuleImport()
{
	assert(m_CurrentToken.Is(Lex::TokenType::Kw_import));
	auto startLoc = m_CurrentToken.GetLocation();
	ConsumeToken();

	std::vector<std::pair<natRefPointer<Identifier::IdentifierInfo>, SourceLocation>> path;
	if (!ParseModuleName(path))
	{
		return {};
	}

	nat_Throw(NotImplementedException);
}

std::vector<NatsuLang::Declaration::DeclPtr> Parser::ParseModuleDecl()
{
	nat_Throw(NotImplementedException);
}

nBool Parser::ParseModuleName(std::vector<std::pair<natRefPointer<Identifier::IdentifierInfo>, SourceLocation>>& path)
{
	while (true)
	{
		if (!m_CurrentToken.Is(TokenType::Identifier))
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier, m_CurrentToken.GetLocation());
			// SkipUntil({ TokenType::Semi });
			return false;
		}

		path.emplace_back(m_CurrentToken.GetIdentifierInfo(), m_CurrentToken.GetLocation());
		ConsumeToken();

		if (!m_CurrentToken.Is(TokenType::Period))
		{
			return true;
		}

		ConsumeToken();
	}
}

// declaration:
//	simple-declaration
// simple-declaration:
//	def declarator [;]
std::vector<NatsuLang::Declaration::DeclPtr> Parser::ParseDeclaration(Declaration::Context context, NatsuLang::SourceLocation& declEnd)
{
	assert(m_CurrentToken.Is(TokenType::Kw_def));
	// 吃掉 def
	ConsumeToken();

	Declaration::Declarator decl{ context };
	ParseDeclarator(decl);

	return { m_Sema.HandleDeclarator(m_Sema.GetCurrentScope(), decl) };
}

NatsuLang::Declaration::DeclPtr Parser::ParseFunctionBody(Declaration::DeclPtr decl, ParseScope& scope)
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

NatsuLang::Statement::StmtPtr Parser::ParseStatement()
{
	Statement::StmtPtr result;
	const auto tokenType = m_CurrentToken.GetType();

	// TODO: 完成根据 tokenType 判断语句类型的过程
	switch (tokenType)
	{
	case TokenType::Identifier:
	{
		auto id = m_CurrentToken.GetIdentifierInfo();
		const auto loc = m_CurrentToken.GetLocation();

		ConsumeToken();

		if (m_CurrentToken.Is(TokenType::Colon))
		{
			return ParseLabeledStatement(std::move(id), loc);
		}

		return ParseStmtError();
	}
	case TokenType::LeftBrace:
		return ParseCompoundStatement();
	case TokenType::Semi:
	{
		const auto loc = m_CurrentToken.GetLocation();
		ConsumeToken();
		return m_Sema.ActOnNullStmt(loc);
	}
	case TokenType::Kw_def:
	{
		const auto declBegin = m_CurrentToken.GetLocation();
		SourceLocation declEnd;
		auto decls = ParseDeclaration(Declaration::Context::Block, declEnd);
		return m_Sema.ActOnDeclStmt(move(decls), declBegin, declEnd);
	}
	case TokenType::Kw_if:
		return ParseIfStatement();
	case TokenType::Kw_while:
		return ParseWhileStatement();
	case TokenType::Kw_for:
		break;
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
	default:
		return ParseStmtError();
	}

	// TODO
	nat_Throw(NotImplementedException);
}

NatsuLang::Statement::StmtPtr Parser::ParseLabeledStatement(Identifier::IdPtr labelId, SourceLocation labelLoc)
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

NatsuLang::Statement::StmtPtr Parser::ParseCompoundStatement()
{
	return ParseCompoundStatement(Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::CompoundStmtScope);
}

NatsuLang::Statement::StmtPtr Parser::ParseCompoundStatement(Semantic::ScopeFlags flags)
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

NatsuLang::Statement::StmtPtr Parser::ParseIfStatement()
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

NatsuLang::Statement::StmtPtr Parser::ParseWhileStatement()
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

NatsuLang::Statement::StmtPtr Parser::ParseContinueStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_continue));
	const auto loc = m_CurrentToken.GetLocation();
	ConsumeToken();
	return m_Sema.ActOnContinueStatement(loc, m_Sema.GetCurrentScope());
}

NatsuLang::Statement::StmtPtr Parser::ParseBreakStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_break));
	const auto loc = m_CurrentToken.GetLocation();
	ConsumeToken();
	return m_Sema.ActOnBreakStatement(loc, m_Sema.GetCurrentScope());
}

NatsuLang::Statement::StmtPtr Parser::ParseReturnStatement()
{
	assert(m_CurrentToken.Is(TokenType::Kw_return));
	const auto loc = m_CurrentToken.GetLocation();
	ConsumeToken();

	Expression::ExprPtr returnedExpr;
	if (!m_CurrentToken.Is(TokenType::Semi))
	{
		returnedExpr = ParseExpression();
	}

	return m_Sema.ActOnReturnStmt(loc, std::move(returnedExpr), m_Sema.GetCurrentScope());
}

NatsuLang::Expression::ExprPtr Parser::ParseExpression()
{
	return ParseRightOperandOfBinaryExpression(ParseAssignmentExpression());
}

NatsuLang::Expression::ExprPtr Parser::ParseCastExpression()
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
	case TokenType::Identifier:
	{
		auto id = m_CurrentToken.GetIdentifierInfo();
		ConsumeToken();
		result = m_Sema.ActOnIdExpr(m_Sema.GetCurrentScope(), nullptr, std::move(id), m_CurrentToken.Is(TokenType::LeftParen));
		break;
	}
	case TokenType::PlusPlus:
	case TokenType::MinusMinus:
	case TokenType::Plus:
	case TokenType::Minus:
	case TokenType::Exclaim:
	case TokenType::Tilde:
	{
		const auto loc = m_CurrentToken.GetLocation();
		ConsumeToken();
		result = ParseCastExpression();
		if (!result)
		{
			// TODO: 报告错误
			result = ParseExprError();
		}

		result = m_Sema.ActOnUnaryOp(m_Sema.GetCurrentScope(), loc, tokenType, std::move(result));
		break;
	}
	case TokenType::Kw_this:
		return m_Sema.ActOnThis(m_CurrentToken.GetLocation());
	default:
		return ParseExprError();
	}

	return ParseAsTypeExpression(ParsePostfixExpressionSuffix(std::move(result)));
}

NatsuLang::Expression::ExprPtr Parser::ParseAsTypeExpression(Expression::ExprPtr operand)
{
	while (m_CurrentToken.Is(TokenType::Kw_as))
	{
		const auto asLoc = m_CurrentToken.GetLocation();

		// 吃掉 as
		ConsumeToken();

		Declaration::Declarator decl{ Declaration::Context::TypeName };
		ParseDeclarator(decl);
		if (!decl.IsValid())
		{
			// TODO: 报告错误
			operand = nullptr;
		}

		auto type = m_Sema.ActOnTypeName(m_Sema.GetCurrentScope(), decl);
		if (!type)
		{
			// TODO: 报告错误
			operand = nullptr;
		}

		if (operand)
		{
			operand = m_Sema.ActOnAsTypeExpr(m_Sema.GetCurrentScope(), std::move(operand), std::move(type), asLoc);
		}
	}

	return std::move(operand);
}

NatsuLang::Expression::ExprPtr Parser::ParseRightOperandOfBinaryExpression(Expression::ExprPtr leftOperand, OperatorPrecedence minPrec)
{
	auto tokenPrec = GetOperatorPrecedence(m_CurrentToken.GetType());
	SourceLocation colonLoc;

	while (true)
	{
		if (tokenPrec < minPrec)
		{
			return std::move(leftOperand);
		}

		auto opToken = m_CurrentToken;
		ConsumeToken();

		Expression::ExprPtr ternaryMiddle;
		if (tokenPrec == OperatorPrecedence::Conditional)
		{
			ternaryMiddle = ParseExpression();
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

		auto rightOperand = tokenPrec <= OperatorPrecedence::Conditional ? ParseAssignmentExpression() : ParseCastExpression();
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
			leftOperand = ternaryMiddle ?
				m_Sema.ActOnConditionalOp(opToken.GetLocation(), colonLoc, std::move(leftOperand), std::move(ternaryMiddle), std::move(rightOperand)) :
				m_Sema.ActOnBinaryOp(m_Sema.GetCurrentScope(), opToken.GetLocation(), opToken.GetType(), std::move(leftOperand), std::move(rightOperand));
		}
	}
}

NatsuLang::Expression::ExprPtr Parser::ParsePostfixExpressionSuffix(Expression::ExprPtr prefix)
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

				return ParseExprError();
			}

			const auto rloc = m_CurrentToken.GetLocation();
			prefix = m_Sema.ActOnArraySubscriptExpr(m_Sema.GetCurrentScope(), std::move(prefix), lloc, std::move(index), rloc);
			ConsumeBracket();

			break;
		}
		case TokenType::LeftParen:
		{
			if (!prefix)
			{
				return ParseExprError();
			}

			std::vector<Expression::ExprPtr> argExprs;
			std::vector<SourceLocation> commaLocs;

			const auto lloc = m_CurrentToken.GetLocation();
			ConsumeParen();

			if (!m_CurrentToken.Is(TokenType::RightParen) && !ParseExpressionList(argExprs, commaLocs))
			{
				// TODO: 报告错误
				return ParseExprError();
			}

			if (!m_CurrentToken.Is(TokenType::RightParen))
			{
				// TODO: 报告错误
				return ParseExprError();
			}

			ConsumeParen();

			prefix = m_Sema.ActOnCallExpr(m_Sema.GetCurrentScope(), std::move(prefix), lloc, from(argExprs), m_CurrentToken.GetLocation());

			break;
		}
		case TokenType::Period:
		{
			const auto periodLoc = m_CurrentToken.GetLocation();
			ConsumeToken();
			Identifier::IdPtr unqualifiedId;
			if (!ParseUnqualifiedId(unqualifiedId))
			{
				return ParseExprError();
			}

			prefix = m_Sema.ActOnMemberAccessExpr(m_Sema.GetCurrentScope(), std::move(prefix), periodLoc, nullptr, unqualifiedId);

			break;
		}
		case TokenType::PlusPlus:
		case TokenType::MinusMinus:
			prefix = m_Sema.ActOnPostfixUnaryOp(m_Sema.GetCurrentScope(), m_CurrentToken.GetLocation(), m_CurrentToken.GetType(), std::move(prefix));
			ConsumeToken();
			break;
		default:
			return std::move(prefix);
		}
	}
}

NatsuLang::Expression::ExprPtr Parser::ParseConstantExpression()
{
	return ParseRightOperandOfBinaryExpression(ParseCastExpression(), OperatorPrecedence::Conditional);
}

NatsuLang::Expression::ExprPtr Parser::ParseAssignmentExpression()
{
	if (m_CurrentToken.Is(TokenType::Kw_throw))
	{
		return ParseThrowExpression();
	}

	return ParseRightOperandOfBinaryExpression(ParseCastExpression());
}

NatsuLang::Expression::ExprPtr Parser::ParseThrowExpression()
{
	assert(m_CurrentToken.Is(TokenType::Kw_throw));
	auto throwLocation = m_CurrentToken.GetLocation();
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
	default:
		auto expr = ParseAssignmentExpression();
		if (!expr)
		{
			return ParseExprError();
		}
		return m_Sema.ActOnThrow(m_Sema.GetCurrentScope(), throwLocation, std::move(expr));
	}
}

NatsuLang::Expression::ExprPtr Parser::ParseParenExpression()
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));

	ConsumeParen();
	auto ret = ParseExpression();
	if (!m_CurrentToken.Is(TokenType::RightParen))
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::RightParen)
			.AddArgument(m_CurrentToken.GetType());
	}
	else
	{
		ConsumeParen();
	}
	
	return ret;
}

nBool Parser::ParseUnqualifiedId(Identifier::IdPtr& result)
{
	if (!m_CurrentToken.Is(TokenType::Identifier))
	{
		return false;
	}

	result = m_CurrentToken.GetIdentifierInfo();
	ConsumeToken();
	return true;
}

nBool Parser::ParseExpressionList(std::vector<Expression::ExprPtr>& exprs, std::vector<SourceLocation>& commaLocs)
{
	while (true)
	{
		auto expr = ParseAssignmentExpression();
		if (!expr)
		{
			SkipUntil({ TokenType::RightParen }, true);
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

	return true;
}

// declarator:
//	[identifier] [specifier-seq] [initializer] [;]
void Parser::ParseDeclarator(Declaration::Declarator& decl)
{
	const auto context = decl.GetContext();
	if (m_CurrentToken.Is(TokenType::Identifier))
	{
		decl.SetIdentifier(m_CurrentToken.GetIdentifierInfo());
		ConsumeToken();
	}
	else if (context != Declaration::Context::Prototype)
	{
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier, m_CurrentToken.GetLocation());
		return;
	}

	// (: int)也可以？
	if (m_CurrentToken.Is(TokenType::Colon) || (context == Declaration::Context::Prototype && !decl.GetIdentifier()))
	{
		ParseSpecifier(decl);
	}

	// 声明函数原型时也可以指定initializer？
	if (m_CurrentToken.IsAnyOf({ TokenType::Equal, TokenType::LeftBrace }))
	{
		ParseInitializer(decl);
	}
}

// specifier-seq:
//	specifier-seq specifier
// specifier:
//	type-specifier
void Parser::ParseSpecifier(Declaration::Declarator& decl)
{
	ParseType(decl);
}

// type-specifier:
//	[auto]
//	typeof-specifier
//	type-identifier
void Parser::ParseType(Declaration::Declarator& decl)
{
	const auto context = decl.GetContext();

	if (!m_CurrentToken.Is(TokenType::Colon))
	{
		if (context != Declaration::Context::Prototype)
		{
			// 在非声明函数原型的上下文中不显式写出类型，视为隐含auto
			// auto的声明符在指定initializer之后决定实际类型
			return;
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
		// 普通或数组类型

		auto type = m_Sema.GetTypeName(m_CurrentToken.GetIdentifierInfo(), m_CurrentToken.GetLocation(), m_Sema.GetCurrentScope(), nullptr);
		if (!type)
		{
			return;
		}

		decl.SetType(std::move(type));

		ConsumeToken();

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
		assert(!"Wrong token");
		ConsumeParen();

		return;
	}
	case TokenType::Kw_typeof:
	{
		// TODO: typeof

		break;
	}
	default:
	{
		const auto builtinClass = Type::BuiltinType::GetBuiltinClassFromTokenType(tokenType);
		if (builtinClass == Type::BuiltinType::Invalid)
		{
			// 对于无效类型的处理
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedTypeSpecifierGot, m_CurrentToken.GetLocation())
				.AddArgument(tokenType);
			return;
		}
		decl.SetType(m_Sema.GetASTContext().GetBuiltinType(builtinClass));
		ConsumeToken();
		break;
	}
	}

	// 即使类型不是数组尝试Parse也不会错误
	ParseArrayType(decl);
}

void Parser::ParseParenType(Declaration::Declarator& decl)
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));
	// 吃掉左括号
	ConsumeParen();

	ParseType(decl);
	if (!decl.GetType())
	{
		if (m_CurrentToken.Is(TokenType::Identifier))
		{
			// 函数类型
			ParseFunctionType(decl);
			return;
		}
		
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier);
	}
}

void Parser::ParseFunctionType(Declaration::Declarator& decl)
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));
	ConsumeParen();

	std::vector<Declaration::Declarator> paramDecls;

	auto mayBeParenType = true;

	while (true)
	{
		Declaration::Declarator param{ Declaration::Context::Prototype };
		ParseDeclarator(param);
		if (mayBeParenType && param.GetIdentifier() || !param.GetType())
		{
			mayBeParenType = false;
		}

		if (param.IsValid())
		{
			paramDecls.emplace_back(std::move(param));
		}
		else
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedDeclarator, m_CurrentToken.GetLocation());
		}

		if (!param.GetType() && !param.GetInitializer())
		{
			// TODO: 报告错误：参数的类型和初始化器至少要存在一个
		}

		if (m_CurrentToken.Is(TokenType::RightParen))
		{
			ConsumeParen();
			break;
		}

		if (!m_CurrentToken.Is(TokenType::Comma))
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				.AddArgument(TokenType::Comma)
				.AddArgument(m_CurrentToken.GetType());
		}

		mayBeParenType = false;
		ConsumeToken();
	}

	// 读取完函数参数信息，开始读取返回类型

	// 如果不是->且只有一个无名称参数说明是普通的括号类型
	if (!m_CurrentToken.Is(TokenType::Arrow))
	{
		if (mayBeParenType)
		{
			// 是括号类型，但是我们已经把Token处理完毕了。。。
			decl = std::move(paramDecls[0]);
			return;
		}

		// 以后会加入元组或匿名类型的支持吗？
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Arrow)
			.AddArgument(m_CurrentToken.GetType());
	}

	ConsumeToken();

	Declaration::Declarator retType{ Declaration::Context::Prototype };
	ParseType(retType);

	decl.SetType(m_Sema.BuildFunctionType(retType.GetType(), from(paramDecls)
		.select([](Declaration::Declarator const& paramDecl)
				{
					return paramDecl.GetType();
				})));

	decl.SetParams(from(paramDecls)
		.select([this](Declaration::Declarator const& paramDecl)
				{
					return m_Sema.ActOnParamDeclarator(m_Sema.GetCurrentScope(), paramDecl);
				}));
}

void Parser::ParseArrayType(Declaration::Declarator& decl)
{
	while (m_CurrentToken.Is(TokenType::LeftSquare))
	{
		ConsumeBracket();
		auto countExpr = ParseConstantExpression();
		decl.SetType(m_Sema.GetASTContext().GetArrayType(decl.GetType(), std::move(countExpr)));
		if (!m_CurrentToken.Is(TokenType::RightSquare))
		{
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				.AddArgument(TokenType::RightSquare)
				.AddArgument(m_CurrentToken.GetType());
		}
		ConsumeAnyToken();
	}
}

// initializer:
//	= expression
//	compound-statement
void Parser::ParseInitializer(Declaration::Declarator& decl)
{
	if (m_CurrentToken.Is(TokenType::Equal))
	{
		ConsumeToken();
		decl.SetInitializer(ParseExpression());
		return;
	}

	if (m_CurrentToken.Is(TokenType::LeftBrace))
	{
		if (decl.GetType()->GetType() != Type::Type::Function)
		{
			// TODO: 报告错误
			return;
		}

		ParseScope bodyScope{ this, Semantic::ScopeFlags::FunctionScope | Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::CompoundStmtScope };
		auto funcDecl = m_Sema.ActOnStartOfFunctionDef(m_Sema.GetCurrentScope(), decl);
		decl.SetDecl(ParseFunctionBody(std::move(funcDecl), bodyScope));
	}

	// 不是 initializer，返回
}

nBool Parser::SkipUntil(std::initializer_list<NatsuLang::Lex::TokenType> list, nBool dontConsume)
{
	// 特例，如果调用者只是想跳到文件结尾，我们不需要再另外判断其他信息
	if (list.size() == 1 && *list.begin() == TokenType::Eof)
	{
		while (!m_CurrentToken.Is(TokenType::Eof))
		{
			ConsumeAnyToken();
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
					ConsumeToken();
				}

				return true;
			}
		}

		if (currentType == TokenType::Eof)
		{
			return false;
		}

		ConsumeAnyToken();
	}
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

	std::vector<Declaration::DeclPtr> decls;

	for (auto atEof = parser.ParseTopLevelDecl(decls); !atEof; atEof = parser.ParseTopLevelDecl(decls))
	{
		if (!astConsumer->HandleTopLevelDecl(from(decls)))
		{
			return;
		}
	}

	astConsumer->HandleTranslationUnit(astContext);
}
