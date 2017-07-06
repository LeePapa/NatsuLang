#include "Diagnostic.h"
#include "Identifier.h"
#include "CharInfo.h"

using namespace NatsuLib;
using namespace NatsuLang;

Diag::DiagnosticsEngine::DiagnosticsEngine(NatsuLib::natRefPointer<Misc::TextProvider<DiagID>> idMap, NatsuLib::natRefPointer<DiagnosticConsumer> consumer)
	: m_IDMap{ std::move(idMap) }, m_Consumer{ std::move(consumer) }, m_CurrentID{ DiagID::Invalid }, m_CurrentRequiredArgs{}
{
}

Diag::DiagnosticsEngine::~DiagnosticsEngine()
{
}

void Diag::DiagnosticsEngine::Clear() noexcept
{
	m_CurrentID = DiagID::Invalid;
	m_CurrentDiagDesc.Clear();
	m_CurrentRequiredArgs = 0;
	m_CurrentSourceLocation = {};
}

nBool Diag::DiagnosticsEngine::EmitDiag()
{
	if (m_Arguments.size() >= m_CurrentRequiredArgs)
	{
		m_Consumer->HandleDiagnostic(getDiagLevel(m_CurrentID), Diagnostic{ this, std::move(m_CurrentDiagDesc) });
		Clear();
		return true;
	}

	return false;
}

nString Diag::DiagnosticsEngine::convertArgumentToString(nuInt index) const
{
	const auto& arg = m_Arguments[index];
	switch (arg.first)
	{
	case ArgumentType::String:
		return std::get<0>(arg.second);
	case ArgumentType::Char:
		return nString{ std::get<1>(arg.second) };
	case ArgumentType::SInt:
		return nStrView{ std::to_string(std::get<2>(arg.second)).data() };
	case ArgumentType::UInt:
		return nStrView{ std::to_string(std::get<3>(arg.second)).data() };
	case ArgumentType::TokenType:
		return Token::GetTokenName(std::get<4>(arg.second));
	case ArgumentType::IdentifierInfo:
		return std::get<5>(arg.second)->GetName();
	default:
		return "(Broken argument)";
	}
}

const Diag::DiagnosticsEngine::DiagnosticBuilder& Diag::DiagnosticsEngine::DiagnosticBuilder::AddArgument(nString string) const
{
	m_Diags.m_Arguments.emplace_back(ArgumentType::String, Argument{ std::in_place_index<0>, std::move(string) });
	return *this;
}

const Diag::DiagnosticsEngine::DiagnosticBuilder& Diag::DiagnosticsEngine::DiagnosticBuilder::AddArgument(nChar Char) const
{
	m_Diags.m_Arguments.emplace_back(ArgumentType::Char, Argument{ std::in_place_index<1>, Char });
	return *this;
}

const Diag::DiagnosticsEngine::DiagnosticBuilder& Diag::DiagnosticsEngine::DiagnosticBuilder::AddArgument(nInt sInt) const
{
	m_Diags.m_Arguments.emplace_back(ArgumentType::SInt, Argument{ std::in_place_index<2>, sInt });
	return *this;
}

const Diag::DiagnosticsEngine::DiagnosticBuilder& Diag::DiagnosticsEngine::DiagnosticBuilder::AddArgument(nuInt uInt) const
{
	m_Diags.m_Arguments.emplace_back(ArgumentType::UInt, Argument{ std::in_place_index<3>, uInt });
	return *this;
}

const Diag::DiagnosticsEngine::DiagnosticBuilder& Diag::DiagnosticsEngine::DiagnosticBuilder::AddArgument(Token::TokenType tokenType) const
{
	m_Diags.m_Arguments.emplace_back(ArgumentType::TokenType, Argument{ std::in_place_index<4>, tokenType });
	return *this;
}

const Diag::DiagnosticsEngine::DiagnosticBuilder& Diag::DiagnosticsEngine::DiagnosticBuilder::AddArgument(NatsuLib::natRefPointer<Identifier::IdentifierInfo> identifierInfo) const
{
	m_Diags.m_Arguments.emplace_back(ArgumentType::TokenType, Argument{ std::in_place_index<5>, std::move(identifierInfo) });
	return *this;
}

nString Diag::DiagnosticsEngine::Diagnostic::GetDiagMessage() const
{
	assert(m_Diag->m_CurrentID != DiagID::Invalid && m_Diag->m_Arguments.size() >= m_Diag->m_CurrentRequiredArgs);

	nString result{};
	result.Reserve(m_StoredDiagMessage.size());

	auto pRead = m_StoredDiagMessage.begin();
	const auto pEnd = m_StoredDiagMessage.end();
	for (; pRead != pEnd;)
	{
		if (*pRead != '{')
		{
			result.Append(*pRead);
		}
		else
		{
			nuInt tmpIndex = 0;
			while (++pRead != pEnd && CharInfo::IsWhitespace(*pRead)) {}
			while (++pRead != pEnd && CharInfo::IsDigit(*pRead))
			{
				tmpIndex = tmpIndex * 10 + *pRead - '0';
			}
			while (++pRead != pEnd && CharInfo::IsWhitespace(*pRead)) {}
			if (pRead == pEnd || *pRead != '}')
			{
				nat_Throw(natErrException, NatErr_InvalidArg, "Expect '}'.");
			}
			assert(tmpIndex < m_Diag->m_Arguments.size());
			result.Append(m_Diag->convertArgumentToString(tmpIndex));
		}
		++pRead;
	}

	return result;
}

Diag::DiagnosticsEngine::DiagnosticBuilder Diag::DiagnosticsEngine::Report(DiagID id, SourceLocation sourceLocation)
{
	// ֮ǰ����ϻ�δ����
	if (m_CurrentID != DiagID::Invalid)
	{
		EmitDiag();
	}

	// Ϊ��ǿ�쳣��ȫ
	m_CurrentDiagDesc = m_IDMap->GetText(id);

	m_CurrentID = id;
	m_CurrentRequiredArgs = getDiagArgCount(id);
	m_CurrentSourceLocation = sourceLocation;

	return { *this };
}

void Diag::DiagnosticConsumer::BeginSourceFile(const Preprocessor* /*pp*/)
{
}

void Diag::DiagnosticConsumer::EndSourceFile()
{
}

void Diag::DiagnosticConsumer::Finish()
{
}
