﻿#include "AST/ASTContext.h"
#include "AST/Expression.h"
#include "Basic/Identifier.h"

#undef max

using namespace NatsuLib;
using namespace NatsuLang;

namespace
{
	constexpr std::size_t AlignTo(std::size_t size, std::size_t alignment) noexcept
	{
		return (size + alignment - 1) / alignment * alignment;
	}

	constexpr ASTContext::TypeInfo GetBuiltinTypeInfo(Type::BuiltinType::BuiltinClass type) noexcept
	{
		switch (type)
		{
		case Type::BuiltinType::Void:
			return { 0, 4 };
		case Type::BuiltinType::Bool:
			return { 1, 4 };
		case Type::BuiltinType::Char:
			return { 1, 4 };
		case Type::BuiltinType::UShort:
		case Type::BuiltinType::Short:
			return { 2, 4 };
		case Type::BuiltinType::UInt:
		case Type::BuiltinType::Int:
			return { 4, 4 };
		case Type::BuiltinType::ULong:
		case Type::BuiltinType::Long:
			return { 8, 8 };
		case Type::BuiltinType::ULongLong:
		case Type::BuiltinType::LongLong:
			return { 16, 16 };
		case Type::BuiltinType::UInt128:
		case Type::BuiltinType::Int128:
			return { 16, 16 };
		case Type::BuiltinType::Float:
			return { 4, 4 };
		case Type::BuiltinType::Double:
			return { 8, 8 };
		case Type::BuiltinType::LongDouble:
			return { 16, 16 };
		case Type::BuiltinType::Float128:
			return { 16, 16 };
		case Type::BuiltinType::Overload:
		case Type::BuiltinType::BoundMember:
		case Type::BuiltinType::BuiltinFn:
		default:
			return { 0, 0 };
		}
	}
}

std::optional<std::pair<std::size_t, std::size_t>> ASTContext::ClassLayout::GetFieldInfo(natRefPointer<Declaration::FieldDecl> const& field) const noexcept
{
	const auto iter = std::find_if(FieldOffsets.cbegin(), FieldOffsets.cend(), [&field](std::pair<natRefPointer<Declaration::FieldDecl>, std::size_t> const& pair)
	{
		return pair.first == field;
	});

	if (iter == FieldOffsets.cend())
	{
		return {};
	}

	return std::optional<std::pair<std::size_t, std::size_t>>{ std::in_place, std::distance(FieldOffsets.cbegin(), iter), iter->second };
}

ASTContext::ASTContext(TargetInfo targetInfo)
	: m_TargetInfo{ targetInfo }, m_TUDecl{ make_ref<Declaration::TranslationUnitDecl>(*this) }
{
}

ASTContext::~ASTContext()
{
}

natRefPointer<Type::BuiltinType> ASTContext::GetBuiltinType(Type::BuiltinType::BuiltinClass builtinClass)
{
	decltype(auto) ptr = m_BuiltinTypeMap[builtinClass];
	if (!ptr)
	{
		ptr = make_ref<Type::BuiltinType>(builtinClass);
	}

	return ptr;
}

natRefPointer<Type::BuiltinType> ASTContext::GetSizeType()
{
	if (m_SizeType)
	{
		return m_SizeType;
	}

	const auto ptrSize = m_TargetInfo.GetPointerSize(), ptrAlign = m_TargetInfo.GetPointerAlign();

	using BuiltinType = std::underlying_type_t<Type::BuiltinType::BuiltinClass>;

	for (auto i = static_cast<BuiltinType>(Type::BuiltinType::Invalid) + 1; i < static_cast<BuiltinType>(Type::BuiltinType::BuiltinClass::LastType); ++i)
	{
		const auto typeInfo = GetBuiltinTypeInfo(static_cast<Type::BuiltinType::BuiltinClass>(i));
		if (typeInfo.Size >= ptrSize && typeInfo.Align >= ptrAlign)
		{
			m_SizeType = GetBuiltinType(Type::BuiltinType::MakeUnsignedBuiltinClass(static_cast<Type::BuiltinType::BuiltinClass>(i)));
			return m_SizeType;
		}
	}

	assert(!"Not found");
	return nullptr;
}

natRefPointer<Type::BuiltinType> ASTContext::GetPtrDiffType()
{
	if (m_PtrDiffType)
	{
		return m_PtrDiffType;
	}

	const auto ptrSize = m_TargetInfo.GetPointerSize(), ptrAlign = m_TargetInfo.GetPointerAlign();

	using BuiltinType = std::underlying_type_t<Type::BuiltinType::BuiltinClass>;

	for (auto i = static_cast<BuiltinType>(Type::BuiltinType::Invalid) + 1; i < static_cast<BuiltinType>(Type::BuiltinType::BuiltinClass::LastType); ++i)
	{
		const auto typeInfo = GetBuiltinTypeInfo(static_cast<Type::BuiltinType::BuiltinClass>(i));
		if (typeInfo.Size >= ptrSize && typeInfo.Align >= ptrAlign)
		{
			m_PtrDiffType = GetBuiltinType(Type::BuiltinType::MakeSignedBuiltinClass(static_cast<Type::BuiltinType::BuiltinClass>(i)));
			return m_PtrDiffType;
		}
	}

	assert(!"Not found");
	return nullptr;
}

natRefPointer<Type::ArrayType> ASTContext::GetArrayType(Type::TypePtr elementType, std::size_t arraySize)
{
	// 能否省去此次构造？
	auto ret = make_ref<Type::ArrayType>(std::move(elementType), arraySize);
	const auto iter = m_ArrayTypes.find(ret);
	if (iter != m_ArrayTypes.end())
	{
		return *iter;
	}

	m_ArrayTypes.emplace(ret);
	return ret;
}

