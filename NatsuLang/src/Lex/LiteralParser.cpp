#include "Lex/LiteralParser.h"
#include "Basic/CharInfo.h"

using namespace NatsuLib;
using namespace NatsuLang::Lex;

namespace
{
	constexpr nuLong DigitValue(nStrView::CharType ch) noexcept
	{
		assert(NatsuLang::CharInfo::IsAlphanumeric(ch));

		if (NatsuLang::CharInfo::IsDigit(ch))
		{
			return static_cast<nuLong>(ch - nStrView::CharType{ '0' });
		}

		if (ch > 'a')
		{
			return static_cast<nuLong>(ch - nStrView::CharType{ 'a' } + 10);
		}

		if (ch > 'A')
		{
			return static_cast<nuLong>(ch - nStrView::CharType{ 'A' } + 10);
		}

		// ��������ַ����ǿ��õ������������ַ�
		return 0;
	}
}

NumericLiteralParser::NumericLiteralParser(nStrView buffer, SourceLocation loc, Diag::DiagnosticsEngine& diag)
	: m_Diag{ diag }, m_Buffer{ buffer }, m_Current{ m_Buffer.cbegin() }, m_DigitBegin{ m_Current }, m_SuffixBegin{}, m_SawPeriod{ false }, m_SawSuffix{ false }, m_Radix{ 10 }
{
	if (*m_Current == '0')
	{
		parseNumberStartingWithZero(loc);
	}
	else
	{
		m_Radix = 10;
		m_Current = skipDigits(m_Current);

		// TODO: ��ɶ�ȡָ����С������
	}

	// TODO: ��ȡ��׺
	m_SuffixBegin = m_Current;
	const auto end = m_Buffer.cend();
	for (; m_Current != end; ++m_Current)
	{
		switch (*m_Current)
		{
		case 'f':
		case 'F':
			m_IsFloat = true;
			// ������
			break;
		case 'u':
		case 'U':
			m_IsUnsigned = true;
			// �޷�����
			break;
		case 'l':
		case 'L':
			m_IsLong = true;
			// �������򳤸�����
			break;
		default:
			break;
		}
	}
}

nBool NumericLiteralParser::GetIntegerValue(nuLong& result) const noexcept
{
	for (auto i = m_DigitBegin; i != m_SuffixBegin; ++i)
	{
		result = result * m_Radix + DigitValue(*i);
	}

	// TODO: ����Ƿ����
	return false;
}

nBool NumericLiteralParser::GetFloatValue(nDouble& result) const noexcept
{
	nStrView::iterator periodPos{};
	nDouble partAfterPeriod{};
	for (auto i = m_DigitBegin; i != m_SuffixBegin; ++i)
	{
		if (!periodPos)
		{
			if (*i == '.')
			{
				periodPos = i;
				continue;
			}
			result = result * m_Radix + DigitValue(*i);
		}
		else
		{
			partAfterPeriod = partAfterPeriod * m_Radix + DigitValue(*i);
		}
	}

	if (periodPos)
	{
		result = partAfterPeriod * pow(m_Radix, periodPos - m_SuffixBegin);
	}
	
	// TODO: ����Ƿ����
	return false;
}

void NumericLiteralParser::parseNumberStartingWithZero(SourceLocation loc) noexcept
{
	assert(*m_Current == '0');
	++m_Current;

	const auto cur = *m_Current, next = *(m_Current + 1);
	if ((cur == 'x' || cur == 'X') && (CharInfo::IsHexDigit(next) || next == '.'))
	{
		++m_Current;
		m_Radix = 16;
		m_DigitBegin = m_Current;
		m_Current = skipHexDigits(m_Current);

		if (*m_Current == '.')
		{
			++m_Current;
			m_SawPeriod = true;
			m_Current = skipHexDigits(m_Current);
		}

		// TODO: ʮ�����Ƶĸ�����������

		return;
	}

	if ((cur == 'b' || cur == 'B') && (next == '0' || next == '1'))
	{
		++m_Current;
		m_Radix = 2;
		m_DigitBegin = m_Current;
		m_Current = skipBinaryDigits(m_Current);
		return;
	}

	// 0��ͷ����û�и�xXbB�ַ�����Ϊ�˽�����������
	m_Radix = 8;
	m_DigitBegin = m_Current;
	m_Current = skipOctalDigits(m_Current);

	// TODO: �˽��Ƶĸ�����������
}

nStrView::iterator NumericLiteralParser::skipHexDigits(nStrView::iterator cur) const noexcept
{
	const auto end = m_Buffer.end();
	while (cur != end && CharInfo::IsHexDigit(*cur))
	{
		++cur;
	}

	return cur;
}

nStrView::iterator NumericLiteralParser::skipOctalDigits(nStrView::iterator cur) const noexcept
{
	const auto end = m_Buffer.end();
	while (cur != end && (*cur >= '0' && *cur <= '7'))
	{
		++cur;
	}

	return cur;
}

nStrView::iterator NumericLiteralParser::skipDigits(nStrView::iterator cur) const noexcept
{
	const auto end = m_Buffer.end();
	while (cur != end && CharInfo::IsDigit(*cur))
	{
		++cur;
	}

	return cur;
}

nStrView::iterator NumericLiteralParser::skipBinaryDigits(nStrView::iterator cur) const noexcept
{
	const auto end = m_Buffer.end();
	while (cur != end && (*cur >= '0' && *cur <= '1'))
	{
		++cur;
	}

	return cur;
}

