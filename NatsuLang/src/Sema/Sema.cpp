#include "Sema/Sema.h"
#include "Sema/Scope.h"
#include "Sema/Declarator.h"
#include "Lex/Preprocessor.h"
#include "Lex/LiteralParser.h"
#include "AST/Declaration.h"
#include "AST/ASTContext.h"
#include "AST/Expression.h"

using namespace NatsuLib;
using namespace NatsuLang::Semantic;

namespace
{
	constexpr NatsuLang::Declaration::IdentifierNamespace chooseIDNS(Sema::LookupNameType lookupNameType) noexcept
	{
		using NatsuLang::Declaration::IdentifierNamespace;
		switch (lookupNameType)
		{
		case Sema::LookupNameType::LookupOrdinaryName:
			return IdentifierNamespace::Ordinary | IdentifierNamespace::Tag | IdentifierNamespace::Member | IdentifierNamespace::Module;
		case Sema::LookupNameType::LookupTagName:
			return IdentifierNamespace::Type;
		case Sema::LookupNameType::LookupLabel:
			return IdentifierNamespace::Label;
		case Sema::LookupNameType::LookupMemberName:
			return IdentifierNamespace::Member | IdentifierNamespace::Tag | IdentifierNamespace::Ordinary;
		case Sema::LookupNameType::LookupModuleName:
			return IdentifierNamespace::Module;
		case Sema::LookupNameType::LookupAnyName:
			return IdentifierNamespace::Ordinary | IdentifierNamespace::Tag | IdentifierNamespace::Member | IdentifierNamespace::Module | IdentifierNamespace::Type;
		default:
			return IdentifierNamespace::None;
		}
	}

	NatsuLang::Expression::CastType getBuiltinCastType(natRefPointer<NatsuLang::Type::BuiltinType> const& fromType, natRefPointer<NatsuLang::Type::BuiltinType> const& toType) noexcept
	{
		using NatsuLang::Expression::CastType;
		using NatsuLang::Type::BuiltinType;

		if (!fromType || (!fromType->IsIntegerType() && !fromType->IsFloatingType()) ||
			!toType || (!toType->IsIntegerType() && !toType->IsFloatingType()))
		{
			return CastType::Invalid;
		}

		if (fromType->GetBuiltinClass() == toType->GetBuiltinClass())
		{
			return CastType::NoOp;
		}

		if (fromType->IsIntegerType())
		{
			if (toType->IsIntegerType())
			{
				return toType->GetBuiltinClass() == BuiltinType::Bool ? CastType::IntegralToBoolean : CastType::IntegralCast;
			}

			// fromType��toTypeֻ����IntegerType��FloatingType
			return CastType::IntegralToFloating;
		}

		// fromType��FloatingType
		if (toType->IsIntegerType())
		{
			return toType->GetBuiltinClass() == BuiltinType::Bool ? CastType::FloatingToBoolean : CastType::FloatingToIntegral;
		}

		return CastType::FloatingCast;
	}

	NatsuLang::Type::TypePtr getUnderlyingType(NatsuLang::Type::TypePtr const& type)
	{
		if (!type)
		{
			return nullptr;
		}

		auto deducedType = static_cast<natRefPointer<NatsuLang::Type::DeducedType>>(type);
		if (deducedType)
		{
			return getUnderlyingType(deducedType->GetDeducedAsType());
		}

		auto typeofType = static_cast<natRefPointer<NatsuLang::Type::TypeOfType>>(type);
		if (typeofType)
		{
			return getUnderlyingType(typeofType->GetUnderlyingType());
		}

		auto parenType = static_cast<natRefPointer<NatsuLang::Type::ParenType>>(type);
		if (parenType)
		{
			return getUnderlyingType(parenType->GetInnerType());
		}

		return type;
	}

	NatsuLang::Type::TypePtr getCommonType(NatsuLang::Type::TypePtr const& type1, NatsuLang::Type::TypePtr const& type2)
	{
		nat_Throw(NotImplementedException);
	}

