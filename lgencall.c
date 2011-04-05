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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <wchar.h>
#define LUA_LIB
#include "lua.h"
#include "llimits.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lgencall.h"

#define COMPILED_TABLE "GenericCall_CompiledFct"
#define MIN(a,b) ((a) < (b) ? (a) : (b))
typedef enum 
{
	BT_NUMBER,
	BT_INTEGER,
	BT_UNSIGNED,
	BT_BOOLEAN,
	BT_NIL,
	BT_STRING,
	BT_STRING_LIST,
	BT_LIGHT_POINTER,
	BT_FULL_POINTER,
	BT_THREAD,
	BT_FUNCTION,
	BT_CALLBACK,
	BT_STRUCTURE,
} eBasicType;

typedef enum 
{
	DT_BASIC_TYPE,
	DT_MEMORY_ALLOC,
	DT_CLOSE_STATE,
	DT_OPEN_LIBRARY,
	DT_GET_STATE,
	DT_CLEAR_CACHE,
	DT_COLLECT_GARBAGE,
} eDirectiveType;

typedef enum
{
	DIR_INPUT,
	DIR_OUTPUT
} eDirection;

typedef enum 
{
	MODE_USE_BUFFER,
	MODE_FROM_STACK,
	MODE_ALLOCATE
} eAllocateMode;

typedef enum
{
	WIDTH_FROM_FORMAT,
	WIDTH_FROM_ARGUMENT,
	WIDTH_TO_OUTPUT
} eWidthMode;

typedef enum 
{
	STATE_START,
	STATE_FLAGS,
	STATE_WIDTH,
	STATE_PRECISION,
	STATE_PREFIX,
	STATE_TYPE
} eParserState;

typedef struct
{
	va_list List;
} tVaList;

typedef struct
{
	unsigned int Width;
	void* Pointer;
	void* Pointer2;
	eBasicType Type            : 5;
	eDirectiveType EnvType     : 4;
	eDirection Direction       : 2;
	eAllocateMode AllocateMode : 3;
	eWidthMode WidthMode       : 3;
	eWidthMode PrecisionMode   : 3;
	int TypeModifier           : 3;
	unsigned int Precision     : 4;
	unsigned int ArgumentNb    : 9;
} tElement;

typedef struct 
{
	uint8_t TypeStart;
	uint8_t TypeEnd;
	uint8_t Bytes;
	int8_t Modifier;
} tTypeSize;

typedef struct
{
	lua_State* L;
	lua_Alloc AllocFct;
	void* AllocUd;
	tElement* Elements;
	int NbElements;
	uint8_t fWideChar   : 1;
	uint8_t fOpenState  : 1;
	uint8_t fCloseState : 1;
	uint8_t fNeedRestart: 1;
	uint8_t fRestarted  : 1;
} tEnvironment;

typedef struct 
{
	const char* Script;
	const char* Format;
	tVaList Marker;
	tEnvironment Environment;
} tGenericCallParamsA;

static const tTypeSize TypeSizes[] = 
{
	{ BT_NUMBER,  BT_NUMBER, sizeof(float),       0 },
	{ BT_NUMBER,  BT_NUMBER, sizeof(double),      1 },
#if LGENCALL_USE_LONG_DOUBLE
	{ BT_NUMBER,  BT_NUMBER, sizeof(long double), 2 },
#endif
	{ BT_INTEGER, BT_UNSIGNED, sizeof(int),         0 },
	{ BT_INTEGER, BT_UNSIGNED, sizeof(long),        1 },
	{ BT_INTEGER, BT_UNSIGNED, sizeof(short),      -1 },
	{ BT_INTEGER, BT_UNSIGNED, sizeof(char),       -2 },
#if LGENCALL_USE_64_BITS > 1
	{ BT_INTEGER, BT_UNSIGNED, sizeof(int64_t),       2 },
#elif LGENCALL_USE_64_BITS == 1
	{ BT_INTEGER, BT_INTEGER, sizeof(int64_t),     2 },
#endif

#ifdef __cplusplus
	{ BT_BOOLEAN, BT_BOOLEAN, sizeof(bool),        0 },
#elif __STDC_VERSION__+0 > 199900L
	{ BT_BOOLEAN, BT_BOOLEAN, sizeof(_Bool),       0 },
#else
	{ BT_BOOLEAN, BT_BOOLEAN, sizeof(int),         0 },
#endif
	{ BT_BOOLEAN, BT_BOOLEAN, sizeof(int),         1 },
	{ BT_BOOLEAN, BT_BOOLEAN, sizeof(char),       -1 },
	{ BT_STRING, BT_STRING_LIST, sizeof(char),        0 },
#if LGENCALL_USE_WIDESTRING
	{ BT_STRING, BT_STRING_LIST, sizeof(wchar_t),     1 },
	{ BT_STRING, BT_STRING_LIST, sizeof(wchar_t),     2 },
#endif
	{ BT_STRING, BT_STRING_LIST, sizeof(char),       -1 },
};

