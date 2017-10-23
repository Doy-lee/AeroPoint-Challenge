#define DQN_IMPLEMENTATION
#define DQN_PLATFORM_HEADER
#define DQN_WIN32_IMPLEMENTATION
#include "dqn.h"

#define CURL_STATICLIB
#include "curl/curl.h"

#include <stdio.h> // Printf

struct Context
{
	DqnMemStack mainMem;
	DqnMemStack tmpMem;

	DqnArray<u8> httpBuffer;
	CURL        *curlHandle;
};

FILE_SCOPE i8        globalDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
FILE_SCOPE DqnMemAPI globalTmpMemAPI;
FILE_SCOPE DqnMemAPI globalMainMemAPI;

FILE_SCOPE void DieSafely()
{
	printf("AeroPointChallenge: Encountered unrecoverable error, exiting prematurely.\n");
	exit(-1);
}

#define CHECK_CURL(expr)                                                                           \
	do                                                                                             \
	{                                                                                              \
		CURLcode result = expr;                                                                    \
		if (result != CURLE_OK)                                                                    \
		{                                                                                          \
			CheckCurl(result, __FILE__, __LINE__, #expr);                                          \
		}                                                                                          \
	} while (0)

FILE_SCOPE void CheckCurl(const CURLcode result, const char *const file, const i32 lineNum,
                          const char *const expr)
{
	const char *errorMsg = curl_easy_strerror(result);
	printf("%s|%d|%s failed: %s\n", file, lineNum, expr, errorMsg);
	DieSafely();

}

FILE_SCOPE size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userData)
{
	DqnArray<char> *buffer = static_cast<DqnArray<char> *>(userData);
	size_t totalSize  = size * nmemb;
	DQN_ASSERT(buffer->Push(ptr, totalSize));

	return totalSize;
}

FILE_SCOPE void Log(const char *const msg, ...)
{
	const char *const formatStr = "AeroPointChallenge: %s\n";

	char userMsg[2048] = {};
	va_list argList; va_start(argList, msg);
	{
		i32 numCopied = Dqn_vsprintf(userMsg, msg, argList);
		DQN_ASSERT_HARD(numCopied < DQN_ARRAY_COUNT(userMsg));
	}
	va_end(argList);
	printf(formatStr, userMsg);
}

class RequestDate
{
	enum Months
	{
		Month_January = 1,
		Month_February,
		Month_March,
		Month_April,
		Month_May,
		Month_June,
		Month_July,
		Month_August,
		Month_September,
		Month_October,
		Month_November,
		Month_December,
	};

public:
	i16  year;
	i8   month;
	i16  day;
	i8   hour;

	bool IsLeapYear() const
	{
		if      (this->year % 4   != 0) return false;
		else if (this->year % 100 != 0) return true;
		else if (this->year % 400 != 0) return false;
		return true;
	}

	void IncrementDay()
	{
		i32 daysInMonth = globalDaysInMonth[this->month - 1];
		if (IsLeapYear() && this->month == Month_February)
			daysInMonth++;

		this->day++;
		if (this->day > daysInMonth)
		{
			this->day = 1;
			this->month++;

			if (this->month > Month_December)
			{
				this->month = Month_January;
				this->year++;

				DQN_ASSERT_HARD(this->year >= 0 && this->year <= 9999);
			}
		}
	}

	i32 GetDayRelativeToYear() const
	{

		if (this->month < Month_January || this->month > Month_December) return -1;
		if (this->day < 1 || this->day > globalDaysInMonth[this->month - 1]) return -1;

		i32 result = 0;
		if (IsLeapYear() && this->month >= Month_February)
			result++;

		for (i32 i = 0; i < this->month - 1; i++)
			result += globalDaysInMonth[i];

		result += this->day;
		return result;
	}
};

struct StationRequest
{
	DqnString   id;
	RequestDate start;
	RequestDate end;
};