	constexpr NatsuLang::Expression::UnaryOperationType getUnaryOperationType(NatsuLang::Token::TokenType tokenType) noexcept
	{
		using NatsuLang::Expression::UnaryOperationType;

		switch (tokenType)
		{
		case NatsuLang::Token::TokenType::Plus:
			return UnaryOperationType::Plus;
		case NatsuLang::Token::TokenType::PlusPlus:
			return UnaryOperationType::PreInc;
		case NatsuLang::Token::TokenType::Minus:
			return UnaryOperationType::Minus;
		case NatsuLang::Token::TokenType::MinusMinus:
			return UnaryOperationType::PreDec;
		case NatsuLang::Token::TokenType::Tilde:
			return UnaryOperationType::Not;
		case NatsuLang::Token::TokenType::Exclaim:
			return UnaryOperationType::LNot;
		default:
			assert(!"Invalid TokenType for UnaryOperationType.");
			return UnaryOperationType::Invalid;
		}
	}

	constexpr NatsuLang::Expression::BinaryOperationType getBinaryOperationType(NatsuLang::Token::TokenType tokenType) noexcept
	{
		using NatsuLang::Expression::BinaryOperationType;

		switch (tokenType)
		{
		case NatsuLang::Token::TokenType::Amp:
			return BinaryOperationType::And;
		case NatsuLang::Token::TokenType::AmpAmp:
			return BinaryOperationType::LAnd;
		case NatsuLang::Token::TokenType::AmpEqual:
			return BinaryOperationType::AndAssign;
		case NatsuLang::Token::TokenType::Star:
			return BinaryOperationType::Mul;
		case NatsuLang::Token::TokenType::StarEqual:
			return BinaryOperationType::MulAssign;
		case NatsuLang::Token::TokenType::Plus:
			return BinaryOperationType::Add;
		case NatsuLang::Token::TokenType::PlusEqual:
			return BinaryOperationType::AddAssign;
		case NatsuLang::Token::TokenType::Minus:
			return BinaryOperationType::Sub;
		case NatsuLang::Token::TokenType::MinusEqual:
			return BinaryOperationType::SubAssign;
		case NatsuLang::Token::TokenType::ExclaimEqual:
			return BinaryOperationType::NE;
		case NatsuLang::Token::TokenType::Slash:
			return BinaryOperationType::Div;
		case NatsuLang::Token::TokenType::SlashEqual:
			return BinaryOperationType::DivAssign;
		case NatsuLang::Token::TokenType::Percent:
			return BinaryOperationType::Mod;
		case NatsuLang::Token::TokenType::PercentEqual:
			return BinaryOperationType::RemAssign;
		case NatsuLang::Token::TokenType::Less:
			return BinaryOperationType::LT;
		case NatsuLang::Token::TokenType::LessLess:
			return BinaryOperationType::Shl;
		case NatsuLang::Token::TokenType::LessEqual:
			return BinaryOperationType::LE;
		case NatsuLang::Token::TokenType::LessLessEqual:
			return BinaryOperationType::ShlAssign;
		case NatsuLang::Token::TokenType::Greater:
			return BinaryOperationType::GT;
		case NatsuLang::Token::TokenType::GreaterGreater:
			return BinaryOperationType::Shr;
		case NatsuLang::Token::TokenType::GreaterEqual:
			return BinaryOperationType::GE;
		case NatsuLang::Token::TokenType::GreaterGreaterEqual:
			return BinaryOperationType::ShrAssign;
		case NatsuLang::Token::TokenType::Caret:
			return BinaryOperationType::Xor;
		case NatsuLang::Token::TokenType::CaretEqual:
			return BinaryOperationType::XorAssign;
		case NatsuLang::Token::TokenType::Pipe:
			return BinaryOperationType::Or;
		case NatsuLang::Token::TokenType::PipePipe:
			return BinaryOperationType::LOr;
		case NatsuLang::Token::TokenType::PipeEqual:
			return BinaryOperationType::OrAssign;
		case NatsuLang::Token::TokenType::Equal:
			return BinaryOperationType::Assign;
		case NatsuLang::Token::TokenType::EqualEqual:
			return BinaryOperationType::EQ;
		default:
			assert(!"Invalid TokenType for BinaryOperationType.");
			return BinaryOperationType::Invalid;
		}
	}
}

Sema::Sema(Preprocessor& preprocessor, ASTContext& astContext)
	: m_Preprocessor{ preprocessor }, m_Context{ astContext }, m_Diag{ preprocessor.GetDiag() }, m_SourceManager{ preprocessor.GetSourceManager() }
{
}

Sema::~Sema()
{
}

void Sema::PushDeclContext(natRefPointer<Scope> const& scope, Declaration::DeclContext* dc)
{
	assert(dc);
	auto declPtr = Declaration::Decl::CastFromDeclContext(dc)->ForkRef();
	assert(declPtr->GetContext() == Declaration::Decl::CastToDeclContext(m_CurrentDeclContext.Get()));
	m_CurrentDeclContext = declPtr;
	scope->SetEntity(dc);
}