static void* MemoryAllocate(const tEnvironment* penv, size_t size)
{
	return (*penv->AllocFct)(penv->AllocUd, NULL, 0, size);
}

static const char* GetNextElement(const tEnvironment* penv, const char* format, 
								   tElement* element)
{
	eParserState state = STATE_START, laststate = STATE_FLAGS;
	for(;;)
	{
		char car = *format++;
		if(car == ' ' || car == '\t' || car == '\n' || car == '\r')
			continue; /* Allow spaces on format string, for clarity */
		switch(state)
		{
		case STATE_START:
			if(car == '%')
				state = STATE_FLAGS;
			else
				luaL_error(penv->L, "unexpected character %c", car);
			break;
		case STATE_FLAGS:
			switch(car)
			{
			case '#':
				element->AllocateMode = MODE_ALLOCATE;
				break;
			case '+':
				element->AllocateMode = MODE_FROM_STACK;
				break;
			default:
				state = STATE_WIDTH;
				break;
			}
			break;
		case STATE_WIDTH:
			if(car == '*')
				element->WidthMode = WIDTH_FROM_ARGUMENT;
			else if(car == '&')
				element->WidthMode = WIDTH_TO_OUTPUT;
			else if(isdigit((int)car))
				element->Width = element->Width * 10 + car - '0';
			else
				state = STATE_PRECISION;
			break;
		case STATE_PRECISION:
			if(car == '.')
				break;
			if(car == '*')
				element->PrecisionMode = WIDTH_FROM_ARGUMENT;
			else if(isdigit((int)car))
				element->Precision = element->Precision * 10 + car - '0';
			else
				state = STATE_PREFIX;
			break;
		case STATE_PREFIX:
			switch(car)
			{
			case 'L':
				element->TypeModifier += 2;
				break;
			case 'l':
				element->TypeModifier++;
				break;
			case 'h':
				element->TypeModifier--;
				break;
			default:
				state = STATE_TYPE;
				break;
			}
			break;
		case STATE_TYPE:
			switch(car)
			{
			case 'f':
				element->Type = BT_NUMBER;
				break;
			case 'd':
			case 'i':
				element->Type = BT_INTEGER;
				break;
			case 'u':
				element->Type = BT_UNSIGNED;
				break;
			case 'p':
				if(element->TypeModifier > 0)
					element->Type = BT_FULL_POINTER;
				else
					element->Type = BT_LIGHT_POINTER;
				break;
			case 's':
				element->Type = BT_STRING;
				if(element->TypeModifier == 0)
					element->TypeModifier = penv->fWideChar;
				break;
			case 'b':
				element->Type = BT_BOOLEAN;
				break;
			case 't':
				element->Type = BT_THREAD;
				break;
			case 'c':
				element->Type = BT_FUNCTION;
				break;
			case 'n':
				element->Type = BT_NIL;
				break;
			case 'k':
				element->Type = BT_CALLBACK;
				break;
			case 'r':
				element->Type = BT_STRUCTURE;
				break;
			case 'z':
				element->Type = BT_STRING_LIST;
				if(element->TypeModifier == 0)
					element->TypeModifier = penv->fWideChar;
				break;
			case 'M':
				element->EnvType = DT_MEMORY_ALLOC;
				break;
			case 'O':
				element->EnvType = DT_OPEN_LIBRARY;
				break;
			case 'S':
				element->EnvType = DT_GET_STATE;
				break;
			case 'C':
				element->EnvType = DT_CLOSE_STATE;
				break;
			case 'F':
				element->EnvType = DT_CLEAR_CACHE;
				break;
			case 'G':
				element->EnvType = DT_COLLECT_GARBAGE;
				break;
			case '%':
			case '>':
			case '<':
			case '\0':
				return format - 1;
			default:
				luaL_error(penv->L, "Invalid type character '%c' near '%s'", car, format);
					break;
			}
		}
		if(state != laststate)
			format--;
		laststate = state;
	}
}

#if LGENCALL_USE_WIDESTRING == 2

