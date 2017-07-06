#include "Lexer.h"
#include "CharInfo.h"
#include "Preprocessor.h"

using namespace NatsuLang::Lex;
using namespace NatsuLang::Token;
using namespace NatsuLang::CharInfo;

Lexer::Lexer(nStrView buffer, NatsuLang::Preprocessor& preprocessor)
	: m_Preprocessor{ preprocessor }, m_Buffer{ buffer }, m_Current{ m_Buffer.cbegin() }
{
	if (m_Buffer.empty())
	{
		nat_Throw(LexerException, "buffer is empty."_nv);
	}
}

nBool Lexer::Lex(NatsuLang::Token::Token& result)
{
NextToken:
	result.Reset();

	auto cur = m_Current;
	const auto end = m_Buffer.end();

	// �����հ��ַ�
	while (cur != end && IsWhitespace(*cur))
	{
		++cur;
	}

	if (cur == end)
	{
		return false;
	}

	const auto charCount = NatsuLib::StringEncodingTrait<nStrView::UsingStringType>::GetCharCount(*cur);
	if (charCount == 1)
	{
		switch (*cur)
		{
		case 0:
			result.SetType(TokenType::Eof);
			result.SetLength(0);
			return true;
		case '\n':
		case '\r':
		case ' ':
		case '\t':
		case '\f':
		case '\v':
			if (skipWhitespace(result, cur))
			{
				return true;
			}
			goto NextToken;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			return lexNumericLiteral(result, cur);
		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
		case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
		case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
		case 'V': case 'W': case 'X': case 'Y': case 'Z':
		case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
		case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
		case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
		case 'v': case 'w': case 'x': case 'y': case 'z':
		case '_':
			return lexIdentifier(result, cur);
		case '\'':
			return lexCharLiteral(result, cur);
		case '"':
			return lexStringLiteral(result, cur);
		case '?':
			result.SetType(TokenType::Question);
			break;
		case '[':
			result.SetType(TokenType::LeftSquare);
			break;
		case ']':
			result.SetType(TokenType::RightSquare);
			break;
		case '(':
			result.SetType(TokenType::LeftParen);
			break;
		case ')':
			result.SetType(TokenType::RightParen);
			break;
		case '{':
			result.SetType(TokenType::LeftBrace);
			break;
		case '}':
			result.SetType(TokenType::RightBrace);
			break;
		case '.':
			result.SetType(TokenType::Period);
			break;
		case '&':
		{
			// TODO: ���ܳ����ļ�β����ͬ
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '&':
				result.SetType(TokenType::AmpAmp);
				++cur;
				break;
			case '=':
				result.SetType(TokenType::AmpEqual);
				++cur;
				break;
			default:
				result.SetType(TokenType::Amp);
				break;
			}
		}
			break;
		case '*':
		{
			const auto nextChar = *(cur + 1);
			if (nextChar == '=')
			{
				result.SetType(TokenType::StarEqual);
				++cur;
			}
			else
			{
				result.SetType(TokenType::Star);
			}
		}
			break;
		case '+':
		{
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '+':
				result.SetType(TokenType::PlusPlus);
				++cur;
				break;
			case '=':
				result.SetType(TokenType::PlusEqual);
				++cur;
				break;
			default:
				result.SetType(TokenType::Plus);
				break;
			}
		}
			break;
		case '-':
		{
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '-':
				result.SetType(TokenType::MinusMinus);
				++cur;
				break;
			case '=':
				result.SetType(TokenType::MinusEqual);
				++cur;
				break;
			default:
				result.SetType(TokenType::Minus);
				break;
			}
		}
			break;
		case '~':
			result.SetType(TokenType::Tilde);
			break;
		case '!':
		{
			const auto nextChar = *(cur + 1);
			if (nextChar == '=')
			{
				result.SetType(TokenType::ExclaimEqual);
				++cur;
			}
			else
			{
				result.SetType(TokenType::Exclaim);
			}
		}
			break;
		case '/':
		{
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '/':
				if (skipLineComment(result, cur + 2))
				{
					return true;
				}
				goto NextToken;
			case '*':
				if (skipBlockComment(result, cur + 2))
				{
					return true;
				}
				goto NextToken;
			case '=':
				result.SetType(TokenType::SlashEqual);
				++cur;
				break;
			default:
				result.SetType(TokenType::Slash);
				break;
			}
		}
			break;
		case '%':
		{
			const auto nextChar = *(cur + 1);
			if (nextChar == '=')
			{
				result.SetType(TokenType::PercentEqual);
				++cur;
			}
			else
			{
				result.SetType(TokenType::Percent);
			}
		}
			break;
		case '<':
		{
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '<':
				if (*(cur + 2) == '=')
				{
					result.SetType(TokenType::LessLessEqual);
					cur += 2;
				}
				else
				{
					result.SetType(TokenType::LessLess);
					++cur;
				}
				break;
			case '=':
				result.SetType(TokenType::LessEqual);
				++cur;
				break;
			default:
				result.SetType(TokenType::Less);
				break;
			}
		}
			break;
		case '>':
		{
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '>':
				if (*(cur + 2) == '=')
				{
					result.SetType(TokenType::GreaterGreaterEqual);
					cur += 2;
				}
				else
				{
					result.SetType(TokenType::GreaterGreater);
					++cur;
				}
				break;
			case '=':
				result.SetType(TokenType::GreaterEqual);
				++cur;
				break;
			default:
				result.SetType(TokenType::Greater);
				break;
			}
		}
			break;
		case '^':
		{
			const auto nextChar = *(cur + 1);
			if (nextChar == '=')
			{
				result.SetType(TokenType::CaretEqual);
				++cur;
			}
			else
			{
				result.SetType(TokenType::Caret);
			}
		}
			break;
		case '|':
		{
			const auto nextChar = *(cur + 1);
			switch (nextChar)
			{
			case '=':
				result.SetType(TokenType::PipeEqual);
				++cur;
				break;
			case '|':
				result.SetType(TokenType::PipePipe);
				++cur;
				break;
			default:
				result.SetType(TokenType::Pipe);
				break;
			}
		}
			break;
		case ':':
			result.SetType(TokenType::Colon);
			break;
		case ';':
			result.SetType(TokenType::Semi);
			break;
		case '=':
		{
			const auto nextChar = *(cur + 1);
			if (nextChar == '=')
			{
				result.SetType(TokenType::EqualEqual);
				++cur;
			}
			else
			{
				result.SetType(TokenType::Equal);
			}
		}
			break;
		case ',':
			result.SetType(TokenType::Comma);
			break;
		case '#':
			result.SetType(TokenType::Hash);
			break;
		case '$':
			result.SetType(TokenType::Dollar);
			break;
		case '@':
			result.SetType(TokenType::At);
			break;
		default:
			result.SetType(TokenType::Unknown);
			break;
		}
	}
	else
	{
		// TODO: ???
	}
	
	cur += charCount;
	result.SetLength(static_cast<nuInt>(cur - m_Current));

	m_Current = cur;
	return false;
}