void Sema::PopDeclContext()
{
	assert(m_CurrentDeclContext);
	const auto parentDc = m_CurrentDeclContext->GetContext();
	assert(parentDc);
	m_CurrentDeclContext = Declaration::Decl::CastFromDeclContext(parentDc)->ForkRef();
}

natRefPointer<NatsuLang::Declaration::Decl> Sema::OnModuleImport(SourceLocation startLoc, SourceLocation importLoc, ModulePathType const& path)
{
	nat_Throw(NatsuLib::NotImplementedException);
}

NatsuLang::Type::TypePtr Sema::GetTypeName(natRefPointer<Identifier::IdentifierInfo> const& id, SourceLocation nameLoc, natRefPointer<Scope> scope, Type::TypePtr const& objectType)
{
	Declaration::DeclContext* context{};

	if (objectType && objectType->GetType() == Type::Type::Record)
	{
		const auto tagType = static_cast<natRefPointer<Type::TagType>>(objectType);
		if (tagType)
		{
			context = tagType->GetDecl().Get();
		}
	}

	LookupResult result{ *this, id, nameLoc, LookupNameType::LookupOrdinaryName };
	if (context)
	{
		if (!LookupQualifiedName(result, context))
		{
			LookupName(result, scope);
		}
	}
	else
	{
		LookupName(result, scope);
	}

	switch (result.GetResultType())
	{
	default:
		assert(!"Invalid result type.");
		[[fallthrough]];
	case LookupResult::LookupResultType::NotFound:
	case LookupResult::LookupResultType::FoundOverloaded:
	case LookupResult::LookupResultType::Ambiguous:
		return nullptr;
	case LookupResult::LookupResultType::Found:
	{
		assert(result.GetDeclSize() == 1);
		const auto decl = *result.GetDecls().begin();
		if (auto typeDecl = static_cast<natRefPointer<Declaration::TypeDecl>>(decl))
		{
			return typeDecl->GetTypeForDecl();
		}
		return nullptr;
	}
	}
}

nBool Sema::LookupName(LookupResult& result, natRefPointer<Scope> scope) const
{
	for (; scope; scope = scope->GetParent().Lock())
	{
		const auto context = scope->GetEntity();
		if (LookupQualifiedName(result, context))
		{
			return true;
		}
	}

	result.ResolveResultType();
	return false;
}

nBool Sema::LookupQualifiedName(LookupResult& result, Declaration::DeclContext* context) const
{
	const auto id = result.GetLookupId();
	const auto lookupType = result.GetLookupType();
	auto found = false;

	auto query = context->Lookup(id);
	switch (lookupType)
	{
	case LookupNameType::LookupTagName:
		query = query.where([](natRefPointer<Declaration::NamedDecl> const& decl)
		{
			return static_cast<natRefPointer<Declaration::TagDecl>>(decl);
		});
		break;
	case LookupNameType::LookupLabel:
		query = query.where([](natRefPointer<Declaration::NamedDecl> const& decl)
		{
			return static_cast<natRefPointer<Declaration::LabelDecl>>(decl);
		});
		break;
	case LookupNameType::LookupMemberName:
		query = query.where([](natRefPointer<Declaration::NamedDecl> const& decl)
		{
			const auto type = decl->GetType();
			return type == Declaration::Decl::Method || type == Declaration::Decl::Field;
		});
		break;
	case LookupNameType::LookupModuleName:
		query = query.where([](natRefPointer<Declaration::NamedDecl> const& decl)
		{
			return static_cast<natRefPointer<Declaration::ModuleDecl>>(decl);
		});
		break;
	default:
		assert(!"Invalid lookupType");
		[[fallthrough]];
	case LookupNameType::LookupOrdinaryName:
	case LookupNameType::LookupAnyName:
		break;
	}
	auto queryResult{ query.Cast<std::vector<natRefPointer<Declaration::NamedDecl>>>() };
	if (!queryResult.empty())
	{
		result.AddDecl(from(queryResult));
		found = true;
	}

	result.ResolveResultType();
	return found;
}

nBool Sema::LookupNestedName(LookupResult& result, natRefPointer<Scope> scope, natRefPointer<NestedNameSpecifier> const& nns)
{
	if (nns)
	{
		auto dc = nns->GetAsDeclContext(m_Context);
		return LookupQualifiedName(result, dc);
	}

	return LookupName(result, scope);
}