static void PushWideString(lua_State* L, const wchar_t* wstr, size_t len)
{
	luaL_Buffer b;
	size_t i;
	wchar_t thres;
	char str[8], car;
	luaL_buffinit(L, &b);
	if(len == 0)
		len = wcslen(wstr);
	for(i=0;i<len;i++)
	{
		char* pstr = str+sizeof(str);
		uint32_t value = wstr[i];
		*--pstr = 0;
		if(value < 0x80)
			*--pstr = (char)value;
		else
		{
			if((value & 0xFC00) == 0xD800) // UTF-16 surrogate pair
			{
				value = 0x10000 + ((value & 0x3FF) << 10);
				if(++i == len || ((wstr[i] & 0xFC00) != 0xDC00))
					break;
				value |= wstr[i] & 0x3FF;
			}
			for(thres=0x40;value>=thres;thres>>=1)
			{
				*--pstr = (char)((value & 0x3F) | 0x80);
				value >>= 6;
			}
			car = (unsigned)-1 << (8-sizeof(str)+pstr-str) | value;
			*--pstr = car;
		}
		luaL_addlstring(&b, pstr, str+sizeof(str)-1-pstr);
	}
	luaL_pushresult(&b);
}

static void LuaStringToWideString(lua_State* L, int idx)
{
	luaL_Buffer b;
	int i,mask;
	uint32_t value;
	char car;
	size_t len;
	wchar_t wc;
	const char* str = (const char*)luaL_checklstring(L, idx, &len);
	const char* strend = str + len;
	luaL_buffinit(L, &b);
	while(str < strend)
	{
		car = *str++;
		if((car & 0x80) == 0)
			value = car;
		else
		{
			for(i=1,mask=0x40;car & mask;i++,mask>>=1);
			value = car & (mask - 1);
			if(i == 1 || (value == 0 && (*str & 0x3F) < (0x100 >> i)))
				luaL_error(L, "overlong character in UTF-8");
			for(;i>1;i--)
			{
				car = *str++;
				if((car & 0xC0) != 0x80)
					luaL_error(L, "invalid UTF-8 string");
				value = (value << 6) | (car & 0x3F);
			}
		}
		// For UTF-16, generate surrogate pair outside BMP 
		if(sizeof(wchar_t) == 2 && value >= 0x10000)
		{
			value -= 0x10000;
			wc = (wchar_t)(0xD800 | (value >> 10));
			luaL_addlstring(&b, (const char*)&wc, sizeof(wc));
			wc = (wchar_t)(0xDC00 | (value & 0x3FF));
			luaL_addlstring(&b, (const char*)&wc, sizeof(wc));
		}
		else
		{
			wc = (wchar_t)value;
			luaL_addlstring(&b, (const char*)&wc, sizeof(wc));
		}
	}
	wc = 0;
	luaL_addlstring(&b, (const char*)&wc, sizeof(wc)-1);
	luaL_pushresult(&b);
	lua_replace(L, idx-(idx<0));
}

#elif LGENCALL_USE_WIDESTRING == 1

static void PushWideString(lua_State* L, const wchar_t* wstr, size_t len)
{
	size_t i;
	luaL_Buffer b;
	char buffer[10];
	if(len == 0)
		len = wcslen(wstr);
	luaL_buffinit(L, &b);
	for(i=0;i<len;i++)
	{
		int res = wctomb(buffer, wstr[i]);
		if(res == -1)
			luaL_error(L, "Error converting wide string to characters");
		luaL_addlstring(&b, buffer, res);
	}
	luaL_pushresult(&b);
}

static void LuaStringToWideString(lua_State* L, int idx)
{
	size_t i;
	luaL_Buffer b;
	wchar_t wchar;
	size_t len, pos = 0;
	const char* psrc = luaL_checklstring(L, idx, &len);
	luaL_buffinit(L, &b);
	for(i=0;i<len;i++)
	{
		int res = mbtowc(&wchar, psrc+pos, len-pos);
		if(res == -1)
			luaL_error(L, "Error converting character to wide string");
		if(res == 0)
		{
			res = 1;
			wchar = 0;
		}
		luaL_addlstring(&b, (const char*)&wchar, sizeof(wchar));
		pos += res;
	}
	wchar = 0;
	luaL_addlstring(&b, (const char*)&wchar, sizeof(wchar)-1);
	luaL_pushresult(&b);
	lua_replace(L, idx-(idx<0));
}

#endif

