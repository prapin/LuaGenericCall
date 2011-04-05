Lua Generic Call
================

Introduction
------------

Using standard Lua API, calling a chunk function while passing input parameters and retrieving output results requires a lot of code and complete knowledge about Lua stack.
As an example, here is how to ask Lua to perform the multiplication of 3 by 2.5, checking for possible syntax and runtime errors:

	const char* errmsg = NULL;
	double result;
	lua_settop(L, 0);
	if(luaL_loadstring(L, "local a,b = ...; return a*b"))
	  errmsg = lua_tostring(L, -1);
	else
	{
	  lua_pushinteger(L, 3);
	  lua_pushnumber(L, 2.5);
	  if(lua_pcall(L, 2, 1, 0))
	    errmsg = lua_tostring(L, -1);
	  else
	    result = lua_tonumber(L, -1);
	}

The generic call feature is primarily aimed to simplify such calling tasks. It allows replacing all the previous code with a single function call:

	double result;
	const char* errmsg = lua_genpcall(L, "local a,b = ...; return a*b",
	  "%d %f > %lf", 3, 2.5, &result);

The second goal is to minimize, ideally to only one, the number of functions needed in the API. This can for example simplify dynamic loading of Lua shared library, because for each exported function, you need to define a new type for the prototype of the function, instantiate a variable of this type and make a call to `GetProcAddress` or `dlsym`. However, for this job, it may be preferable to follow EasyManualLibraryLoad guideline.


Variants
--------
Four versions of the general call function are in fact defined, but normally you only use one of them:

* __`lua_gencallA`__ is the base function. In case of an error (compile time, run time or coming from parameters), 
  it calls `luaL_error` that throws an exception (or makes a `longjmp`, which is similar). 
  It is the responsibility of the caller to catch this error, by using `lua_cpcall`, or by setting `lua_atpanic`. 
* __`lua_genpcallA`__ is the protected version of the same function. It calls `lua_gencallA`, enclosed with `lua_cpcall`. 
  In case of errors, the exception is trapped and the error message is output as the function result. 
  Otherwise, the function returns `NULL`.
* __`lua_gencallW`__ is the Unicode, or wide character, version of `lua_gencallA`. Its both string arguments (script chunk and format) must be of type __`wchar_t*`__. It will convert these into their __`char*`__ counterpart and call `lua_gencallA`. The format string always contains ASCII characters, so the transformation is straightforward. The script string may contain arbitrary characters; most typically file names and user messages in localized languages. Two conversion methods are supported by the function, selected at compile time. Either using `wctomb` and `mbtowc` (system functions based on current locale) or converting to UTF-8 with custom code.
* __`lua_genpcallW`__ is the protected version of `lua_gencallW`. In case of errors, the message string is converted back to wide characters and returned.

As with all include files under Windows OS, a compilation switch automatically redirects `lua_gencall` to either `lua_gencallA` or `lua_gencallW` depending whether `UNICODE` is defined or not. It is also possible to compile the library file without wide character support.

General form
------------
Each of these functions has a variable number of arguments, and three fixed ones.

1. __lua_State* L__: a pointer to an instance of a Lua state. If the pointer is `NULL`, the function automatically calls `lua_newstate` to create a new instance, and will also call `lua_close` to release its memory, unless the pointer to the instance is retrieved with __%S__ option (see after). If the state is freed and an error occurred before, the function allocates a buffer to copy the error message, which must be closed with `free`.
2. __script string__: it contains some piece of Lua code to execute. It typically begins with parameter retrieval: {{local var1, var2, var3 = ...;}} and ends with returning results: {{return res1, res2, res2}}. If the pointer is `NULL`, it is equivalent to the empty script "".
3. __format string__: a string similar to the `printf` or `scanf` format strings, using the __%__ character to describe the variable types of input and output values. If the pointer is `NULL`, it is equivalent to the empty format "".
4. __zero or more value parameters.__ Input parameters are passed by value, while output results must be retrieved by passing addresses of variables. Allocation options may also change the expected types of variables.