NatsuLang::Type::TypePtr Sema::ActOnTypeName(natRefPointer<Scope> const& scope, Declaration::Declarator const& decl)
{
	static_cast<void>(scope);
	return decl.GetType();
}

natRefPointer<NatsuLang::Declaration::ParmVarDecl> Sema::ActOnParamDeclarator(natRefPointer<Scope> const& scope, Declaration::Declarator const& decl)
{
	
}

natRefPointer<NatsuLang::Declaration::NamedDecl> Sema::HandleDeclarator(natRefPointer<Scope> const& scope, Declaration::Declarator const& decl)
{
	auto id = decl.GetIdentifier();

	if (decl.GetContext() != Declaration::Context::Prototype && !id)
	{
		m_Diag.Report(Diag::DiagnosticsEngine::DiagID::ErrExpectedIdentifier, decl.GetRange().GetBegin());
		return nullptr;
	}


}

NatsuLang::Expression::ExprPtr Sema::ActOnBooleanLiteral(Token::Token const& token) const
{
	assert(token.IsAnyOf({ Token::TokenType::Kw_true, Token::TokenType::Kw_false }));
	return make_ref<Expression::BooleanLiteral>(token.Is(Token::TokenType::Kw_true), m_Context.GetBuiltinType(Type::BuiltinType::Bool), token.GetLocation());
}

NatsuLang::Expression::ExprPtr Sema::ActOnNumericLiteral(Token::Token const& token) const
{
	assert(token.Is(Token::TokenType::NumericLiteral));

	Lex::NumericLiteralParser literalParser{ token.GetLiteralContent().value(), token.GetLocation(), m_Diag };

	if (literalParser.Errored())
	{
		return nullptr;
	}

	if (literalParser.IsFloatingLiteral())
	{
		Type::BuiltinType::BuiltinClass builtinType;
		if (literalParser.IsFloat())
		{
			builtinType = Type::BuiltinType::Float;
		}
		else if (literalParser.IsLong())
		{
			builtinType = Type::BuiltinType::LongDouble;
		}
		else
		{
			builtinType = Type::BuiltinType::Double;
		}

		auto type = m_Context.GetBuiltinType(builtinType);

		nDouble value;
		if (literalParser.GetFloatValue(value))
		{
			// TODO: �������
		}

		return make_ref<Expression::FloatingLiteral>(value, std::move(type), token.GetLocation());
	}

	// ������������
	Type::BuiltinType::BuiltinClass builtinType;
	if (literalParser.IsLong())
	{
		builtinType = literalParser.IsUnsigned() ? Type::BuiltinType::ULong : Type::BuiltinType::Long;
	}
	else if (literalParser.IsLongLong())
	{
		builtinType = literalParser.IsUnsigned() ? Type::BuiltinType::ULongLong : Type::BuiltinType::LongLong;
	}
	else
	{
		builtinType = literalParser.IsUnsigned() ? Type::BuiltinType::UInt : Type::BuiltinType::Int;
	}

	auto type = m_Context.GetBuiltinType(builtinType);

	nuLong value;
	if (literalParser.GetIntegerValue(value))
	{
		// TODO: �������
	}

	return make_ref<Expression::IntegerLiteral>(value, std::move(type), token.GetLocation());
}

NatsuLang::Expression::ExprPtr Sema::ActOnCharLiteral(Token::Token const& token) const
{
	assert(token.Is(Token::TokenType::CharLiteral) && token.GetLiteralContent().has_value());

	Lex::CharLiteralParser literalParser{ token.GetLiteralContent().value(), token.GetLocation(), m_Diag };

	if (literalParser.Errored())
	{
		return nullptr;
	}

	return make_ref<Expression::CharacterLiteral>(literalParser.GetValue(), nString::UsingStringType, m_Context.GetBuiltinType(Type::BuiltinType::Char), token.GetLocation());
}

NatsuLang::Expression::ExprPtr Sema::ActOnStringLiteral(Token::Token const& token)
{
	assert(token.Is(Token::TokenType::StringLiteral) && token.GetLiteralContent().has_value());

	Lex::StringLiteralParser literalParser{ token.GetLiteralContent().value(), token.GetLocation(), m_Diag };

	if (literalParser.Errored())
	{
		return nullptr;
	}

	// TODO: �����ַ����������Ա�����
	auto value = literalParser.GetValue();
	return make_ref<Expression::StringLiteral>(value, m_Context.GetArrayType(m_Context.GetBuiltinType(Type::BuiltinType::Char), value.GetSize()), token.GetLocation());
}

