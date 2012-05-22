/* Test functions for lgencall.c.
   Uses the same functions as the documentation examples,
   with the difference that it supports Unicode and ANSI compilation.
   The header file <tchar.h>, found only on Windows systems, consists of
   macro definitions like _T(string), to support both compilation modes. */
   
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <tchar.h>
#include <stdio.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lgencall.h"
}

static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}

static void test_in_numbers(lua_State* L)
{
	lua_genpcall(L, _T("for k,v in pairs{...} do print(k, type(v), v) end"), 
		_T("%i%d%u%f%f"), -4, 0xFFFFFFFF, 0xFFFFFFFF, 3.1415926535f, 3.1415926535);
}

static void test_in_other_scalars(lua_State* L)
{
	lua_genpcall(L, _T("for k,v in pairs{...} do print(k, type(v), v) end"), 
		_T("%b%b%n%s%t%p"), 0, 1, _T("Hello"), L, L);
}

static int cFunction(lua_State* L)
{
	printf("%s\n", luaL_checkstring(L, 1));
	return 1;
}
static void pushMessage(lua_State* L, const void* ptr)
{
	lua_pushinteger(L, *(int*)ptr);
}
static void test_in_function_callback(lua_State* L)
{
	lua_genpcall(L, _T("local fct, msg = ...; fct(msg)"), 
		_T("%c%k"), cFunction, pushMessage, 2);
}

static void test_in_arrays(lua_State* L)
{
	short array[] = { 1,2,3 };
	lua_genpcall(L, 
		_T("for k,v in pairs{...} do print(k, #v, table.concat(v, ', ')) end"), 
		_T("%2hd%5.1u%*.*d"), array, "Hello", 
		sizeof(array)/sizeof(array[0]), sizeof(array[0]), array);
}

static void test_in_strings(lua_State* L)
{
	unsigned char data[] = { 200, 100, 0, 3, 5, 0 };
	lua_genpcall(L, _T("for k,v in pairs{...} do print(k, v:gsub('.', ")
		_T("function(c) return '\\\\'..c:byte() end)) end"), 
		_T("%hs%6s%*.1s%ls"), "Hello", _T("P1\0P2"), sizeof(data), data, L"été");
}

static void test_in_string_lists(lua_State* L)
{
	lua_genpcall(L, 
		_T("for k,v in pairs{...} do print(k, #v, table.concat(v, ',')) end"),
		_T("%z  %7z %hz %*lz"), _T("s1\0s2\0s3\0"), _T("s4\0\0s5\0"), 
		"c1\0c2\0c3\0", 7, L"w1\0\0w2\0"
	);
}

static void test_out_numbers(lua_State* L)
{
	char var1; unsigned short var2; int var3;
	float var4; double var5;
	lua_genpcall(L, _T("return 1, 2, 3, 4, 5"), _T(">%hhd%hu%d%f%lf"), 
		&var1, &var2, &var3, &var4, &var5);
	printf("%d %u %d %f %f\n", var1, var2, var3, var4, var5);
}

static void test_out_other_scalars(lua_State* L)
{
	bool bool1; int bool2; 
	const char* str; void* ptr;
	lua_genpcall(L, _T("return true, false, 'dummy', 'Hello', io.stdin"), 
		_T(">%b%b%n%+hs%p"), &bool1, &bool2, &str, &ptr);
	printf("%hd %ld %s %p\n", bool1, bool2, str, ptr);
}

static void getMessage(lua_State* L, int idx, void* ptr)
{
	*(const char**)ptr = lua_tostring(L, idx);
}
static void test_out_function_callback(lua_State* L)
{
	lua_CFunction fct;
	const char* msg;
	lua_genpcall(L, _T("return print, 'Hello World!'"), 
		_T(">%c%k"), &fct, getMessage, &msg);
	lua_pushstring(L, msg);
	fct(L);
}