static void PushValueByPointer(lua_State* L, const void* ptr, tElement* pelem)
{
	lua_Number val = 0;
	luaL_checkstack(L, 1, NULL);
	switch(pelem->Type)
	{
	case BT_NUMBER:
		if(pelem->Precision == sizeof(float))
			lua_pushnumber(L, *(const float*)ptr);
		else if(pelem->Precision == sizeof(double))
			lua_pushnumber(L, *(const double*)ptr);
#if LGENCALL_USE_LONG_DOUBLE
		else
			lua_pushnumber(L, *(const long double*)ptr);
#endif
		lua_pushnumber(L, val);
		break;
	case BT_UNSIGNED:
		switch(pelem->Precision)
		{
		case 1: val = (lua_Number)*(const uint8_t *)ptr; break;
		case 2: val = (lua_Number)*(const uint16_t*)ptr; break;
		case 4: val = (lua_Number)*(const uint32_t*)ptr; break;
#if LGENCALL_USE_64_BITS > 1
		case 8: val = (lua_Number)*(const uint64_t*)ptr; break;
#endif
		default: luaL_error(L, "unknown unsigned precision %d", pelem->Precision); break;
		}
		lua_pushnumber(L, val);
		break;
	case BT_INTEGER:
	case BT_BOOLEAN:
		switch(pelem->Precision)
		{
		case 1: val = (lua_Number)*(const int8_t *)ptr; break;
		case 2: val = (lua_Number)*(const int16_t*)ptr; break;
		case 4: val = (lua_Number)*(const int32_t*)ptr; break;
#if LGENCALL_USE_64_BITS
		case 8: val = (lua_Number)*(const int64_t*)ptr; break;
#endif
		default: luaL_error(L, "unknown integer precision %d", pelem->Precision); break;
		}
		if(pelem->Type == BT_INTEGER)
			lua_pushnumber(L, val);
		else
			lua_pushboolean(L, (int)val);
		break;
	case BT_STRING_LIST:
	{
		int i;
		int target = 2 * pelem->Precision;
		size_t len;
		const char* psrc = *(const char**)ptr;
		if(pelem->Width == 0)
		{
			int cnt0 = 0;
			for(i=0;cnt0!=target;i++)
			{
				if(psrc[i])
					cnt0 = 0;
				else
					cnt0++;
			}
			pelem->Width = (i - 1) / pelem->Precision;
		}
		pelem->Type = BT_STRING;
		PushValueByPointer(L, ptr, pelem);
		pelem->Type = BT_STRING_LIST;
		psrc = lua_tolstring(L, -1, &len);
		lua_createtable(L, 0, 0);
		i = 0;
		while(len)
		{
			size_t s = strlen(psrc);
			lua_pushlstring(L, psrc, s);
			len -= s + 1;
			psrc += s + 1;
			lua_rawseti(L, -2, ++i);
		}
		lua_remove(L, -2);
		break;
	}
	case BT_STRING:
#if LGENCALL_USE_WIDESTRING
		if(pelem->Precision == sizeof(wchar_t))
			PushWideString(L, *(const wchar_t**)ptr, pelem->Width);
		else
#endif
		{
			if(pelem->Width)
				lua_pushlstring(L, *(const char**)ptr, pelem->Width);
			else
				lua_pushstring(L, *(const char**)ptr);
		}
		break;
	case BT_LIGHT_POINTER:
		lua_pushlightuserdata(L, *(void**)ptr);
		break;
	case BT_FULL_POINTER:
	{
		void* ud = lua_newuserdata(L, pelem->Precision);
		memcpy(ud, *(void**)ptr, pelem->Precision);
		break;
	}
	case BT_THREAD:
	{
		lua_State* L2 = *(lua_State**)ptr;
		lua_pushthread(L2);
		lua_xmove(L2, L, 1);
		break;
	}
	case BT_FUNCTION:
		/* FIXME: upvalues */
		lua_pushcclosure(L, *(lua_CFunction*)ptr, pelem->Precision);
		break;
	case BT_NIL:
		lua_pushnil(L);
		break;
	case BT_CALLBACK:
		(*(lgencall_pushCB)pelem->Pointer2)(L, ptr);
		break;
	case BT_STRUCTURE:
		break;
	}
}