NatsuLang::Expression::ExprPtr Sema::ActOnThrow(natRefPointer<Scope> const& scope, SourceLocation loc, Expression::ExprPtr expr)
{
	// TODO
	if (expr)
	{

	}
}

NatsuLang::Expression::ExprPtr Sema::ActOnIdExpr(natRefPointer<Scope> const& scope, natRefPointer<NestedNameSpecifier> const& nns, Identifier::IdPtr id, nBool hasTraillingLParen)
{
	LookupResult result{ *this, id, {}, LookupNameType::LookupOrdinaryName };
	if (!LookupNestedName(result, scope, nns) || result.GetResultType() == LookupResult::LookupResultType::Ambiguous)
	{
		return nullptr;
	}

	// TODO: ���Ժ���������ʽ���õı�ʶ����ȡ����Ĵ���
	static_cast<void>(hasTraillingLParen);

	if (result.GetDeclSize() == 1)
	{
		return BuildDeclarationNameExpr(nns, std::move(id), result.GetDecls().first());
	}
	
	// TODO: ֻ�����غ��������ڴ��ҵ�������������򱨴�
	nat_Throw(NotImplementedException);
}

NatsuLang::Expression::ExprPtr Sema::ActOnThis(SourceLocation loc)
{
	assert(m_CurrentDeclContext);
	auto dc = Declaration::Decl::CastToDeclContext(m_CurrentDeclContext.Get());
	while (dc->GetType() == Declaration::Decl::Enum)
	{
		dc = Declaration::Decl::CastFromDeclContext(dc)->GetContext();
	}

	auto decl = Declaration::Decl::CastFromDeclContext(dc)->ForkRef();

	if (auto methodDecl = static_cast<natRefPointer<Declaration::MethodDecl>>(decl))
	{
		auto recordDecl = Declaration::Decl::CastFromDeclContext(methodDecl->GetContext())->ForkRef<Declaration::RecordDecl>();
		assert(recordDecl);
		return make_ref<Expression::ThisExpr>(loc, recordDecl->GetTypeForDecl(), false);
	}

	// TODO: ���浱ǰ�����Ĳ�����ʹ�� this �Ĵ���
	return nullptr;
}

NatsuLang::Expression::ExprPtr Sema::ActOnAsTypeExpr(natRefPointer<Scope> const& scope, Expression::ExprPtr exprToCast, Type::TypePtr type, SourceLocation loc)
{
	return make_ref<Expression::AsTypeExpr>(std::move(type), getCastType(exprToCast, type), std::move(exprToCast));
}

NatsuLang::Expression::ExprPtr Sema::ActOnArraySubscriptExpr(natRefPointer<Scope> const& scope, Expression::ExprPtr base, SourceLocation lloc, Expression::ExprPtr index, SourceLocation rloc)
{
	// TODO: ����δʹ�ò������棬��Щ���������ڽ����İ汾��ʹ��
	static_cast<void>(scope);
	static_cast<void>(lloc);

	// TODO: ��ǰ��֧�ֶ��ڽ�������д˲���
	auto baseType = static_cast<natRefPointer<Type::ArrayType>>(base->GetExprType());
	if (!baseType)
	{
		// TODO: ������������������ڽ�����
		return nullptr;
	}

	// TODO: ��ǰ�������±�Ϊ�ڽ���������
	auto indexType = static_cast<natRefPointer<Type::BuiltinType>>(index->GetExprType());
	if (!indexType || !indexType->IsIntegerType())
	{
		// TODO: �����±�������������ڽ���������
		return nullptr;
	}

	return make_ref<Expression::ArraySubscriptExpr>(base, index, baseType->GetElementType(), rloc);
}

NatsuLang::Expression::ExprPtr Sema::ActOnCallExpr(natRefPointer<Scope> const& scope, Expression::ExprPtr func, SourceLocation lloc, Linq<const Expression::ExprPtr> argExprs, SourceLocation rloc)
{
	// TODO: ������ز���

	if (!func)
	{
		return nullptr;
	}

	auto fn = static_cast<natRefPointer<Expression::DeclRefExpr>>(func->IgnoreParens());
	if (!fn)
	{
		return nullptr;
	}
	auto refFn = fn->GetDecl();
	if (!refFn)
	{
		return nullptr;
	}
	auto fnType = static_cast<natRefPointer<Type::FunctionType>>(refFn->GetValueType());
	if (!fnType)
	{
		return nullptr;
	}

	return make_ref<Expression::CallExpr>(refFn, argExprs, fnType->GetResultType(), rloc);
}

