/******************************************************************************
* Copyright (C) 2007 Olivetti Engineering SA, CH 1400 Yverdon-les-Bains.  
* Original author: Patrick Rapin. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/

#ifndef _LUA_GENCALL_H_
#define _LUA_GENCALL_H_

/* LGENCALL_USE_WIDESTRING defines the support for wide character (Unicode) strings:
   0 : no support
   1 : conversions between wchar_t* and char* use wctomb and mbtowc standard functions
   2 : conversions between wchar_t* and char* are done by the library, using UTF-8 format */
#ifndef LGENCALL_USE_WIDESTRING
#define LGENCALL_USE_WIDESTRING 2
#endif

/* LGENCALL_USE_64_BITS defines the support of 64 bits integers of the platform.
   0 : no support (8, 16 and 32 bits only)
   1 : signed 64 bits supported (int64_t)
   2 : signed and unsigned (int64_t and uint64_t)
   Microsoft Visual C++, for example, does not support unsigned 64 bits integers. */
#ifndef LGENCALL_USE_64_BITS
#define LGENCALL_USE_64_BITS 1
#endif

/* LGENCALL_USE_LONG_DOUBLE defines whether or not the type "long double" is supported.
   0 : no support. Only "float" and "double" arguments accepted.
   1 : accepts "long double" if format is "%Lf" 
   MSVC, for example, only supports 64 bits "long double", like a regular "double" */
#ifndef LGENCALL_USE_LONG_DOUBLE
#define LGENCALL_USE_LONG_DOUBLE 0
#endif


typedef void (*lgencall_pushCB)(lua_State* L, const void* ptr);
typedef void (*lgencall_getCB)(lua_State* L, int idx, void* ptr);

LUALIB_API void (lua_gencallA)(lua_State* L, const char* script, const char* format, ...);
LUALIB_API char* (lua_genpcallA)(lua_State* L, const char* script, const char* format, ...);

#if LGENCALL_USE_WIDESTRING
LUALIB_API void (lua_gencallW)(lua_State* L, const wchar_t* script, const wchar_t* format, ...);
LUALIB_API wchar_t* (lua_genpcallW)(lua_State* L, const wchar_t* script, const wchar_t* format, ...);
#endif

#if defined(_UNICODE) && LGENCALL_USE_WIDESTRING
#define lua_gencall     lua_gencallW
#define lua_genpcall    lua_genpcallW
#else
#define lua_gencall     lua_gencallA
#define lua_genpcall    lua_genpcallA
#endif

#endif