For performance reasons, there is a cache of already compiled chunks. It is implemented as a Lua table in the Lua registry, indexed by chunk strings. So if you call several times `lua_genpcall` with the same script string, it is compiled only the first time. All successive calls will reuse the cached version. The cache table is _not_ a weak table, to avoid having to recompile several times the same script, because of garbage collection going on. A drawback if that the cache may grow indefinitely when the script chunks can change arbitrary at runtime. This can for example happen on a server interpreter executing Lua chunks coming from a client program. But you can explicitly clear the cache by specifying it on the format string. 

As with `printf` and even more with `scanf`, you must be very careful with the types of the arguments and the corresponding format specifications. Any mismatch can lead to unexpected results, or even worse, to an application crash. 

The general form of the format string is this one:
	
	"[ Directives < ] Inputs [ > Outputs ]"

In this general form, `Directives`, `Inputs` and `Outputs` are strings similar to `printf` and `scanf` formats, consisting in format items beginning with a percent sign. `Directives` and `Outputs` are optional parts. If they are present, there must be separated from `Inputs` with a __<__ or a __>__ character. `Inputs` can also be an empty string.

Input and output format strings
-------------------------------

As with standard `printf` format specifications, each input or output format item consists of up to 6 fields, as in the next example. Directive items have fewer options and are explained later. Any blank character (space, tabulation, carriage return and line feed) is ignored and may be used to increase clarity in the format string. Other characters are either interpreted as explained in this chapter, or throw a Lua error when invalid.

	%#12.4Ls

1. A mandatory percent sign (__'%'__)
2. An optional flags argument (__'#'__)
3. An optional width argument (__'12'__)
4. An optional precision argument (__'.4'__)
5. An optional size modifier (__'L'__)
6. A mandatory conversion character (__'s'__)

The __flag__ argument may be one of these characters:

* __'#'__: the output string or array will be allocated by calling the Lua allocating function (the one passed to `lua_newstate`, which is by default implemented by calling standard `realloc` and `free` functions). You will need to `free` it after use.
* __'+'__: the output string or array will be allocated on Lua stack. You must use it or copy it to another buffer before the next call to Lua API, since the garbage collector may free the area at any moment during Lua execution.
* __(none)__: the output string or array buffer is allocated by the caller and passed to the generic call, which fills it up to its allocated size.

The __width__ parameter is used with strings, string lists and arrays. It represents the number of elements or characters of the memory buffer. It can be one of the following forms:
the following forms:

* __'[0-9]+'__: A number in ASCII representation. 
* __'*'__: the width is passed as an additional argument of type __`int`__ placed _before_ the value itself. 
* __'&'__: the actual width of the output array or string is returned through an additional argument of type __`int*`__ placed _before_ the value. If the flag argument is __(none)__, the variable must be initialized to the allocated size before the call.
* __(none)__: For all types except strings, it denotes scalar values. For input strings, it indicates a zero terminated string, which length will be determined by `strlen`. For a string list, it means the list end up after two consecutive zero characters.

The __precision__ argument is used with numerical types to indicate the size in bytes of the C type. It is important for numerical arrays, and for output values. This is because in both cases, a pointer to a variable and not the value itself it passed. It has one of these forms:

* __'.[0-9]+'__: a dot sign followed by a number in ASCII representation. 
* __'.*'__: the precision is passed as an additional argument of type __`int`__ placed _before_ the value itself (but after an optional __'*'__ width argument). 
* __(none)__: the precision is the size of the default type for the conversion character, see table below.

The __size modifier__ argument is an alternative to the numerical precision value, and changes the expected C type of the value. It can be one of these characters. Please refer to size table below for correspondences:

* __'h'__: the type is shorter as the default
* __'hh'__: the type if the shortest one
* __'l'__ (small ell): the type is larger as the default one
* __'L'__ (big ell): the type is the largest one
* __(none)__: the type is the default one

Most importantly, the __conversion character__ specifies the type of data. It must be one of the following characters:

