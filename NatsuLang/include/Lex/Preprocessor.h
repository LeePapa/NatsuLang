#pragma once
#include "Basic/Identifier.h"
#include "Basic/Diagnostic.h"
#include "Basic/SourceManager.h"
#include "Lexer.h"

namespace NatsuLang
{
	class Preprocessor
	{
	public:
		Preprocessor(Diag::DiagnosticsEngine& diag, SourceManager& sourceManager);
		~Preprocessor();

		NatsuLib::natRefPointer<Identifier::IdentifierInfo> FindIdentifierInfo(nStrView identifierName, Token::Token& token) const;

		Diag::DiagnosticsEngine& GetDiag() const noexcept
		{
			return m_Diag;
		}

		SourceManager& GetSourceManager() const noexcept
		{
			return m_SourceManager;
		}

		NatsuLib::natRefPointer<Lex::Lexer> GetLexer() const noexcept
		{
			return m_Lexer;
		}

		void SetLexer(NatsuLib::natRefPointer<Lex::Lexer> lexer) noexcept
		{
			m_Lexer = std::move(lexer);
		}

		nBool Lex(Token::Token& result) const
		{
			return m_Lexer->Lex(result);
		}

	private:
		mutable Identifier::IdentifierTable m_Table;
		Diag::DiagnosticsEngine& m_Diag;
		SourceManager& m_SourceManager;
		NatsuLib::natRefPointer<Lex::Lexer> m_Lexer;

		void init() const;
	};
}