static void PushValueByVARG(lua_State* L, tElement* pelem, tVaList* marker)
{
	lua_Number val;
	luaL_checkstack(L, 1, NULL);
	if(pelem->Width && (pelem->Type != BT_STRING && pelem->Type != BT_STRING_LIST))
	{
		int i;
		const uint8_t* pdata = va_arg(marker->List, const uint8_t*);
		int width = pelem->Width;
		pelem->Width = 0;
		lua_createtable(L, width, 0);
		for(i=0;i<width;i++)
		{
			PushValueByPointer(L, pdata, pelem);
			lua_rawseti(L, -2, i+1);
			pdata += pelem->Precision;
		}
		pelem->Width = width;
		return;
	}
	switch(pelem->Type)
	{
	case BT_NUMBER:
#if LGENCALL_USE_LONG_DOUBLE
		if(pelem->Precision == sizeof(long double))
			val = (lua_Number)va_arg(marker->List, long double);
		else
#endif
			val = (lua_Number)va_arg(marker->List, double);
		lua_pushnumber(L, val);
		break;
	case BT_INTEGER:
#if INT_MAX <= 2147483647 && LGENCALL_USE_64_BITS
		if(pelem->Precision == 8)
			val = (lua_Number)va_arg(marker->List, int64_t);
		else
#endif
			val = (lua_Number)va_arg(marker->List, int);
		lua_pushnumber(L, val);
		break;
	case BT_UNSIGNED:
#if UINT_MAX <= 4294967295u && LGENCALL_USE_64_BITS > 1
		if(pelem->Precision == 8)
			val = (lua_Number)va_arg(marker->List, uint64_t);
		else
#endif
			val = (lua_Number)va_arg(marker->List, unsigned int);
		lua_pushnumber(L, val);
		break;
	case BT_BOOLEAN:
		lua_pushboolean(L, va_arg(marker->List, int));
		break;
	case BT_NIL:
		lua_pushnil(L);
		break;
	case BT_STRING:
	case BT_STRING_LIST:
	case BT_LIGHT_POINTER:
	case BT_FULL_POINTER:
	case BT_THREAD:
	case BT_FUNCTION:
	case BT_CALLBACK:
	case BT_STRUCTURE:
	{
		const void* value = va_arg(marker->List, const void*);
		PushValueByPointer(L, &value, pelem);
		break;
	}
	}
}

