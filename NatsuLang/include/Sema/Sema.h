#pragma once
#include <natMisc.h>
#include <natRefObj.h>
#include <unordered_set>
#include "Basic/SourceLocation.h"
#include "Basic/Identifier.h"
#include "AST/Declaration.h"
#include "AST/Type.h"

namespace NatsuLang
{
	namespace Declaration
	{
		class Declarator;
	}

	namespace Diag
	{
		class DiagnosticsEngine;
	}

	namespace Identifier
	{
		class IdentifierInfo;
		using IdPtr = NatsuLib::natRefPointer<IdentifierInfo>;
	}

	class Preprocessor;
	class ASTContext;
	class SourceManager;
}

namespace NatsuLang::Semantic
{
	class LookupResult;
	class Scope;

	class Sema
		: public NatsuLib::nonmovable
	{
	public:
		enum class ExpressionEvaluationContext
		{
			Unevaluated,
			DiscardedStatement,
			ConstantEvaluated,
			PotentiallyEvaluated,
			PotentiallyEvaluatedIfUsed
		};

		enum class LookupNameType
		{
			LookupOrdinaryName,
			LookupTagName,
			LookupLabel,
			LookupMemberName,
			LookupModuleName,
			LookupAnyName
		};

		using ModulePathType = std::vector<std::pair<NatsuLib::natRefPointer<Identifier::IdentifierInfo>, SourceLocation>>;

		explicit Sema(Preprocessor& preprocessor, ASTContext& astContext);
		~Sema();

		Preprocessor& GetPreprocessor() const noexcept
		{
			return m_Preprocessor;
		}

		ASTContext& GetASTContext() const noexcept
		{
			return m_Context;
		}

		Diag::DiagnosticsEngine& GetDiagnosticsEngine() const noexcept
		{
			return m_Diag;
		}

		SourceManager& GetSourceManager() const noexcept
		{
			return m_SourceManager;
		}

		NatsuLib::natRefPointer<Scope> GetCurrentScope() const noexcept
		{
			return m_CurrentScope;
		}

		NatsuLib::natRefPointer<Declaration::Decl> OnModuleImport(SourceLocation startLoc, SourceLocation importLoc, ModulePathType const& path);

		Type::TypePtr GetTypeName(NatsuLib::natRefPointer<Identifier::IdentifierInfo> const& id, SourceLocation nameLoc, NatsuLib::natRefPointer<Scope> scope, Type::TypePtr const& objectType);

		nBool LookupName(LookupResult& result, NatsuLib::natRefPointer<Scope> scope) const;
		nBool LookupQualifiedName(LookupResult& result, Declaration::DeclContext* context) const;

		Type::TypePtr ActOnTypeName(NatsuLib::natRefPointer<Scope> const& scope, Declaration::Declarator const& decl);
		NatsuLib::natRefPointer<Declaration::ParmVarDecl> ActOnParamDeclarator(NatsuLib::natRefPointer<Scope> const& scope, Declaration::Declarator const& decl);
		NatsuLib::natRefPointer<Declaration::NamedDecl> HandleDeclarator(NatsuLib::natRefPointer<Scope> const& scope, Declaration::Declarator const& decl);

		Expression::ExprPtr ActOnThrow(NatsuLib::natRefPointer<Scope> const& scope, SourceLocation loc, Expression::ExprPtr expr);

	private:
		Preprocessor& m_Preprocessor;
		ASTContext& m_Context;
		Diag::DiagnosticsEngine& m_Diag;
		SourceManager& m_SourceManager;

		NatsuLib::natRefPointer<Scope> m_CurrentScope;
	};

	class LookupResult
	{
	public:
		enum class LookupResultType
		{
			NotFound,
			Found,
			FoundOverloaded,
			Ambiguous
		};

		enum class AmbiguousType
		{
			// TODO
		};

		LookupResult(Sema& sema, Identifier::IdPtr id, SourceLocation loc, Sema::LookupNameType lookupNameType);

		Identifier::IdPtr GetLookupId() const noexcept
		{
			return m_LookupId;
		}

		Sema::LookupNameType GetLookupType() const noexcept
		{
			return m_LookupNameType;
		}

		NatsuLib::Linq<NatsuLib::natRefPointer<const Declaration::NamedDecl>> GetDecls() const noexcept;

		std::size_t GetDeclSize() const noexcept
		{
			return m_Decls.size();
		}

		nBool IsEmpty() const noexcept
		{
			return m_Decls.empty();
		}

		void AddDecl(NatsuLib::natRefPointer<Declaration::NamedDecl> decl);
		void AddDecl(NatsuLib::Linq<NatsuLib::natRefPointer<Declaration::NamedDecl>> decls);

		void ResolveResultType() noexcept;

		LookupResultType GetResultType() const noexcept
		{
			return m_Result;
		}

		AmbiguousType GetAmbiguousType() const noexcept
		{
			return m_AmbiguousType;
		}

		Type::TypePtr GetBaseObjectType() const noexcept
		{
			return m_BaseObjectType;
		}

		void SetBaseObjectType(Type::TypePtr value) noexcept
		{
			m_BaseObjectType = std::move(value);
		}

	private:
		// ���Ҳ���
		Sema& m_Sema;
		Identifier::IdPtr m_LookupId;
		SourceLocation m_LookupLoc;
		Sema::LookupNameType m_LookupNameType;
		Declaration::IdentifierNamespace m_IDNS;

		// ���ҽ��
		LookupResultType m_Result;
		AmbiguousType m_AmbiguousType;
		std::unordered_set<Declaration::DeclPtr> m_Decls;
		Type::TypePtr m_BaseObjectType;
	};
}