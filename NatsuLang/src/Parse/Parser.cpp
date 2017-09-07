#include "Parse/Parser.h"
#include "Sema/Sema.h"
#include "Sema/Scope.h"
#include "Sema/Declarator.h"
#include "AST/Type.h"
#include "AST/ASTContext.h"

using namespace NatsuLib;
using namespace NatsuLang::Syntax;
using namespace NatsuLang::Token;
using namespace NatsuLang::Diag;

Parser::Parser(Preprocessor& preprocessor, Semantic::Sema& sema)
	: m_Preprocessor{ preprocessor }, m_Diag{ preprocessor.GetDiag() }, m_Sema{ sema }, m_ParenCount{}, m_BracketCount{}, m_BraceCount{}
{
	m_CurrentToken.SetType(TokenType::Eof);
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

nBool Parser::ParseTopLevelDecl(std::vector<Declaration::DeclPtr>& decls)
{
	if (m_CurrentToken.Is(TokenType::Eof))
	{
		ConsumeToken();
	}

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
		SourceLocation declEnd;
		return ParseDeclaration(Declaration::Context::Global, declEnd);
	default:
		// TODO: �������
		return {};
	}
}

std::vector<NatsuLang::Declaration::DeclPtr> Parser::ParseModuleImport()
{
	assert(m_CurrentToken.Is(Token::TokenType::Kw_import));
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
	// �Ե� def
	ConsumeToken();

	Declaration::Declarator decl{ context };
	ParseDeclarator(decl);

	return { m_Sema.HandleDeclarator(m_Sema.GetCurrentScope(), decl) };
}

NatsuLang::Statement::StmtPtr Parser::ParseStatement()
{
	Statement::StmtPtr result;
	const auto tokenType = m_CurrentToken.GetType();

	// TODO: ��ɸ��� tokenType �ж�������͵Ĺ���
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
		break;
	case TokenType::Kw_for:
		break;
	case TokenType::Kw_goto:
		break;
	case TokenType::Kw_continue:
		break;
	case TokenType::Kw_break:
		break;
	case TokenType::Kw_return:
		break;
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
	const auto scope = make_scope([this] { ConsumeBrace(); });

	std::vector<Statement::StmtPtr> stmtVec;

	while (!m_CurrentToken.IsAnyOf({ TokenType::RightBrace, TokenType::Eof }))
	{
		auto stmt = ParseStatement();
		if (stmt)
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

	ParseScope ifScope{ this, Semantic::ScopeFlags::DeclarableScope | Semantic::ScopeFlags::ControlScope };

	auto cond = ParseParenExpression();

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

		if (trailingElseLoc)
		{
			*trailingElseLoc = elseLoc;
		}

		ConsumeToken();

		elseStmtLoc = m_CurrentToken.GetLocation();

		ParseScope thenScope{ this, Semantic::ScopeFlags::DeclarableScope };
		elseStmt = ParseStatement();
	}

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
			// TODO: �������
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
		auto asLoc = m_CurrentToken.GetLocation();

		// �Ե� as
		ConsumeToken();

		Declaration::Declarator decl{ Declaration::Context::TypeName };
		ParseDeclarator(decl);
		if (!decl.IsValid())
		{
			// TODO: �������
			operand = nullptr;
		}

		auto type = m_Sema.ActOnTypeName(m_Sema.GetCurrentScope(), decl);
		if (!type)
		{
			// TODO: �������
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
			return leftOperand;
		}

		auto opToken = m_CurrentToken;
		ConsumeToken();

		Expression::ExprPtr ternaryMiddle;
		if (tokenPrec == OperatorPrecedence::Conditional)
		{
			ternaryMiddle = ParseExpression();
			if (!ternaryMiddle)
			{
				// TODO: �������
				leftOperand = nullptr;
			}

			if (!m_CurrentToken.Is(TokenType::Colon))
			{
				// TODO: �������ȱʧ��':'�Ǻ�
			}

			colonLoc = m_CurrentToken.GetLocation();
			ConsumeToken();
		}

		auto rightOperand = tokenPrec <= OperatorPrecedence::Conditional ? ParseAssignmentExpression() : ParseCastExpression();
		if (!rightOperand)
		{
			// TODO: �������
			leftOperand = nullptr;
		}

		auto prevPrec = tokenPrec;
		tokenPrec = GetOperatorPrecedence(m_CurrentToken.GetType());

		auto isRightAssoc = prevPrec == OperatorPrecedence::Assignment || prevPrec == OperatorPrecedence::Conditional;

		if (prevPrec < tokenPrec || (prevPrec == tokenPrec && isRightAssoc))
		{
			rightOperand = ParseRightOperandOfBinaryExpression(std::move(rightOperand),
				static_cast<OperatorPrecedence>(static_cast<std::underlying_type_t<OperatorPrecedence>>(prevPrec) + !isRightAssoc));
			if (!rightOperand)
			{
				// TODO: �������
				leftOperand = nullptr;
			}

			tokenPrec = GetOperatorPrecedence(m_CurrentToken.GetType());
		}

		// TODO: ���֮ǰ�ķ������ֳ���Ļ����������ᱻ���㣬����֮ǰ�Ѿ������˴����ڴ˿��Բ����б��棬���������ִ���Ա�֤�������������ʽ
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
			auto lloc = m_CurrentToken.GetLocation();
			ConsumeBracket();
			auto index = ParseExpression();

			if (!m_CurrentToken.Is(TokenType::RightSquare))
			{
				m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
					.AddArgument(TokenType::RightSquare)
					.AddArgument(m_CurrentToken.GetType());

				return ParseExprError();
			}

			auto rloc = m_CurrentToken.GetLocation();
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

			auto lloc = m_CurrentToken.GetLocation();
			ConsumeParen();

			if (!m_CurrentToken.Is(TokenType::RightParen) && !ParseExpressionList(argExprs, commaLocs))
			{
				// TODO: �������
				return ParseExprError();
			}

			if (!m_CurrentToken.Is(TokenType::RightParen))
			{
				// TODO: �������
				return ParseExprError();
			}

			ConsumeParen();

			prefix = m_Sema.ActOnCallExpr(m_Sema.GetCurrentScope(), std::move(prefix), lloc, from(argExprs), m_CurrentToken.GetLocation());

			break;
		}
		case TokenType::Period:
		{
			auto periodLoc = m_CurrentToken.GetLocation();
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

	// (: int)Ҳ���ԣ�
	if (m_CurrentToken.Is(TokenType::Colon) || (context == Declaration::Context::Prototype && !decl.GetIdentifier()))
	{
		ParseSpecifier(decl);
	}

	// ��������ԭ��ʱҲ����ָ��initializer��
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
	const auto token = m_CurrentToken;
	ConsumeToken();

	if (!token.Is(TokenType::Colon) && context != Declaration::Context::Prototype)
	{
		// �ڷ���������ԭ�͵��������в���ʽд�����ͣ���Ϊ����auto
		// auto����������ָ��initializer֮�����ʵ������
		return;
	}

	const auto tokenType = m_CurrentToken.GetType();
	switch (tokenType)
	{
	case TokenType::Identifier:
	{
		// ��ͨ����������

		auto type = m_Sema.GetTypeName(m_CurrentToken.GetIdentifierInfo(), m_CurrentToken.GetLocation(), m_Sema.GetCurrentScope(), nullptr);
		if (!type)
		{
			return;
		}

		ConsumeToken();

		break;
	}
	case TokenType::LeftParen:
	{
		// �������ͻ�����������
		ParseFunctionType(decl);

		break;
	}
	case TokenType::RightParen:
	{
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
			// ������Ч���͵Ĵ���
			m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedTypeSpecifierGot, m_CurrentToken.GetLocation())
				.AddArgument(tokenType);
			return;
		}
		decl.SetType(m_Sema.GetASTContext().GetBuiltinType(builtinClass));
		break;
	}
	}

	// ��ʹ���Ͳ������鳢��ParseҲ�������
	ParseArrayType(decl);
}