static void LuaValueToPointer(const tEnvironment* penv, int idx, void* ptr, tElement* pelem)
{
	lua_Number val = 0;
	lua_State* L = penv->L;
	if(pelem->Width && (pelem->Type != BT_STRING && pelem->Type != BT_STRING_LIST))
	{
		int i, len;
		uint8_t* pdata = NULL;
		int width = pelem->Width;
		luaL_checktype(L, idx, LUA_TTABLE); 
		len = (int)lua_objlen(L, idx);
		switch(pelem->AllocateMode)
		{
		case MODE_USE_BUFFER:
			pdata = (uint8_t*)ptr;
			len = MIN(width, len);
			break;
		case MODE_FROM_STACK:
			pdata = (uint8_t*)lua_newuserdata(L, len * pelem->Precision);
			*(uint8_t**)ptr = pdata;
			break;
		case MODE_ALLOCATE:
			pdata = (uint8_t*)MemoryAllocate(penv, len * pelem->Precision);
			*(uint8_t**)ptr = pdata;
			break;
		}
		if(pelem->WidthMode == WIDTH_TO_OUTPUT)
		{
			*(unsigned*)pelem->Pointer2 = len;
			pelem->Width = len;
		}
		pelem->Width = 0;
		for(i=0;i<len;i++)
		{
			lua_rawgeti(L, idx, i+1);
			LuaValueToPointer(penv, -1, pdata, pelem);
			lua_pop(L, 1);
			pdata += pelem->Precision;
		}
		pelem->Width = width;
		return;
	}
	switch(pelem->Type)
	{
	case BT_NUMBER:
	case BT_INTEGER:
	case BT_UNSIGNED:
		val = luaL_checknumber(L, idx);
		break;
	case BT_BOOLEAN:
		luaL_checktype(L, idx, LUA_TBOOLEAN); 
		val = (lua_Number)lua_toboolean(L, idx);
	default:
		break;
	}
	switch(pelem->Type)
	{
	case BT_NUMBER:
		if(pelem->Precision == sizeof(float))
			*(float*)ptr = (float)val;
		else if(pelem->Precision == sizeof(double))
			*(double*)ptr = (double)val;
#if LGENCALL_USE_LONG_DOUBLE
		else if(pelem->Precision == sizeof(long double))
			*(long double*)ptr = (long double)val;
#endif
		else
			luaL_error(L, "unknown floating precision %d", pelem->Precision);
		break;
	case BT_UNSIGNED:
		switch(pelem->Precision)
		{
		case 1: *(uint8_t *)ptr = (uint8_t )val; break;
		case 2: *(uint16_t*)ptr = (uint16_t)val; break;
		case 4: *(uint32_t*)ptr = (uint32_t)val; break;
#if LGENCALL_USE_64_BITS >= 2
		case 8: *(uint64_t*)ptr = (uint64_t)val; break;
#endif
		}
		break;
	case BT_INTEGER:
	case BT_BOOLEAN:
		switch(pelem->Precision)
		{
		case 1: *(int8_t *)ptr = (int8_t )val; break;
		case 2: *(int16_t*)ptr = (int16_t)val; break;
		case 4: *(int32_t*)ptr = (int32_t)val; break;
#if LGENCALL_USE_64_BITS
		case 8: *(int64_t*)ptr = (int64_t)val; break;
#endif
		}
		break;
	case BT_NIL:
		break;
	case BT_STRING_LIST:
	{
		size_t i, len;
		luaL_Buffer b;
		luaL_checktype(L, idx, LUA_TTABLE);
		luaL_buffinit(L, &b);
		len = lua_objlen(L, idx);
		for(i=1;i<=len;i++)
		{
			lua_rawgeti(L, idx, (int)i);
			luaL_addvalue(&b);
			luaL_addchar(&b, 0);
		}
		luaL_pushresult(&b);
		lua_replace(L, idx);
		/* Deliberate fall back into BT_STRING */
	}
	case BT_STRING:
	{
		size_t len;
		const char* value;
#if LGENCALL_USE_WIDESTRING
		if(pelem->Precision == sizeof(wchar_t))
			LuaStringToWideString(L, idx);
#endif
		value = luaL_checklstring(L, idx, &len);
		if(pelem->WidthMode == WIDTH_TO_OUTPUT)
			*(unsigned*)pelem->Pointer2 = (unsigned)len / pelem->Precision;
		len++;
		switch(pelem->AllocateMode)
		{
		case MODE_USE_BUFFER:
			memcpy((char*)ptr, value, MIN(len, (size_t)(pelem->Width*pelem->Precision)));
			break;
		case MODE_FROM_STACK:
			*(const char**)ptr = value;
			break;
		case MODE_ALLOCATE:
			*(void**)ptr = MemoryAllocate(penv, len);
			memcpy(*(void**)ptr, value, len);
			break;
		}
		break;
	}
	case BT_LIGHT_POINTER:
		luaL_checktype(L, idx, LUA_TLIGHTUSERDATA); 
		*(const void**)ptr = lua_topointer(L, idx);
		break;
	case BT_FULL_POINTER:
		luaL_checktype(L, idx, LUA_TUSERDATA); 
		*(const void**)ptr = lua_topointer(L, idx);
		break;
	case BT_THREAD:
		luaL_checktype(L, idx, LUA_TTHREAD); 
		*(lua_State**)ptr = lua_tothread(L, idx);
		break;
	case BT_FUNCTION:
		luaL_checktype(L, idx, LUA_TFUNCTION); 
		*(lua_CFunction*)ptr = lua_tocfunction(L, idx);
		break;
	case BT_CALLBACK:
		(*(lgencall_getCB)pelem->Pointer2)(L, idx, ptr);
		break;
	case BT_STRUCTURE:
		break;
	}
}


static void CheckAndRetrieveWidth(lua_State* L, tElement* element, tVaList* marker)
{
	size_t i;
	if(element->WidthMode == WIDTH_FROM_ARGUMENT)
		element->Width = va_arg(marker->List, unsigned int);
	if(element->WidthMode == WIDTH_TO_OUTPUT)
	{
		if(element->Direction == DIR_INPUT)
			luaL_error(L, "argument #%d: '&' character only allowed for output parameter", element->ArgumentNb);
		element->Pointer2 = va_arg(marker->List, void*);
		if(element->AllocateMode == MODE_USE_BUFFER)
			element->Width = *(int*)element->Pointer2;
		else
			element->Width = 1;
	}
	if(element->AllocateMode != MODE_USE_BUFFER && element->Width == 0)
		element->Width = 1;
	if(element->Type == BT_CALLBACK)
		element->Pointer2 = va_arg(marker->List, void*);
	if(element->PrecisionMode == WIDTH_FROM_ARGUMENT)
		element->Precision = va_arg(marker->List, unsigned int);
	if(element->Precision == 0)
	{
		for(i=0;i<sizeof(TypeSizes)/sizeof(TypeSizes[0]);i++)
		{
			if(TypeSizes[i].TypeStart > element->Type || 
			   TypeSizes[i].TypeEnd < element->Type)
				continue;
			if(TypeSizes[i].Modifier == 0)
				element->Precision = TypeSizes[i].Bytes;
			if(TypeSizes[i].Modifier == element->TypeModifier)
			{
				element->Precision = TypeSizes[i].Bytes;
				return;
			}
		}
	}
}