FILE_SCOPE bool ExtractDate(const char *const dateString, RequestDate *request)
{
	if (!request || !dateString) return false;

	const char *datePtr = dateString;
	i32 dateLen         = DqnStr_Len(dateString);

	auto AdvancePtr = [](const char *datePtr, i32 *dateLen, i32 advance) -> const char * {
		const char *result = datePtr + advance;
		*dateLen -= advance;

		return result;
	};

	i32 offset = DqnStr_FindFirstOccurence(datePtr, dateLen, "-", 1);
	if (offset != 4)
	{
		Log("Malformed year value");
		return false;
	}

	request->year = (i16)Dqn_StrToI64(datePtr, offset);
	datePtr = AdvancePtr(datePtr, &dateLen, offset + 1);

	offset = DqnStr_FindFirstOccurence(datePtr, dateLen, "-", 1);
	if (offset != 2)
	{
		Log("Malformed month value");
		return false;
	}

	request->month = (i8)Dqn_StrToI64(datePtr, offset);
	datePtr = AdvancePtr(datePtr, &dateLen, offset + 1);

	offset = DqnStr_FindFirstOccurence(datePtr, dateLen, "T", 1);
	if (offset != 2)
	{
		Log("Malformed day value");
		return false;
	}

	request->day = (i8)Dqn_StrToI64(datePtr, offset);
	datePtr = AdvancePtr(datePtr, &dateLen, offset + 1);

	offset = DqnStr_FindFirstOccurence(datePtr, dateLen, ":", 1);
	if (offset != 2)
	{
		Log("Malformed hour value");
		return false;
	}

	request->hour = (i8)Dqn_StrToI64(datePtr, offset);
	DQN_ASSERT(dateLen == DqnStr_Len(datePtr));

	if (request->year < 0 || request->year > 9999) return false;
	if (request->month < 1 || request->month > 12) return false;
	if (request->day < 1 || request->day > globalDaysInMonth[request->month]) return false;
	if (request->hour < 0 || request->hour > 23) return false;

	return true;
}

FILE_SCOPE bool Initialize(Context *const context)
{
	bool init = true;
	init &= context->tmpMem.Init(DQN_MEGABYTE(1), true, 4);
	init &= context->mainMem.Init(DQN_MEGABYTE(2), true, 4);
	init &= context->httpBuffer.Init(DQN_MEGABYTE(1), DqnMemAPI_StackAllocator(&context->mainMem));
	globalTmpMemAPI  = DqnMemAPI_StackAllocator(&context->tmpMem);
	globalMainMemAPI = DqnMemAPI_StackAllocator(&context->mainMem);

	CHECK_CURL(curl_global_init(CURL_GLOBAL_WIN32));
	context->curlHandle = curl_easy_init();

	if (!context->curlHandle)
	{
		Log("curl_easy_init() failed");
		init = false;
	}

	return init;
}

FILE_SCOPE bool GenerateQueryUrl(const char *baseStation, const RequestDate &date,
                                 DqnString *result)
{
	if (!baseStation || !result) return false;

	static const char FORMAT_STR[] = "ftp://www.ngs.noaa.gov/cors/rinex/%d/%d/%s/%s%d%c.%do.gz";

	auto TimeToHourBlock = [](i8 hour, char *result) -> bool {
		if (hour < 0 || hour > 24) return false;
		if (!result) return false;

		*result = 'a' + hour;
		return true;
	};

	char hourBlock;
	if (!TimeToHourBlock(date.hour, &hourBlock))
	{
		Log("Invalid arguments passed into TimeToHourBlock");
		return false;
	}

	i32 dayRelativeToYear = date.GetDayRelativeToYear();
	i32 yearTo2Digits     = date.year % 100;
	i32 numCopied = Dqn_sprintf(result->str, FORMAT_STR, date.year, dayRelativeToYear, baseStation,
	                            baseStation, dayRelativeToYear, hourBlock, yearTo2Digits);
	result->len += numCopied;
	DQN_ASSERT(numCopied <= result->max);

	return true;
}

