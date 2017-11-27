﻿#include "CodeGen.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>

using namespace NatsuLib;
using namespace NatsuLang::Compiler;

void PrintException(natLog& logger, std::exception const& e);

void PrintException(natLog& logger, natException const& e)
{
	logger.LogErr(u8"捕获到来自函数 {0}({1}:{2})的异常，描述为 {3}"_nv, e.GetSource(), e.GetFile(), e.GetLine(), e.GetDesc());

#ifdef EnableStackWalker
	logger.LogErr(u8"调用栈为"_nv);

	const auto& stackWalker = e.GetStackWalker();

	for (std::size_t i = 0, count = stackWalker.GetFrameCount(); i < count; ++i)
	{
		const auto& symbol = stackWalker.GetSymbol(i);
#ifdef _WIN32
		logger.LogErr(u8"{3}: (0x%p) {4} (地址：0x%p) (文件 {5}:{6} (地址：0x%p))"_nv, symbol.OriginalAddress, reinterpret_cast<const void*>(symbol.SymbolAddress), reinterpret_cast<const void*>(symbol.SourceFileAddress), i, symbol.SymbolName, symbol.SourceFileName, symbol.SourceFileLine);
#else
		logger.LogErr(u8"0x%p : {1}"_nv, symbol.OriginalAddress, symbol.SymbolInfo);
#endif
	}
#endif

	try
	{
		std::rethrow_if_nested(e);
	}
	catch (natException& inner)
	{
		logger.LogErr(u8"由以下异常引起："_nv);
		PrintException(logger, inner);
	}
	catch (std::exception& inner)
	{
		logger.LogErr(u8"由以下异常引起："_nv);
		PrintException(logger, inner);
	}
}

void PrintException(natLog& logger, std::exception const& e)
{
	logger.LogErr(u8"捕获到异常，描述为 {0}"_nv, e.what());

	try
	{
		std::rethrow_if_nested(e);
	}
	catch (natException& inner)
	{
		logger.LogErr(u8"由以下异常引起："_nv);
		PrintException(logger, inner);
	}
	catch (std::exception& inner)
	{
		logger.LogErr(u8"由以下异常引起："_nv);
		PrintException(logger, inner);
	}
}

int main(int argc, char* argv[])
{
	natConsole console;
	natEventBus event;
	natLog logger{ event };
	logger.UseDefaultAction(console);

	try
	{
		if (argc == 2)
		{
			AotCompiler compiler{ make_ref<natStreamReader<nStrView::UsingStringType>>(make_ref<natFileStream>("DiagIdMap.txt", true, false)), logger };

			Uri uri{ argv[1] };

			std::string outputName{ uri.GetPath().cbegin(), uri.GetPath().cend() };
			outputName += ".obj";

			std::error_code ec;
			llvm::raw_fd_ostream output{ outputName, ec, llvm::sys::fs::F_None };

			if (ec)
			{
				logger.LogErr(u8"目标文件无法打开，错误为：{0}"_nv, ec.message());
				return EXIT_FAILURE;
			}

			compiler.Compile(uri, output);
		}
		else
		{
			// TODO
		}

		console.ReadLine();
	}
	catch (natException& e)
	{
		PrintException(logger, e);
		logger.LogErr(u8"编译器由于未处理的不可恢复的异常而中止运行，请按 Enter 退出程序");
		console.ReadLine();
	}
	catch (std::exception& e)
	{
		PrintException(logger, e);
		logger.LogErr(u8"编译器由于未处理的不可恢复的异常而中止运行，请按 Enter 退出程序");
		console.ReadLine();
	}
}
