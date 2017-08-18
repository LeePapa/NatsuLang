#include "Parse/Parser.h"
#include "Sema/Sema.h"
#include "Sema/Declarator.h"
#include "AST/Type.h"
#include "AST/ASTContext.h"

using namespace NatsuLib;
using namespace NatsuLang::Syntax;
using namespace NatsuLang::Token;
using namespace NatsuLang::Diag;

Parser::Parser(Preprocessor& preprocessor, Semantic::Sema& sema)
	: m_Preprocessor{ preprocessor }, m_DiagnosticsEngine{ preprocessor.GetDiag() }, m_Sema{ sema }, m_ParenCount{}, m_BracketCount{}, m_BraceCount{}
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
	return m_DiagnosticsEngine;
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
		break;
	case TokenType::RightBrace:
		m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExtraneousClosingBrace);
		ConsumeBrace();
		return {};
	case TokenType::Eof:
		m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrUnexpectEOF);
		return {};
	case TokenType::Kw_def:
		return ParseDeclaration(Declaration::Context::Global);
	default:
		break;
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
			m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier, m_CurrentToken.GetLocation());
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
std::vector<NatsuLang::Declaration::DeclPtr> Parser::ParseDeclaration(Declaration::Context context)
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
	// TODO
	nat_Throw(NotImplementedException);
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
		// TODO
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
		result = m_Sema.ActOnIdExpr(m_Sema.GetCurrentScope(), nullptr, id, m_CurrentToken.Is(TokenType::LeftParen));
		break;
	}
	case TokenType::PlusPlus:
	case TokenType::MinusMinus:
	{
		// TODO
		break;
	}
	case TokenType::Plus:
	case TokenType::Minus:
	case TokenType::Exclaim:
	case TokenType::Tilde:
		// TODO
		break;
	case TokenType::Kw_this:
		break;
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
	Expression::ExprPtr result;
	
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
				m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
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
			// TODO
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
		m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::RightParen)
			.AddArgument(m_CurrentToken.GetType());
	}
	else
	{
		ConsumeParen();
	}
	
	return ret;
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
		m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier, m_CurrentToken.GetLocation());
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
		// typeof

		break;
	}
	default:
	{
		const auto builtinClass = Type::BuiltinType::GetBuiltinClassFromTokenType(tokenType);
		if (builtinClass == Type::BuiltinType::Invalid)
		{
			// ������Ч���͵Ĵ���
			m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedTypeSpecifierGot, m_CurrentToken.GetLocation())
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
		
		m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedIdentifier);
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
			m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedDeclarator, m_CurrentToken.GetLocation());
		}

		if (!param.GetType() && !param.GetInitializer())
		{
			// ���������ͺͳ�ʼ��������Ҫ����һ��
		}

		if (m_CurrentToken.Is(TokenType::RightParen))
		{
			ConsumeParen();
			break;
		}

		if (!m_CurrentToken.Is(TokenType::Comma))
		{
			m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
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
		m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
			.AddArgument(TokenType::Arrow)
			.AddArgument(m_CurrentToken.GetType());
	}

	ConsumeToken();

	Declaration::Declarator retType{ Declaration::Context::Prototype };
	ParseType(retType);

	// �ý���Sema�����ˣ�����һ��function prototype
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
			m_DiagnosticsEngine.Report(DiagnosticsEngine::DiagID::ErrExpectedGot, m_CurrentToken.GetLocation())
				.AddArgument(TokenType::RightSquare)
				.AddArgument(m_CurrentToken.GetType());
		}
		ConsumeAnyToken();
	}
}

// initializer:
//	= expression
//	{ statement }
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