void Parser::ParseParenType(Declaration::Declarator& decl)
{
	assert(m_CurrentToken.Is(TokenType::LeftParen));
	// �Ե�������
	ConsumeParen();

	ParseType(decl);
	if (!decl.GetType())
	{
		if (m_CurrentToken.Is(TokenType::Identifier))
		{
			// ��������
			ParseFunctionType(decl);
			return;
		}
		
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier);
	}
}

void Parser::ParseFunctionType(Declaration::Declarator& decl)
{
	assert(m_CurrentToken.Is(TokenType::Identifier));

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
			// TODO: ������󣺲��������ͺͳ�ʼ��������Ҫ����һ��
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

	// ��ȡ�꺯��������Ϣ����ʼ��ȡ��������

	// �������->��ֻ��һ�������Ʋ���˵������ͨ����������
	if (!m_CurrentToken.Is(TokenType::Arrow))
	{
		if (mayBeParenType)
		{
			// ���������ͣ����������Ѿ���Token��������ˡ�����
			decl = std::move(paramDecls[0]);
			return;
		}

		// �Ժ�����Ԫ����������͵�֧����
		m_Diag.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Arrow)
			.AddArgument(m_CurrentToken.GetType());
	}

	ConsumeToken();

	Declaration::Declarator retType{ Declaration::Context::Prototype };
	ParseType(retType);

	// TODO: �ý���Sema�����ˣ�����һ��function prototype
	nat_Throw(NotImplementedException);
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
		decl.SetInitializer(ParseExpression());
		return;
	}

	if (m_CurrentToken.Is(TokenType::LeftBrace))
	{
		if (decl.GetType()->GetType() != Type::Type::Function)
		{
			// TODO: �������
			return;
		}

		// TODO: ����������Ϊ������
	}

	// ���� initializer������
}

nBool Parser::SkipUntil(std::initializer_list<Token::TokenType> list, nBool dontConsume)
{
	// ���������������ֻ���������ļ���β�����ǲ���Ҫ�������ж�������Ϣ
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