* __'f'__: floating point number
* __'d'__ or __'i'__: a signed integer value
* __'u'__: an unsigned integer value
* __'p'__: a pointer value mapped to a light userdatum
* __'s'__: a string. Strings may contain arbitrary data, including embedded zeros.
* __'z'__: a zero terminated list of zero terminated strings. This is a C string mapped to a Lua array of strings, where each string element is separated from the others with a zero character. Windows uses such string lists for example in the lpstrFilter member of OPENFILENAME structure.
* __'b'__: a Boolean value
* __'t'__: a pointer to a thread (coroutine)
* __'c'__: a pointer to a C function (or closure)
* __'n'__: __`nil`__
* __'k'__: a pointer to a C callback function, for user specific data

For numerical values, here are the default and modified underlying C types listed in the following table:

	         (default)               'hh'       'h'       'l'           'L'
	                                                                   
	'f'      float                   ---        float     double        long double (*)
	                                                                   
	'd','i'  int                     char       short     long          int64_t (*)
	                                                                   
	'u'      unsigned                unsigned   unsigned  unsigned      uint64_t (*)
	         int                     char       short     long         
	                                                                   
	'b'      C89: int                ---        char      int           ---  
	         C99: _Bool                                                
	         C++: bool                                                 
	                                                                   
	's','z'  gencallA: char*                                           
	         gencallW: wchar_t* (*)  ---        char*     wchar_t* (*)  ---

(*) If supported by your compiler and enabled at compilation time

Other object values have the following associated C types:

* __'p'__: __`void*`__ 
* __'t'__: __`lua_State*`__
* __'c'__: __`lua_CFunction`__: _`int (*) (lua_State *L)`_
* __'n'__: __`void`__ : no associated value. In input, pushes a __`nil`__ value to the stack. In output, skips a result value from the stack.
* __'k'__: Pointer to function. A second parameter of any type must be provided; `ptr` will receive its address. The pointer function has one of these two prototypes, depending of the data direction:
	* for intput __`lgencall_pushCB`__: _`void (*)(lua_State* L, const void* ptr)`_
	* for output __`lgencall_getCB`__: _`void (*)(lua_State* L, int idx, void* ptr)`_

