#include "Sema/Sema.h"
#include "Sema/Scope.h"
#include "Sema/Declarator.h"
#include "Lex/Preprocessor.h"
#include "AST/Declaration.h"
#include "AST/ASTContext.h"

using namespace NatsuLib;
using namespace NatsuLang::Semantic;

namespace
{
	constexpr NatsuLang::Declaration::IdentifierNamespace ChooseIDNS(Sema::LookupNameType lookupNameType) noexcept
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
}

Sema::Sema(Preprocessor& preprocessor, ASTContext& astContext)
	: m_Preprocessor{ preprocessor }, m_Context{ astContext }, m_Diag{ preprocessor.GetDiag() }, m_SourceManager{ preprocessor.GetSourceManager() }
{
}

Sema::~Sema()
{
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

NatsuLang::Type::TypePtr Sema::ActOnTypeName(natRefPointer<Scope> const& scope, Declaration::Declarator const& decl)
{

}

natRefPointer<NatsuLang::Declaration::ParmVarDecl> Sema::ActOnParamDeclarator(natRefPointer<Scope> const& scope, Declaration::Declarator const& decl)
{
	
}

natRefPointer<NatsuLang::Declaration::NamedDecl> Sema::HandleDeclarator(natRefPointer<Scope> const& scope, Declaration::Declarator const& decl)
{
	auto id = decl.GetIdentifier();

	if (!id)
	{
		m_Diag.Report(Diag::DiagnosticsEngine::DiagID::ErrExpectedIdentifier, decl.GetRange().GetBegin());
		return nullptr;
	}


}

LookupResult::LookupResult(Sema& sema, Identifier::IdPtr id, SourceLocation loc, Sema::LookupNameType lookupNameType)
	: m_Sema{ sema }, m_LookupId{ std::move(id) }, m_LookupLoc{ loc }, m_LookupNameType{ lookupNameType }, m_IDNS{ ChooseIDNS(m_LookupNameType) }, m_Result{}, m_AmbiguousType{}
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
	if (from(m_Decls).all([](Declaration::DeclPtr const& decl)
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
