//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Character code conversion
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "CharacterSet.h"
//---------------------------------------------------------------------------
#include <limits>
#include <string>

static tjs_int TVPConversionLength(size_t length)
{
    if (length == static_cast<size_t>(-1) || length > static_cast<size_t>(std::numeric_limits<tjs_int>::max())) return -1;
    return static_cast<tjs_int>(length);
}

tjs_int TVPWideCharToUtf8String(const tjs_char *input, char *output)
{
    auto length = TJS_wcstombs(nullptr, input, 0);
    if (!output || length == static_cast<size_t>(-1)) return TVPConversionLength(length);
    return TVPConversionLength(TJS_wcstombs(output, input, length));
}

tjs_int TVPUtf8ToWideCharString(const char *input, tjs_char *output)
{
    auto length = TJS_mbstowcs(nullptr, input, 0);
    if (!output || length == static_cast<size_t>(-1)) return TVPConversionLength(length);
    return TVPConversionLength(TJS_mbstowcs(output, input, length));
}

tjs_int TVPUtf8ToWideCharString(const char *input, tjs_uint length, tjs_char *output)
{
    if (!input) return -1;
    std::string bounded(input, length);
    return TVPUtf8ToWideCharString(bounded.c_str(), output);
}