void EnvironmentParameter(tEnvironment* penv, tElement* element, tVaList* marker)
{
	lua_State* L = penv->L;
	switch(element->EnvType)
	{
	case DT_BASIC_TYPE:
		luaL_error(L, "Only capital letters for global options");
		break;
	case DT_MEMORY_ALLOC:
		if(element->WidthMode == WIDTH_TO_OUTPUT)
		{
			*va_arg(marker->List, lua_Alloc*) = penv->AllocFct;
			if(element->AllocateMode == MODE_FROM_STACK)
				*va_arg(marker->List, void**) = penv->AllocUd;
		}
		else
		{
			penv->AllocFct = va_arg(marker->List, lua_Alloc);
			if(element->AllocateMode == MODE_FROM_STACK)
				penv->AllocUd = va_arg(marker->List, void*);
			if(penv->fOpenState && !penv->fRestarted)
				penv->fNeedRestart = 1;
		}
		break;
	case DT_CLOSE_STATE:
		penv->fCloseState = 1;
		break;
	case DT_OPEN_LIBRARY:
		luaL_openlibs(L);
		break;
	case DT_GET_STATE:
		*va_arg(marker->List, lua_State**) = L;
		penv->fCloseState = 0;
		break;
	case DT_CLEAR_CACHE:
		lua_pushnil(L);
		lua_setfield(L, LUA_REGISTRYINDEX, COMPILED_TABLE);
		break;
	case DT_COLLECT_GARBAGE:
		lua_gc(L, LUA_GCCOLLECT, 0);
		break;
	}
}

/* Function copied from lua.c */
static int traceback (lua_State *L) {
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}


static void genericcallA(tEnvironment* penv, const char* script, const char* format, tVaList* marker)
{
	tElement* element;
	int i;
	eDirection direction = DIR_INPUT;
	int nbparams[2] = {0,0};
	lua_State* L = penv->L;
	int idxbase, idxtrace;

	if(format == NULL)
		format = "";
	if(strchr(format, '<'))
	{
		tElement element;
		while(*format != '<')
		{
			memset(&element, 0, sizeof(tElement));
			format = GetNextElement(penv, format, &element);
			EnvironmentParameter(penv, &element, marker);
			if(penv->fNeedRestart)
				return;
		}
		format++;
	}
	if(script == NULL || *script == 0)
		return;
	for(i=0;format[i];i++)
		if(format[i] == '%')
			penv->NbElements++;
	penv->Elements = (tElement*)lua_newuserdata(L, penv->NbElements*sizeof(tElement));
	memset(penv->Elements, 0, penv->NbElements*sizeof(tElement));
	element = penv->Elements;

	lua_pushcfunction(L, traceback);
	idxtrace = lua_gettop(L);

	lua_getfield(L, LUA_REGISTRYINDEX, COMPILED_TABLE);
	if(!lua_istable(L, -1))
	{
		lua_createtable(L, 0, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, COMPILED_TABLE);
	}
	lua_getfield(L, -1, script);
	if(!lua_isfunction(L, -1))
	{
		if(luaL_loadstring(L, script))
			lua_error(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, -4, script);
	}
	idxbase = lua_gettop(L);

	for(;*format;)
	{
		if(*format == '>')
		{
			format++;
			direction = DIR_OUTPUT;
			continue;
		}
		if(element - penv->Elements >= penv->NbElements)
			luaL_error(L, "overlong format string");
		element->Direction = direction;
		element->ArgumentNb = ++nbparams[direction];
		format = GetNextElement(penv, format, element);
		CheckAndRetrieveWidth(L, element, marker);
		if(direction == DIR_INPUT)
			PushValueByVARG(L, element, marker);
		else if(element->Type != BT_NIL)
			element->Pointer = va_arg(marker->List, void*);
		element++;
	}


	if(lua_pcall(L, nbparams[0], nbparams[1], idxtrace))
		lua_error(L);
	for(i=0;i<nbparams[1];i++)
	{
		element = penv->Elements + nbparams[0] + i;
		LuaValueToPointer(penv, i+idxbase, element->Pointer, element);
	}
}