FILE_SCOPE bool MakeListOfUrlsToQuery(DqnArray<DqnString> *const queryList, StationRequest *const request)
{
	DqnString exampleStr = DQN_STRING_LITERAL(
	    exampleStr, "ftp://www.ngs.noaa.gov/cors/rinex/2017/288/nybp/nybp288a.17o.gz");

	auto LogFailedQueryUrl = [](const RequestDate &date) -> void {
		Log("Failed to generate query string on: %d-%d-%dT%d:%d", date.year, date.month, date.day,
		    date.hour);
	};

	RequestDate start      = request->start;
	const RequestDate &end = request->end;

	while (start.year < end.year || start.GetDayRelativeToYear() < end.GetDayRelativeToYear())
	{
		DqnString query;
		query.InitSize(exampleStr.len, globalMainMemAPI);
		for (; start.hour < 24; start.hour++)
		{
			bool result = GenerateQueryUrl(request->id.str, start, &query);
			if (!result)
			{
				LogFailedQueryUrl(start);
				return false;
			}

			queryList->Push(query);
		}

		start.hour = 0;
		start.IncrementDay();
	}

	for (DqnString query = {}; start.hour <= end.hour; start.hour++)
	{
		query.InitSize(exampleStr.len, globalMainMemAPI);

		bool result = GenerateQueryUrl(request->id.str, start, &query);
		if (!result)
		{
			LogFailedQueryUrl(start);
			return false;
		}

		queryList->Push(query);
	}

	return true;
}