Finally, for each parameter its expected type depends on whether it is on input or output direction, and on its width and flag arguments. Let be `TYPE` the basic C type as stated in previous 2 tables. Except for __'n'__ and __'k'__ conversion characters, the composed types are:

	Width argument    (none)            number, '*' or '&'   number, '*' or '&'
	                                    
	Flag              (don't care)      (none)               '#' or '+'
	
	                                                         
	Input             TYPE              const TYPE*          const TYPE*
	                                                         
	Output            TYPE*             TYPE*                TYPE**

Directive format string
-----------------------

In the directive part of the format string, the following conversion characters (all uppercases) are supported:

* __'M'__: Set or get Lua memory allocation function. If flag is empty, the argument is of type __`lua_Alloc`__: _void*(*) (void *ud, void *ptr, size_t osize, size_t nsize)_ and sets the allocation function. If flag is __'&'__, the expected type is __`lua_Alloc*`__ and the current allocation function is returned.
* __'O'__: Standard libraries will be initialized by calling `luaL_openlibs`
* __'S'__: An argument of type __`lua_State**`__ follows, that will retrieve the allocated Lua state
* __'C'__: Lua state will be freed with `lua_close` at the end of the call
* __'F'__: Flush the compilation cache before compiling this chunk. Useful to save memory when a lot of different script chunks have been compiled.
* __'G'__: Run a complete garbage collection before running the chunk

Source code
===========

Copyright
---------

Lua Generic call library has been placed under the same MIT license as Lua itself. This means that although the library is copyrighted by Olivetti Engineering SA, it is free software and can be used for both academic and commercial purposes at absolutely no cost.

Source files
------------

The library distribution consists in just one C implementation file {{lgencall.c}} and one header file {{lgencall.h}}. There is also a testing file {{testwin.cpp}}, which includes all test examples of the next chapter, including Windows header file {{tchar.h}}.  Using this utility header, it is possible to write code that compile for both ANSI and Unicode platforms. 

The main C file includes ANSI standard files, and the public Lua API header files. Like other standard Lua libraries, no private feature is used, and the file can be compiled in both C and C++ languages. However, it requires the new C99 include file {{stdint.h}} to define fixed size integers. If your compiler does not support this, there are several free versions available on the WWW. [http://www.azillionmonkeys.com/qed/pstdint.h] [http://msinttypes.googlecode.com/svn/trunk/stdint.h]

The source file can either be compiled together with the application, or placed inside Lua shared library if you can afford to recompile it.

Compilation switches
--------------------

In header file {{lgencall.h}} are defined 3 compilation macros which are used to customize the library for your platform. Each parameter can either be changed in the file itself, or specified on the compiler's command line. A small explanation for it is present in the header file, listing the possible values. Also, the compilation is affected by the following standard macros: `__cplusplus`, `INT_MAX`, `UINT_MAX` and `__`STDC_VERSION`__`.

Examples
========

Directive elements
-------------------

For sure, examples will help you to understand the different features. The first examples show the usage of various directive formats.
Let us begin with the simplest one, a {{"Hello World"}} program of course:

	lua_genpcall(NULL, "print 'Hello World!'", "%O<");

Here no Lua state is passed to the function, so it is automatically allocated. The __%O__ in the directive part instructs to open standard libraries, with include the global `print` function. Since no __%S__ option is passed, Lua state is freed at the end of the call.
We haven't tested the return value, which is the error message.

Here is the same example, but as a more complete and realistic implementation:

	lua_State* L = lua_open();
	luaL_openlibs(L);
	const char* errmsg = lua_genpcall(L, "print 'Hello World!'", "");
	if(errmsg)
	  fprintf(stderr, "Lua error: %s\n", errmsg);
	lua_close(L);

Here, Lua state is manually created, filled with standard libraries and freed. The return error message is tested and printed in case of trouble.

Again the same example, but using only `lua_genpcall`:

	lua_State* L;
	lua_Alloc falloc;
	lua_genpcall(NULL, NULL, "%O %S %&M<", &L, &falloc);
	char* errmsg = lua_genpcall(L, "print 'Hello World!'", "%C<");
	if(errmsg)
	{
	  fprintf(stderr, "Lua error: %s\n", errmsg);
	  falloc(NULL, errmsg, 10, 0);
	}

The first call will allocate a new Lua state, open standard libraries (__%O__), return the Lua state (__%S__) and also the memory allocation function (__%&M__). The second call prints the message and destroys Lua state (__%C__). Because of this, the error message (if present) is allocated with Lua allocation function, and not taken from the stack. Therefore, it is best to free it with the same function (passing 0 as the new size).

Input elements
--------------

There will be 6 examples of how to input data from C to Lua:

1. Numbers
2. Boolean, nil, simple strings and light userdata
3. C functions and call-backs
4. Numerical arrays
5. Advanced strings
6. String lists

For all these examples, we will suppose that Lua state is already open and that it will be closed at the end. We are not testing for return errors to simplify the coding.

### 1. Numbers

	lua_genpcall(L, "for k,v in pairs{...} do print(k, type(v), v) end", 
	  "%i %d %u %f %f", -4, 0xFFFFFFFF, 0xFFFFFFFF, 
	  3.1415926535f, 3.1415926535);
	-->
	1       number  -4
	2       number  -1
	3       number  4294967295
	4       number  3.1415927410126
	5       number  3.1415926535

The script chunk loops over the arguments and for each one prints its type and value, as long as the index. Here, five numerical arguments are passed: three integers, a floating point and a double floating point (all seen as type `number` by Lua). Because in Lua all numbers are stored as __`double`__, there is a truncation error in the value of `Pi` for the float argument. Please note the difference in behaviour between __%d__ and __%u__ for the value `0xFFFFFFFF`. For the __`double`__ parameter, it is not necessary to specify __%lf__ instead of __%f__ here, because floating point numbers are always converted to __`double`__ when passed to a variable argument function in C. Integers smaller than __`int`__ are also automatically converted to __`int`__. 

### 2. Boolean, nil, simple strings and light userdata

	lua_genpcall(L, "for k,v in pairs{...} do print(k, type(v), v) end", 
	  "%b %b %n %s %p", 0, 1, "Hello", L);
	-->
	1       boolean false
	2       boolean true
	4       string  Hello
	5       userdata        userdata: 0096CE70

Boolean values can be 0 or 1, or `true` and `false` if compiled in C++ or in C99 languages. The __`nil`__ parameter __%n__ is only present in the format string, no parameter is associated (it is not printed because function `pairs` skips __`nil`__ values).  The string is here supposed to be zero terminated, and the last parameter `L` (the Lua state) is just an example of a generic pointer.

### 3. C functions and call-backs

	int cFunction(lua_State* L)
	{
	  printf("%s\n", luaL_checkstring(L, 1));
	  return 1;
	}
	void pushMessage(lua_State* L, const void* ptr)
	{
	  lua_pushstring(L, *(const char**)ptr);
	}
	...
	lua_genpcall(L, "local fct, msg = ...; fct(msg)", 
	  "%c %k", cFunction, pushMessage, "Hello from C!");

The first Lua argument is of type function and is passed as a pointer to `cFunction`. The second argument is a call-back parameter consisting of the user function `pushMessage` and a string. Please note that the call-back function receives a _pointer_ to the argument and not the argument itself!

### 4. Numerical arrays

	short array[] = { 1,2,3 };
	lua_genpcall(L, 
	  "for k,v in pairs{...} do print(k, #v, table.concat(v, ', ')) end", 
	  "%2hd %5.1u %*.*d", array, "Hello", 
	  sizeof(array)/sizeof(array[0]), sizeof(array[0]), array);
	-->
	1       2       1, 2
	2       5       72, 101, 108, 108, 111
	3       3       1, 2, 3

The code chunk prints the parameter index, the length of the array and a list with its values. For array data, it is necessary to specify the precision or a size modifier (unless it is the default type) and a width value. The first parameter declares a width of 2, therefore only the first two numbers of the __`short`__ array are received. The second parameter is a string (__char[]__), so its precision is 1 and the width is the string length. On the third argument, both width and precision are passed though the argument list since the format specifies __'*'__.

### 5. Advanced strings

	unsigned char data[] = { 200, 100, 0, 3, 5, 0 };
	lua_genpcall(L, "for k,v in pairs{...} do print(k, v:gsub('.', "
	  "function(c) return '\\\\'.. c:byte() end)) end", 
	  "%s %6s %*s %ls", "Hello", "P1\0P2", sizeof(data), data, L"été"); 
	-->
	1       \72\101\108\108\111     5
	2       \80\49\0\80\50\0        6
	3       \200\100\0\3\5\0        6
	4       \195\169\116\195\169    5 

Strings are not necessarily zero terminated arrays of __`char`__. Here the script chunk prints the argument index, then the string in which each byte is replaced by a backslash and its decimal value. Note that the backslash has to be escaped _twice_: first for C (the chunk passed to Lua is {{... return '\\' ...}}), and second for Lua. The first argument is a zero terminated string; the second is a string containing a binary 0, specified by its length. The third argument is a binary array of data, for which the length is passed by argument. And the last argument is a wide character string, which is converted to a UTF-8 string or another form of multi-byte string, depending on how the module was compiled.

### 6. String lists

	lua_genpcall(L, 
	  "for k,v in pairs{...} do print(k, #v, table.concat(v, ',')) end",
	  "%z  %7z %hz %*lz", "s1\0s2\0s3\0", "s4\0\0s5\0", 
	  "c1\0c2\0c3\0", 7, L"w1\0\0w2\0"	);
	-->
	1       3       s1,s2,s3
	2       3       s4,,s5
	3       3       c1,c2,c3
	4       3       w1,,w2

An array of strings is expected to be a zero terminated list of zero terminated strings on the C side. In other words, it is a string containing one a more additional null characters inside it, delimiting elements. Because of this, it is impossible to support strings with embedded zeros. 
In the first example, no width is specified, so the string list automatically ends on the first double zero bytes. The second list has its second string element of length 0. In this case, if no width was provided in the format string, the array would be erroneously of length 1, because there are two consecutive zero bytes in the middle. By specifying the width to be 7 (so not counting the last zero byte, as in usual strings), the number of received array elements is correctly 3. The third example simply specifies that the string list is of type __`char*`__. Otherwise, on Unicode support, `lua_genpcallW` expects wide character strings. The last list is a wide character version specifying its length with an addition __'*'__ parameter.

Output elements
---------------

In output mode, the main differences are that we must normally pass pointer to variables and not values, and we should always specify the precision field. Beware: on little endian processors like Intel ones, passing a wrong precision value might work anyway; but it will surely fail on big endian platforms! There are again 6 examples, demonstrating the same data types as for input elements.

### 1. Numbers

	char var1; unsigned short var2; int var3;
	float var4; double var5;
	lua_genpcall(L, "return 1, 2, 3, 4, 5", ">%hhd %hu %d %f %lf", 
	  &var1, &var2, &var3, &var4, &var5);
	printf("%d %u %d %f %f\n", var1, var2, var3, var4, var5);
	-->
	1 2 3 4.000000 5.000000

This sample retrieves 5 numerical values of different types and sizes. The second variable is unsigned, `var1` and `var3` are signed integers, and the last 2 are floating point numbers. In this case, the __%lf__ format is mandatory!

### 2. Boolean, nil, simple strings and light userdata

	bool bool1; int bool2; 
	const char* str; void* ptr;
	lua_genpcall(L, "return true, false, 'dummy', 'Hello', io.stdin", 
	  ">%hb %lb %n %+s %p", &bool1, &bool2, &str, &ptr);
	printf("%d %d %s %p\n", bool1, bool2, str, ptr);
	-->
	1 0 Hello 00975598

In this example the first two parameters retrieved are two Boolean values, of C different types. The third return value is discarded because of the __%n__ format. A {{'Hello'}} string is received through Lua stack with __%+s__ idiom. And the last result value, a userdatum value, is got by address into a generic pointer.

### 3. C functions and call-backs

	void getMessage(lua_State* L, int idx, void* ptr)
	{
	  *(const char**)ptr = lua_tostring(L, idx);
	}
	...
	lua_CFunction fct;
	const char* msg;
	lua_genpcall(L, "return print, 'Hello World!'", 
	  ">%c %k", &fct, getMessage, &msg);
	lua_pushstring(L, msg);
	fct(L);

This sample is a complicated way to implement the {{Hello World}} program. The first return value is a pointer to a Lua registered C function, the global `print` function. The second value, a simple string, is retrieved through a callback function, which receives as its `ptr` argument the address of variable `msg`. Then we can print the message by pushing the message onto Lua stack and call the `print` function directly by C (which is certainly not a good practice in normal situations). 

### 4. Numerical arrays

	unsigned int int_a[3];
	bool bool_a[4];
	char* str; 
	short* pshort;
	int short_len;
	int bool_len = sizeof(bool_a)/sizeof(bool_a[0]);
	lua_genpcall(L, "return {1,2,3,4},{72,101,108,108,111,0}, {5,6,7}, {false,true}", 
	  ">%3u %+.1d %#&hd %&.*b", &int_a, &str, &short_len, &pshort, 
	  &bool_len, sizeof(bool_a[0]), &bool_a);
	printf("int_a = {%u,%u,%u}\nstr = %s\npshort[%d]=%d\nbool_a = #%d:{%d,%d,%d,%d}\n", 
	  int_a[0], int_a[1], int_a[2], str, short_len-1, pshort[short_len-1],
	  bool_len, bool_a[0], bool_a[1], bool_a[2], bool_a[3]);
	free(pshort);
	-->
	int_a = {1,2,3}
	str = Hello
	pshort[2]=7
	bool_a = #2:{0,1,204,204}

The first array is allocated in the C stack and filled up by Lua up to 3 values (the last one is lost). The second is allocated on Lua stack and because it is of type __`char`__, its precision is set to 1. Note that because the __'+'__ sign is present, it is not necessary to specify a width. In the third array, the real length is returned (because of __'&'__) in addition to a pointer allocated by the current Lua allocation function (instructed by __'#'__). We have to free this buffer after use. In the fourth Boolean array, the precision is passed by the __'*'__ feature. More interesting, the width argument is passed in both input and output directions with the __'&'__ sign. The value of `bool_len` must be initialized to the size of the array before the call, since we are using a C stack allocated buffer. Because the buffer is larger than the returned array, its last two values will remain not initialized. 

### 5. Advanced strings

	const char *str1;
	char *str2;
	char str3[10];
	unsigned char data[6];
	int len = sizeof(data);
	wchar_t* wstr;
	lua_genpcall(L, "return 'Hello', ' Wor', 'ld!', '\\0\\5\\200\\0', 'Unicode'",
	  ">%+s %#s %*s %&s %+ls", &str1, &str2, sizeof(str3), str3, &len, data, &wstr);
	printf("%s%s%s\ndata (%d bytes): %02X %02X %02X %02X %02X\nwstr = %S\n", 
	  str1, str2, str3, len, data[0],data[1],data[2],data[3],data[4], wstr);
	free(str2);
	-->
	Hello World!
	data (4 bytes): 00 05 C8 00 00
	wstr = Unicode

This sample retrieves five strings in different ways. The first string is taken from Lua stack (__'+'__ sign). The second is allocated by Lua current allocation function, and must therefore be freed after its use. The third one is taken from the C stack and the buffer size is passed through the __'*'__ width specification. The next return value is considered as a string on Lua side, but is defined as a raw byte buffer in C. Through the __'&'__ mechanism, we both set the buffer size by initializing variable len, and get back the real data size after the call. Note that there is always an additional zero byte copied to the destination buffer (if there is enough place). The last value is a wide character string, placed on Lua stack.

### 6. String lists

	void print_string_list(const char* title, const void* data, int fchar){
	  printf("%-4s = {", title);
	  if(fchar) {
	    const char* str = (const char*)data;
	    while(*str){
	      printf("'%s', ", str);
	      str += strlen(str) + 1;
	    }
	  }
	  else {
	    const wchar_t* str = (const wchar_t*)data;
	    while(*str) {
	      printf("'%S', ", str);
	      str += wcslen(str) + 1;
	    }
	  }
	  printf("}\n");
	}
	…
	const char *str1;
	char *str2;
	char str3[10];
	int len;
	wchar_t* wstr;
	lua_genpcall(L, "return {1,2,3},{4,5,6},{10,9,8,7},{11,12}",
	  ">%+hz %+&z %*z %#lz", &str1, &len, &str2, 
	  sizeof(str3)/sizeof(str3[0]), &str3, &wstr );
	print_string_list("str1", str1, 1);
	print_string_list("str2", str2, 1);
	printf("len = %d\n", len);
	print_string_list("str3", str3, 1);
	print_string_list("wstr", wstr, 0);
	free(wstr);
	-->
	str1 = {'1', '2', '3', }
	str2 = {'4', '5', '6', }
	len = 6
	str3 = {'10', '9', '8', '7', }
	wstr = {'11', '12', }

In this last example, the helper function `print_string_list` is just here to display the retrieved string lists in a readable form. 
The first string list is of type __`char*`__, whether or not the Unicode version is used, which is not the case for `str2` and `str3`. 
The first list is allocated on Lua stack; the second one also, with in addition the retrieval of the string list length (counted without the last zero byte). 
The buffer of `str3` is on the C stack; so its size is passed as an additional parameter instructed by the __'*'__ flag. 
The last string list is a wide character version; its buffer is allocated with Lua allocator, requiring a call to `free` after use. 
You have certainly noted the strong similarities between string and string list parameters.

_Patrick Rapin_
