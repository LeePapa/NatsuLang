#pragma once
#include <natRefObj.h>

namespace NatsuLang::Misc
{
	////////////////////////////////////////////////////////////////////////////////
	///	@brief	�ṩ��ID��ʵ���ı���ӳ��Ľӿڳ���
	///	@tparam	IDType	ID������
	////////////////////////////////////////////////////////////////////////////////
	template <typename IDType>
	struct TextProvider
		: NatsuLib::natRefObjImpl<TextProvider<IDType>>
	{
		virtual nString GetText(IDType id) = 0;
	};
}