NatsuLang::Expression::ExprPtr Sema::ActOnMemberAccessExpr(natRefPointer<Scope> const& scope, Expression::ExprPtr base, SourceLocation periodLoc, natRefPointer<NestedNameSpecifier> const& nns, Identifier::IdPtr id)
{
	auto baseType = base->GetExprType();

	LookupResult r{ *this, id, {}, LookupNameType::LookupMemberName };
	const auto record = static_cast<natRefPointer<Type::RecordType>>(baseType);
	if (record)
	{
		const auto recordDecl = record->GetDecl();

		Declaration::DeclContext* dc = recordDecl.Get();
		if (nns)
		{
			dc = nns->GetAsDeclContext(m_Context);
		}

		// TODO: ��dc�ĺϷ��Խ��м��

		LookupQualifiedName(r, dc);

		if (r.IsEmpty())
		{
			// TODO: �Ҳ��������Ա
		}

		return BuildMemberReferenceExpr(scope, std::move(base), std::move(baseType), periodLoc, nns, r);
	}

	// TODO: ��ʱ��֧�ֶ�RecordType��������ͽ��г�Ա���ʲ���
	return nullptr;
}

NatsuLang::Expression::ExprPtr Sema::ActOnUnaryOp(natRefPointer<Scope> const& scope, SourceLocation loc, Token::TokenType tokenType, Expression::ExprPtr operand)
{
	// TODO: Ϊ�������ܵĲ��������ر���
	static_cast<void>(scope);
	return CreateBuiltinUnaryOp(loc, getUnaryOperationType(tokenType), std::move(operand));
}

NatsuLang::Expression::ExprPtr Sema::ActOnPostfixUnaryOp(natRefPointer<Scope> const& scope, SourceLocation loc, Token::TokenType tokenType, Expression::ExprPtr operand)
{
	// TODO: Ϊ�������ܵĲ��������ر���
	static_cast<void>(scope);

	assert(tokenType == Token::TokenType::PlusPlus || tokenType == Token::TokenType::MinusMinus);
	return CreateBuiltinUnaryOp(loc,
		tokenType == Token::TokenType::PlusPlus ?
			Expression::UnaryOperationType::PostInc :
			Expression::UnaryOperationType::PostDec,
		std::move(operand));
}

NatsuLang::Expression::ExprPtr Sema::ActOnBinaryOp(natRefPointer<Scope> const& scope, SourceLocation loc, Token::TokenType tokenType, Expression::ExprPtr leftOperand, Expression::ExprPtr rightOperand)
{
	static_cast<void>(scope);
	return BuildBuiltinBinaryOp(loc, getBinaryOperationType(tokenType), std::move(leftOperand), std::move(rightOperand));
}

NatsuLang::Expression::ExprPtr Sema::BuildBuiltinBinaryOp(SourceLocation loc, Expression::BinaryOperationType binOpType, Expression::ExprPtr leftOperand, Expression::ExprPtr rightOperand)
{
	Type::TypePtr resultType;

	// TODO: ��ɴ� binOpType ��� resultType �Ĳ���
	switch (binOpType)
	{
	case Expression::BinaryOperationType::Invalid:
		break;
	case Expression::BinaryOperationType::Mul:
		break;
	case Expression::BinaryOperationType::Div:
		break;
	case Expression::BinaryOperationType::Mod:
		break;
	case Expression::BinaryOperationType::Add:
		break;
	case Expression::BinaryOperationType::Sub:
		break;
	case Expression::BinaryOperationType::Shl:
		break;
	case Expression::BinaryOperationType::Shr:
		break;
	case Expression::BinaryOperationType::LT:
		break;
	case Expression::BinaryOperationType::GT:
		break;
	case Expression::BinaryOperationType::LE:
		break;
	case Expression::BinaryOperationType::GE:
		break;
	case Expression::BinaryOperationType::EQ:
		break;
	case Expression::BinaryOperationType::NE:
		break;
	case Expression::BinaryOperationType::And:
		break;
	case Expression::BinaryOperationType::Xor:
		break;
	case Expression::BinaryOperationType::Or:
		break;
	case Expression::BinaryOperationType::LAnd:
		break;
	case Expression::BinaryOperationType::LOr:
		break;
	case Expression::BinaryOperationType::Assign:
		break;
	case Expression::BinaryOperationType::MulAssign:
		break;
	case Expression::BinaryOperationType::DivAssign:
		break;
	case Expression::BinaryOperationType::RemAssign:
		break;
	case Expression::BinaryOperationType::AddAssign:
		break;
	case Expression::BinaryOperationType::SubAssign:
		break;
	case Expression::BinaryOperationType::ShlAssign:
		break;
	case Expression::BinaryOperationType::ShrAssign:
		break;
	case Expression::BinaryOperationType::AndAssign:
		break;
	case Expression::BinaryOperationType::XorAssign:
		break;
	case Expression::BinaryOperationType::OrAssign:
		break;
	default:
		break;
	}
}