static void FillEnvironment(lua_State* L, tEnvironment* env)
{
	env->fRestarted = env->fNeedRestart;
	if(L == NULL)
	{
		if(env->fNeedRestart)
			lua_close(env->L);
		if(env->AllocFct)
			L = lua_newstate(env->AllocFct, env->AllocUd);
		else
			L = luaL_newstate();
		env->fOpenState = 1;
		env->fCloseState = 1;
	}
	env->fNeedRestart = 0;
	env->L = L;
	env->AllocFct = lua_getallocf(L, &env->AllocUd);
}

static char* GetErrorAndClose(tEnvironment* env, int errcode)
{
	char* res = NULL;
	if(errcode)
	{
		size_t len;
		const char* errtmp;
		errtmp = lua_tolstring(env->L, -1, &len);
		if(env->fCloseState)
		{
			res = (char*)MemoryAllocate(env, len+1);
			memcpy(res, errtmp, len+1);
		}
		else
			res = (char*)errtmp;
	}
	if(env->fCloseState)
		lua_close(env->L);
	return res;
}

LUALIB_API void lua_gencallA(lua_State* L, const char* script, const char* format, ...)
{
	tEnvironment env;
	tVaList marker;
	memset(&env, 0, sizeof(tEnvironment));
	do
	{
		FillEnvironment(L, &env);
		va_start(marker.List, format);
		lua_settop(env.L, 0);
		genericcallA(&env, script, format, &marker);
		va_end(marker.List);
	}
	while(env.fNeedRestart);
	GetErrorAndClose(&env, 0);
}

static int pgenericcallA(lua_State* L)
{
	tGenericCallParamsA* p = (tGenericCallParamsA*)lua_topointer(L, 1);
	lua_settop(L, 0);
	genericcallA(&p->Environment, p->Script, p->Format, &p->Marker);
	return 0;
}

LUALIB_API char* lua_genpcallA(lua_State* L, const char* script, const char* format, ...)
{
	tGenericCallParamsA p;
	int res;
	memset(&p.Environment, 0, sizeof(tEnvironment));
	do
	{
		FillEnvironment(L, &p.Environment);
		va_start(p.Marker.List, format);
		p.Script = script;
		p.Format = format;
		res = lua_cpcall(p.Environment.L, pgenericcallA, &p);
		va_end(p.Marker.List);
	}
	while(p.Environment.fNeedRestart);
	return GetErrorAndClose(&p.Environment, res);
}

#if LGENCALL_USE_WIDESTRING
typedef struct 
{
	const wchar_t* Script;
	const wchar_t* Format;
	tVaList Marker;
	tEnvironment Environment;
} tGenericCallParamsW;

static void genericcallW(tEnvironment* penv, const wchar_t* script, const wchar_t* format, tVaList* marker)
{
	lua_State* L = penv->L;
	lua_settop(L, 0);
	if(script == NULL)
		lua_pushstring(L, "");
	else
		PushWideString(L, script, 0);
	if(format == NULL)
		lua_pushstring(L, "");
	else
		PushWideString(L, format, 0);
	penv->fWideChar = 1;
	genericcallA(penv, lua_tostring(L, -2), lua_tostring(L, -1), marker);
}

LUALIB_API void lua_gencallW(lua_State* L, const wchar_t* script, const wchar_t* format, ...)
{
	tEnvironment env;
	tVaList marker;
	memset(&env, 0, sizeof(tEnvironment));
	do
	{
		FillEnvironment(L, &env);
		va_start(marker.List, format);
		genericcallW(&env, script, format, &marker);
		va_end(marker.List);
	}
	while(env.fNeedRestart);
	GetErrorAndClose(&env, 0);
}

static int pgenericcallW(lua_State* L)
{
	tGenericCallParamsW* p = (tGenericCallParamsW*)lua_topointer(L, 1);
	genericcallW(&p->Environment, p->Script, p->Format, &p->Marker);
	return 0;
}

LUALIB_API wchar_t* lua_genpcallW(lua_State* L, const wchar_t* script, const wchar_t* format, ...)
{
	tGenericCallParamsW p;
	int res;
	memset(&p.Environment, 0, sizeof(tEnvironment));
	do
	{
		FillEnvironment(L, &p.Environment);
		va_start(p.Marker.List, format);
		p.Script = script;
		p.Format = format;
		res = lua_cpcall(p.Environment.L, pgenericcallW, &p);
		va_end(p.Marker.List);
	}
	while(p.Environment.fNeedRestart);
	if(res)
		LuaStringToWideString(p.Environment.L, -1);
	return (wchar_t*)GetErrorAndClose(&p.Environment, res);
}
#endif