i32 main(i32 argc, char *argv[])
{
	////////////////////////////////////////////////////////////////////////////
	// Initialisation
	////////////////////////////////////////////////////////////////////////////
	if (argc != 4)
	{
		Log("Requires 4 arguments, received: %d", argc);
		return 1;
	}

	Context context = {};
	if (!Initialize(&context))
	{
		Log("Initalising context failed, CURL or Out Of Memory");
		DieSafely();
	}

	////////////////////////////////////////////////////////////////////////////
	// Parse Arguments
	////////////////////////////////////////////////////////////////////////////
	StationRequest request = {};
	if (!request.id.InitLiteralNoAlloc(argv[1]))
	{
		Log("Base station string failed to initialise");
		return -1;
	}

	// Parse date strings on argv
	{
		bool datesValid = true;
		datesValid &= ExtractDate(argv[2], &request.start);
		datesValid &= ExtractDate(argv[3], &request.end);

		if (!datesValid)
		{
			Log("Date arguments are invalid non-conforming strings: '%s' and '%s'", argv[2],
			    argv[3]);
			return -1;
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Generate Query URLS
	////////////////////////////////////////////////////////////////////////////
	DqnArray<DqnString> queryList = {}; queryList.Init(16, globalMainMemAPI);
	if (!MakeListOfUrlsToQuery(&queryList, &request))
	{
		Log("Failed to create list of urls to query.");
		return -1;
	}

	for (auto i = 0; i < queryList.count; i++)
	{
		printf("%s\n", queryList.data[i].str);
	}

	////////////////////////////////////////////////////////////////////////////
	// Query Website
	////////////////////////////////////////////////////////////////////////////
	// const char url[] = "ftp://www.ngs.noaa.gov/cors/rinex/2017/258/nynb/nynb2580.17S.md5";
	CHECK_CURL(curl_easy_setopt(context.curlHandle, CURLOPT_VERBOSE, 1));
	CHECK_CURL(curl_easy_setopt(context.curlHandle, CURLOPT_WRITEDATA, &context.httpBuffer));
	CHECK_CURL(curl_easy_setopt(context.curlHandle, CURLOPT_WRITEFUNCTION, WriteCallback));

	DqnArray<DqnString> fileNames = {}; fileNames.Init(queryList.count, globalMainMemAPI);
	for (auto i = 0; i < queryList.count; i++)
	{
		DqnArray<u8> *const httpBuffer = &context.httpBuffer;
		const DqnString &query         = queryList.data[i];
		CHECK_CURL(curl_easy_setopt(context.curlHandle, CURLOPT_URL, query.str));
		CURLcode error = curl_easy_perform(context.curlHandle);

		if (error == CURLE_OK)
		{
			if (httpBuffer->count > 0)
			{
				char *const dest  = DqnChar_FindLastChar(query.str, '/', query.len, nullptr) + 1;
				DqnString destStr = {}; destStr.InitLiteralNoAlloc(dest);
				fileNames.Push(destStr);

				Log("Writing %d bytes to disk: %s", httpBuffer->count, dest);

				DqnFile file(true);
				file.Open(dest, DqnFilePermissionFlag_Write, DqnFileAction_ForceCreate);
				file.Write(context.httpBuffer.data, httpBuffer->count, 0);
				httpBuffer->Clear();
			}
			else
			{
				Log("CURL query to '%s' successful but downloaded no bytes", query.str);
				DieSafely();
			}
		}
		else
		{
			const char *errorMsg = curl_easy_strerror(error);
			Log("CURL encountered error: %s: Trying to download '%s'", errorMsg, query.str);
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Execute TEQC Merge
	////////////////////////////////////////////////////////////////////////////
	{
		DqnString exeDir; exeDir.InitSize(1024, globalTmpMemAPI);
		i32 offsetToLastBackslash = DqnWin32_GetEXEDirectory(exeDir.str, exeDir.max);
		while (offsetToLastBackslash == -1)
		{
			offsetToLastBackslash = DqnWin32_GetEXEDirectory(exeDir.str, exeDir.max);
			if (!exeDir.Expand((i32)(exeDir.len * 1.5f)))
			{
				Log("Out of memory error");
				DieSafely();
			}
		}

		i32 oneAfterLastBackSlash = offsetToLastBackslash + 1;
		DqnString teqcExe    = DQN_STRING_LITERAL(teqcExe, "teqc.exe");
		const i32 SPACE_CHAR = 1;

		i32 requiredSize = teqcExe.len + SPACE_CHAR;
		for (auto i = 0; i < fileNames.count; i++)
		{
			requiredSize += fileNames.data[i].len;
			requiredSize += SPACE_CHAR;
		}

		DqnString cmdStr = {}; cmdStr.InitSize(requiredSize, globalTmpMemAPI);
		cmdStr.AppendStr(teqcExe);
		for (auto i = 0; i < fileNames.count; i++)
		{
			cmdStr.AppendCStr(" ");
			cmdStr.AppendStr(fileNames.data[i]);
		}

		DqnString fullTeqcExePath = {}; fullTeqcExePath.InitSize(oneAfterLastBackSlash + teqcExe.len);
		fullTeqcExePath.AppendStr(exeDir, oneAfterLastBackSlash);
		fullTeqcExePath.AppendStr(teqcExe);

		STARTUPINFO startInfo     = {};
		startInfo.cb              = sizeof(startInfo);
		const bool INHERIT_HANDLE = false;

		PROCESS_INFORMATION procInfo = {};
		if (CreateProcess(/* lpApplicationName */    fullTeqcExePath.str,
		                  /* lpCommandline */        cmdStr.str,
		                  /* lpProcessAttributes */  NULL,
		                  /* lpThreadAttributes */   NULL,
		                  /* bInheritHandles */      INHERIT_HANDLE,
		                  /* dwCreationFlags */      0,
		                  /* lpEnvironment */        NULL,
		                  /* lpCurrentDirectory */   NULL,
		                  /* lpStartupInfo */        &startInfo,
		                  /* lpProcessInformation */ &procInfo) == 0)
		{
			Log("CreateProcess failed");
			Log(".. Could not start encoder process are arguments correct? %s", cmdStr.str);
			return -1;
		}

		DWORD exitCode = 0;
		WaitForSingleObject(procInfo.hProcess, INFINITE);
		GetExitCodeProcess(procInfo.hProcess, &exitCode);

		CloseHandle(procInfo.hProcess);
		CloseHandle(procInfo.hThread);
	}

	// curl_global_cleanup();
	return 0;
}