nBool Lexer::skipWhitespace(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto end = m_Buffer.end();

	while (cur != end && IsWhitespace(*cur))
	{
		++cur;
	}

	// TODO: ��¼����Ϣ

	m_Current = cur;
	return false;
}

nBool Lexer::skipLineComment(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto end = m_Buffer.end();

	while (cur != end && *cur != '\r' && *cur != '\n')
	{
		++cur;
	}

	m_Current = cur;
	return false;
}

nBool Lexer::skipBlockComment(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto end = m_Buffer.end();

	while (cur != end)
	{
		if (*cur == '*' && *(cur + 1) == '/')
		{
			cur += 2;
			break;
		}
		++cur;
	}

	m_Current = cur;
	return false;
}

nBool Lexer::lexNumericLiteral(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto start = cur, end = m_Buffer.end();
	CharType curChar = *cur, prevChar{};
	while (cur != end && IsNumericLiteralBody(curChar))
	{
		prevChar = curChar;
		curChar = *cur++;
	}

	// ��ѧ������������1E+10
	if ((curChar == '+' || curChar == '-') || (prevChar == 'e' || prevChar == 'E'))
	{
		return lexNumericLiteral(result, ++cur);
	}

	result.SetType(TokenType::NumericLiteral);
	result.SetLiteralContent({ start, cur });
	m_Current = cur;
	return true;
}

nBool Lexer::lexIdentifier(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto start = cur, end = m_Buffer.end();
	auto curChar = *cur++;

	while (cur != end && IsIdentifierBody(curChar))
	{
		curChar = *cur++;
	}

	m_Current = cur;

	auto info = m_Preprocessor.FindIdentifierInfo(nStrView{ start, cur }, result);
	// ����Ҫ��info���в�������Ϊ�Ѿ���FindIdentifierInfo�д������
	static_cast<void>(info);

	return true;
}

nBool Lexer::lexCharLiteral(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto start = ++cur, end = m_Buffer.end();
	const auto count = NatsuLib::StringEncodingTrait<nString::UsingStringType>::GetCharCount(*start);
	if (count < static_cast<std::size_t>(end - start))
	{
		const auto literalEnd = start + count;

		result.SetType(TokenType::CharLiteral);
		result.SetLiteralContent({ start, literalEnd });

		if (*literalEnd != '\'')
		{
			m_Preprocessor.GetDiag().Report(Diag::DiagnosticsEngine::DiagID::ErrMultiCharInLiteral);
		}

		cur = literalEnd;
		while (cur != end)
		{
			++cur;

			if (*cur == '\'')
			{
				++cur;
				break;
			}
		}

		m_Current = cur;
		return true;
	}
	
	m_Preprocessor.GetDiag().Report(Diag::DiagnosticsEngine::DiagID::ErrUnexpectEOF);
	m_Current = end;
	return false;
}

nBool Lexer::lexStringLiteral(NatsuLang::Token::Token& result, Iterator cur)
{
	const auto start = ++cur, end = m_Buffer.end();

	auto prevChar = *cur;
	while (cur != end)
	{
		if (*cur == '"' && prevChar != '\\')
		{
			break;
		}

		++cur;
	}

	if (cur == end)
	{
		m_Preprocessor.GetDiag().Report(Diag::DiagnosticsEngine::DiagID::ErrUnexpectEOF);
	}
	else
	{
		result.SetType(TokenType::StringLiteral);
		result.SetLiteralContent({ start, cur });
		++cur;
	}

	m_Current = cur;
	return true;
}
