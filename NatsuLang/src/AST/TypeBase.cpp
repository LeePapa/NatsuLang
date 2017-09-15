#include "AST/TypeBase.h"
#include "Basic/Token.h"
#include "Basic/Identifier.h"
#include "AST/Type.h"

using namespace NatsuLib;
using namespace NatsuLang;
using namespace NatsuLang::Type;

NatsuLang::Type::Type::~Type()
{
}

TypePtr NatsuLang::Type::Type::GetUnderlyingType(TypePtr const& type)
{
	if (!type)
	{
		return nullptr;
	}

	if (const auto deducedType = static_cast<natRefPointer<DeducedType>>(type))
	{
		return GetUnderlyingType(deducedType->GetDeducedAsType());
	}

	if (const auto typeofType = static_cast<natRefPointer<TypeOfType>>(type))
	{
		return GetUnderlyingType(typeofType->GetUnderlyingType());
	}

	if (const auto parenType = static_cast<natRefPointer<ParenType>>(type))
	{
		return GetUnderlyingType(parenType->GetInnerType());
	}

	return type;
}
