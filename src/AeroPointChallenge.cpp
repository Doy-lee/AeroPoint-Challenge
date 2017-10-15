#define DQN_IMPLEMENTATION
#define DQN_PLATFORM_HEADER
#define DQN_WIN32_IMPLEMENTATION
#include "dqn.h"

#define CURL_STATICLIB
#include "curl/curl.h"

#include <stdio.h> // Printf

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

void CheckCurl(const CURLcode result, const char *const file, const i32 lineNum,
                     const char *const expr)
{
	const char *errorMsg = curl_easy_strerror(result);
	printf("%s|%d|%s failed: %s", file, lineNum, expr, errorMsg);
	DieSafely();

}

struct HttpData
{
	DqnArray<char> buffer;
};

size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userData)
{
	HttpData *context = static_cast<HttpData *>(userData);
	size_t totalSize  = size * nmemb;
	DQN_ASSERT(context->buffer.Push(ptr, totalSize));

	return totalSize;
}

i32 main(i32 argc, char *argv[])
{
	DqnMemStack tmpMem = {}, mainMem = {};
	HttpData httpData = {};

	bool init = true;
	init &= tmpMem.Init(DQN_MEGABYTE(1), true, 4);
	init &= mainMem.Init(DQN_MEGABYTE(2), true, 4);
	init &= httpData.buffer.Init(DQN_MEGABYTE(1), DqnMemAPI_HeapAllocator());
	if (!init) DieSafely();

	CHECK_CURL(curl_global_init(CURL_GLOBAL_WIN32));
	CURL *curlHandle = curl_easy_init();
	if (!curlHandle)
	{
		printf("curl_easy_init() failed");
		DieSafely();
	}

	// const char url[] = "ftp://www.ngs.noaa.gov/cors/rinex/2017/288/nybp/nybp288a.17o.gz";
	const char url[] = "google.com";
	CHECK_CURL(curl_easy_setopt(curlHandle, CURLOPT_URL, url));
	CHECK_CURL(curl_easy_setopt(curlHandle, CURLOPT_VERBOSE, 1));
	CHECK_CURL(curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, &httpData));
	CHECK_CURL(curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, WriteCallback));
	CHECK_CURL(curl_easy_perform(curlHandle));

	if (httpData.buffer.count > 0)
	{
		printf("Received data:\n%s\n", httpData.buffer.data);
	}

	curl_global_cleanup();
	return 0;
}