NatsuLang::Expression::ExprPtr Sema::ActOnConditionalOp(SourceLocation questionLoc, SourceLocation colonLoc, Expression::ExprPtr condExpr, Expression::ExprPtr leftExpr, Expression::ExprPtr rightExpr)
{
	return make_ref<Expression::ConditionalOperator>(std::move(condExpr), questionLoc, std::move(leftExpr), colonLoc, std::move(rightExpr), getCommonType(leftExpr->GetExprType(), rightExpr->GetExprType()));
}

NatsuLang::Expression::ExprPtr Sema::BuildDeclarationNameExpr(natRefPointer<NestedNameSpecifier> const& nns, Identifier::IdPtr id, natRefPointer<Declaration::NamedDecl> decl)
{
	auto valueDecl = static_cast<natRefPointer<Declaration::ValueDecl>>(decl);
	if (!valueDecl)
	{
		// �������õĲ���ֵ
		return nullptr;
	}

	return BuildDeclRefExpr(std::move(valueDecl), valueDecl->GetValueType(), std::move(id), nns);
}

NatsuLang::Expression::ExprPtr Sema::BuildDeclRefExpr(natRefPointer<Declaration::ValueDecl> decl, Type::TypePtr type, Identifier::IdPtr id, natRefPointer<NestedNameSpecifier> const& nns)
{
	static_cast<void>(id);
	return make_ref<Expression::DeclRefExpr>(nns, std::move(decl), SourceLocation{}, std::move(type));
}

NatsuLang::Expression::ExprPtr Sema::BuildMemberReferenceExpr(natRefPointer<Scope> const& scope, Expression::ExprPtr baseExpr, Type::TypePtr baseType, SourceLocation opLoc, natRefPointer<NestedNameSpecifier> const& nns, LookupResult& r)
{
	r.SetBaseObjectType(baseType);

	switch (r.GetResultType())
	{
	case LookupResult::LookupResultType::Found:
	{
		assert(r.GetDeclSize() == 1);
		auto decl = r.GetDecls().first();
		const auto type = decl->GetType();

		if (!baseExpr)
		{
			// ��ʽ��Ա����

			if (type != Declaration::Decl::Field && type != Declaration::Decl::Method)
			{
				// ���ʵ��Ǿ�̬��Ա
				return BuildDeclarationNameExpr(nns, r.GetLookupId(), std::move(decl));
			}

			baseExpr = make_ref<Expression::ThisExpr>(SourceLocation{}, r.GetBaseObjectType(), true);
		}

		if (auto field = static_cast<natRefPointer<Declaration::FieldDecl>>(decl))
		{
			return BuildFieldReferenceExpr(std::move(baseExpr), opLoc, nns, std::move(field), r.GetLookupId());
		}

		if (auto var = static_cast<natRefPointer<Declaration::VarDecl>>(decl))
		{
			return make_ref<Expression::MemberExpr>(std::move(baseExpr), opLoc, std::move(var), r.GetLookupId(), var->GetValueType());
		}

		if (auto method = static_cast<natRefPointer<Declaration::MethodDecl>>(decl))
		{
			return make_ref<Expression::MemberExpr>(std::move(baseExpr), opLoc, std::move(method), r.GetLookupId(), method->GetValueType());
		}

		return nullptr;
	}
	case LookupResult::LookupResultType::FoundOverloaded:
		// TODO: �������ص����
		nat_Throw(NotImplementedException);
	default:
		assert(!"Invalid result type.");
		[[fall_through]];
	case LookupResult::LookupResultType::NotFound:
	case LookupResult::LookupResultType::Ambiguous:
		// TODO: �������
		return nullptr;
	}
}