natRefPointer<Type::PointerType> ASTContext::GetPointerType(Type::TypePtr pointeeType)
{
	auto ret = make_ref<Type::PointerType>(std::move(pointeeType));
	const auto iter = m_PointerTypes.find(ret);
	if (iter != m_PointerTypes.end())
	{
		return *iter;
	}

	m_PointerTypes.emplace(ret);
	return ret;
}

natRefPointer<Type::FunctionType> ASTContext::GetFunctionType(Linq<Valued<Type::TypePtr>> const& params, Type::TypePtr retType, nBool hasVarArg)
{
	auto ret = make_ref<Type::FunctionType>(params, std::move(retType), hasVarArg);
	const auto iter = m_FunctionTypes.find(ret);
	if (iter != m_FunctionTypes.end())
	{
		return *iter;
	}

	m_FunctionTypes.emplace(ret);
	return ret;
}

natRefPointer<Type::ParenType> ASTContext::GetParenType(Type::TypePtr innerType)
{
	auto ret = make_ref<Type::ParenType>(std::move(innerType));
	const auto iter = m_ParenTypes.find(ret);
	if (iter != m_ParenTypes.end())
	{
		return *iter;
	}

	m_ParenTypes.emplace(ret);
	return ret;
}

natRefPointer<Type::AutoType> ASTContext::GetAutoType(Type::TypePtr deducedAsType)
{
	auto ret = make_ref<Type::AutoType>(std::move(deducedAsType));
	const auto iter = m_AutoTypes.find(ret);
	if (iter != m_AutoTypes.end())
	{
		return *iter;
	}

	m_AutoTypes.emplace(ret);
	return ret;
}

natRefPointer<Type::UnresolvedType> ASTContext::GetUnresolvedType(std::vector<Lex::Token>&& tokens)
{
	auto ret = make_ref<Type::UnresolvedType>(std::move(tokens));
	const auto iter = m_UnresolvedTypes.find(ret);
	if (iter != m_UnresolvedTypes.end())
	{
		return *iter;
	}

	m_UnresolvedTypes.emplace(ret);
	return ret;
}

natRefPointer<Declaration::TranslationUnitDecl> ASTContext::GetTranslationUnit() const noexcept
{
	return m_TUDecl;
}

ASTContext::TypeInfo ASTContext::GetTypeInfo(Type::TypePtr const& type)
{
	auto underlyingType = Type::Type::GetUnderlyingType(type);
	const auto iter = m_CachedTypeInfo.find(underlyingType);
	if (iter != m_CachedTypeInfo.cend())
	{
		return iter->second;
	}

	auto info = getTypeInfoImpl(underlyingType);
	m_CachedTypeInfo.emplace(std::move(underlyingType), info);
	return info;
}

// TODO: 根据编译目标的方案进行计算
ASTContext::ClassLayout const& ASTContext::GetClassLayout(natRefPointer<Declaration::ClassDecl> const& classDecl)
{
	assert(classDecl);

	const auto layoutIter = m_CachedClassLayout.find(classDecl);
	if (layoutIter != m_CachedClassLayout.end())
	{
		return layoutIter->second;
	}

	// 允许 0 大小对象将会允许对象具有相同的地址
	ClassLayout info{};
	for (auto const& field : classDecl->GetFields())
	{
		const auto fieldInfo = getTypeInfoImpl(field->GetValueType());
		info.Align = std::max(fieldInfo.Align, info.Align);
		const auto fieldOffset = AlignTo(info.Size, info.Align);
		if (fieldOffset != info.Size)
		{
			// 插入 padding
			info.FieldOffsets.emplace_back(nullptr, info.Size);
		}
		info.FieldOffsets.emplace_back(field, fieldOffset);
		info.Size = fieldOffset + fieldInfo.Size;
	}

	const auto ret = m_CachedClassLayout.emplace(classDecl, info);
	if (!ret.second)
	{
		// TODO: 报告错误
	}

	return ret.first->second;
}

// TODO: 使用编译目标的值
ASTContext::TypeInfo ASTContext::getTypeInfoImpl(Type::TypePtr const& type)
{
	switch (type->GetType())
	{
	case Type::Type::Builtin:
		return GetBuiltinTypeInfo(type.UnsafeCast<Type::BuiltinType>()->GetBuiltinClass());
	case Type::Type::Pointer:
		return { m_TargetInfo.GetPointerSize(), m_TargetInfo.GetPointerAlign() };
	case Type::Type::Array:
	{
		const auto arrayType = type.UnsafeCast<Type::ArrayType>();
		auto elemInfo = GetTypeInfo(arrayType->GetElementType());
		elemInfo.Size *= arrayType->GetSize();
		return elemInfo;
	}
	case Type::Type::Function:
		return { 0, 0 };
	case Type::Type::Class:
	{
		const auto classLayout = GetClassLayout(type.UnsafeCast<Type::ClassType>()->GetDecl());
		return { classLayout.Size * 8, classLayout.Align * 8 };
	}
	case Type::Type::Enum:
	{
		const auto enumType = type.UnsafeCast<Type::EnumType>();
		const auto enumDecl = enumType->GetDecl().Cast<Declaration::EnumDecl>();
		assert(enumDecl);
		return GetTypeInfo(enumDecl->GetUnderlyingType());
	}
	default:
		assert(!"Invalid type.");
		std::terminate();
	}
}