static void test_out_arrays(lua_State* L)
{
	unsigned int int_a[3];
	bool bool_a[4];
	char* str; 
	short* pshort;
	int short_len;
	int bool_len = sizeof(bool_a)/sizeof(bool_a[0]);
	lua_genpcall(L, _T("return {1,2,3,4},{72,101,108,108,111,0}, {5,6,7}, {false,true}"), 
		_T(">%3u%+.1d%#&hd%&.*b"), &int_a, &str, &short_len, &pshort, 
		&bool_len, sizeof(bool_a[0]), &bool_a);
	printf("int_a = {%u,%u,%u}\nstr = %s\npshort[%d]=%d\nbool_a = #%d:{%d,%d,%d,%d}\n", 
		int_a[0], int_a[1], int_a[2], str, short_len-1, pshort[short_len-1],
		bool_len, bool_a[0], bool_a[1], bool_a[2], bool_a[3]);
	free(pshort);
}

static void test_out_strings(lua_State* L)
{
	const TCHAR *str1;
	TCHAR *str2;
	TCHAR str3[10];
	unsigned char data[6];
	int len = sizeof(data);
	wchar_t* wstr;
	lua_genpcall(L, _T("return 'Hello', ' Wor', 'ld!', '\\0\\5\\200\\0', 'Unicode'"),
		_T(">%+s%#s%*s%&hs%+ls"), &str1, &str2, sizeof(str3), str3, &len, data, &wstr);
	_tprintf(_T("%s%s%s\ndata (%d bytes): %02X %02X %02X %02X %02X\n"), 
		str1, str2, str3, len, data[0],data[1],data[2],data[3],data[4]);
	printf("wstr = %S\n", wstr);
	free(str2);
}

static void print_string_list(const char* title, const void* data, int fchar)
{
	printf("%-4s = {", title);
	if(fchar)
	{
		const char* str = (const char*)data;
		while(*str)
		{
			printf("'%s', ", str);
			str += strlen(str) + 1;
		}
	}
	else
	{
		const wchar_t* str = (const wchar_t*)data;
		while(*str)
		{
			printf("'%S', ", str);
			str += wcslen(str) + 1;
		}
	}
	printf("}\n");
}
static void test_out_string_lists(lua_State* L)
{
	const char *str1;
	TCHAR *str2;
	TCHAR str3[10];
	int len;
	wchar_t* wstr;
	lua_genpcall(L, _T("return {1,2,3},{4,5,6},{10,9,8,7},{11,12}"),
		_T(">%+hz %+&z %*z %#lz"), &str1, &len, &str2, 
		sizeof(str3)/sizeof(str3[0]), &str3, &wstr );
	print_string_list("str1", str1, 1);
	print_string_list("str2", str2, sizeof(TCHAR)==1);
	printf("len = %d\n", len);
	print_string_list("str3", str3, sizeof(TCHAR)==1);
	print_string_list("wstr", wstr, 0);
	free(wstr);
}

static void test_null_parameters(lua_State* L)
{
	lua_gencallA(NULL, NULL, NULL);
	lua_genpcallA(NULL, NULL, NULL);
	lua_gencallW(NULL, NULL, NULL);
	lua_genpcallW(NULL, NULL, NULL);
}

static void test_format_errors(lua_State* L)
{
	_tprintf(_T("%s\n"), lua_genpcall(L, _T("print 'hello'"), _T("%O u<%d>n'importe  quoi%d")));
}

int main(int argc, char* argv[])
{
	lua_State* L = NULL;

	L = lua_open();
	luaL_openlibs(L);
	
	test_in_numbers(L);
	test_in_other_scalars(L);
	test_in_function_callback(L);
	test_in_arrays(L);
	test_in_strings(L);
	test_in_string_lists(L);

	test_out_numbers(L);
	test_out_other_scalars(L);
	test_out_function_callback(L);
	test_out_arrays(L);
	test_out_strings(L);
	test_out_string_lists(L);

	test_null_parameters(L);
	test_format_errors(L);

	lua_close(L);
	return 0;
}