NatsuLang::Expression::ExprPtr Sema::BuildFieldReferenceExpr(Expression::ExprPtr baseExpr, SourceLocation opLoc, natRefPointer<NestedNameSpecifier> const& nns, natRefPointer<Declaration::FieldDecl> field, Identifier::IdPtr id)
{
	// TODO
	nat_Throw(NotImplementedException);
}

NatsuLang::Expression::ExprPtr Sema::CreateBuiltinUnaryOp(SourceLocation opLoc, Expression::UnaryOperationType opCode, Expression::ExprPtr operand)
{
	Type::TypePtr resultType;

	switch (opCode)
	{
	case Expression::UnaryOperationType::PostInc:
	case Expression::UnaryOperationType::PostDec:
	case Expression::UnaryOperationType::PreInc:
	case Expression::UnaryOperationType::PreDec:
	case Expression::UnaryOperationType::Plus:
	case Expression::UnaryOperationType::Minus:
	case Expression::UnaryOperationType::Not:
		// TODO: ���ܵ�����������
		resultType = operand->GetExprType();
		break;
	case Expression::UnaryOperationType::LNot:
		resultType = m_Context.GetBuiltinType(Type::BuiltinType::Bool);
		break;
	case Expression::UnaryOperationType::Invalid:
	default:
		// TODO: �������
		return nullptr;
	}

	return make_ref<Expression::UnaryOperator>(std::move(operand), opCode, std::move(resultType), opLoc);
}

NatsuLang::Expression::CastType Sema::getCastType(Expression::ExprPtr operand, Type::TypePtr toType)
{
	toType = getUnderlyingType(toType);
	auto fromType = getUnderlyingType(operand->GetExprType());

	assert(operand && toType);
	assert(fromType);

	// TODO
	if (fromType->GetType() == Type::Type::Builtin)
	{
		auto builtinFromType = static_cast<natRefPointer<Type::BuiltinType>>(fromType);

		switch (toType->GetType())
		{
		case Type::Type::Builtin:
			return getBuiltinCastType(fromType, toType);
		case Type::Type::Enum:
			if (builtinFromType->IsIntegerType())
			{
				return Expression::CastType::IntegralCast;
			}
			if (builtinFromType->IsFloatingType())
			{
				return Expression::CastType::FloatingToIntegral;
			}
			return Expression::CastType::Invalid;
		case Type::Type::Record:
			// TODO: ����û�����ת��
			return Expression::CastType::Invalid;
		case Type::Type::Auto:
		case Type::Type::Array:
		case Type::Type::Function:
		case Type::Type::TypeOf:
		case Type::Type::Paren:
		default:
			return Expression::CastType::Invalid;
		}
	}
}

LookupResult::LookupResult(Sema& sema, Identifier::IdPtr id, SourceLocation loc, Sema::LookupNameType lookupNameType)
	: m_Sema{ sema }, m_LookupId{ std::move(id) }, m_LookupLoc{ loc }, m_LookupNameType{ lookupNameType }, m_IDNS{ chooseIDNS(m_LookupNameType) }, m_Result{}, m_AmbiguousType{}
{
}

void LookupResult::AddDecl(natRefPointer<Declaration::NamedDecl> decl)
{
	m_Decls.emplace(std::move(decl));
	m_Result = LookupResultType::Found;
}

void LookupResult::AddDecl(Linq<natRefPointer<Declaration::NamedDecl>> decls)
{
	m_Decls.insert(decls.begin(), decls.begin());
	m_Result = LookupResultType::Found;
}

void LookupResult::ResolveResultType() noexcept
{
	const auto size = m_Decls.size();

	if (size == 0)
	{
		m_Result = LookupResultType::NotFound;
		return;
	}
	
	if (size == 1)
	{
		m_Result = LookupResultType::Found;
		return;
	}

	// ���Ѿ��϶�Ϊ����������Ҫ��һ���޸�
	if (m_Result == LookupResultType::Ambiguous)
	{
		return;
	}

	// �����ҵ�����������������ػ��Ƕ�����
	if (from(m_Decls).all([](natRefPointer<Declaration::NamedDecl> const& decl)
		{
			return static_cast<natRefPointer<Declaration::FunctionDecl>>(decl);
		}))
	{
		m_Result = LookupResultType::FoundOverloaded;
	}
	else
	{
		// TODO: ���Ͻ�����Ҫ��һ������
		m_Result = LookupResultType::Ambiguous;
	}
}

Linq<natRefPointer<const NatsuLang::Declaration::NamedDecl>> LookupResult::GetDecls() const noexcept
{
	return from(m_Decls);
}
