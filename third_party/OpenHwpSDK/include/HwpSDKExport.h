#pragma once
#ifndef HWP_SDK_EXPORT_H
#define HWP_SDK_EXPORT_H

#if defined(HWPSDKCPP_EXPORTS)
#	if defined(_MSC_VER)
#		define SDK_FE_API			__declspec(dllexport)
#	elif defined(__GNUC__)
#		define SDK_FE_API			__attribute__((__visibility__("default")))
#	else
#		define SDK_FE_API
#	endif	// defined(_MSC_VER)
#else	// defined(HWPSDKCPP_EXPORTS)
#	if defined(HWPSDK_STATIC)
#		define SDK_FE_API
#	elif defined(_MSC_VER)
#		define SDK_FE_API				__declspec(dllimport)
#		pragma comment(lib, "hwpsdk.lib")
#	else
#		define SDK_FE_API
#	endif	// defined(_MSC_VER)
#endif	// defined(HWPSDKCPP_EXPORTS)

#endif