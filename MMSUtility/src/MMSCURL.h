/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   CURL.h
 * Author: giuliano
 *
 * Created on March 29, 2018, 6:27 AM
 */

#ifndef CURL_h
#define CURL_h

#include "spdlog/spdlog.h"
#include "json/json.h"
#include <fstream>
#include <vector>

using namespace std;

#ifndef __FILEREF__
    #ifdef __APPLE__
        #define __FILEREF__ string("[") + string(__FILE__).substr(string(__FILE__).find_last_of("/") + 1) + ":" + to_string(__LINE__) + "] "
    #else
        #define __FILEREF__ string("[") + basename((char *) __FILE__) + ":" + to_string(__LINE__) + "] "
    #endif
#endif

struct ServerNotReachable: public exception {
	char const* what() const throw()
	{
		return "Server not reachable";
	};
};

class MMSCURL {
    
public:

    struct CurlDownloadData {
        int64_t		ingestionJobKey;
		string		loggerName;
        int         currentChunkNumber;
        string      destBinaryPathName;
        ofstream    mediaSourceFileStream;
        size_t      currentTotalSize;
        size_t      maxChunkFileSize;
    };

	struct CurlUploadData {
		string		loggerName;
		ifstream	mediaSourceFileStream;

		int64_t		bytesSent;
		int64_t		upToByte_Excluded;
	};

	struct CurlUploadFormData {
		string		loggerName;
		ifstream	mediaSourceFileStream;

		int64_t		bytesSent;
		int64_t		upToByte_Excluded;

		bool		formDataSent;
		string		formData;

		bool		endOfFormDataSent;
		string		endOfFormData;
	};

	static string httpGet(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static Json::Value httpGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static string httpPostString(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string body,
		string contentType,	// i.e.: application/json
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static string httpPutString(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string body,
		string contentType,	// i.e.: application/json
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static Json::Value httpPostStringAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string body,
		string contentType,	// i.e.: application/json
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static Json::Value httpPutStringAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string body,
		string contentType,	// i.e.: application/json
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static string httpPostFile(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string pathFileName,
		int64_t fileSizeInBytes,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static string httpPutFile(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string pathFileName,
		int64_t fileSizeInBytes,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static Json::Value httpPostFileAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string pathFileName,
		int64_t fileSizeInBytes,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static Json::Value httpPutFileAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string pathFileName,
		int64_t fileSizeInBytes,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static string httpPostFileSplittingInChunks(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string pathFileName,
		int64_t fileSizeInBytes,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static string httpPostFormData(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static string httpPutFormData(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static Json::Value httpPostFormDataAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static Json::Value httpPutFormDataAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

	static string httpPostFileByFormData(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		string pathFileName,
		int64_t fileSizeInBytes,
		string mediaContentType,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static string httpPutFileByFormData(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		string pathFileName,
		int64_t fileSizeInBytes,
		string mediaContentType,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static Json::Value httpPostFileByFormDataAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		string pathFileName,
		int64_t fileSizeInBytes,
		string mediaContentType,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static Json::Value httpPutFileByFormDataAndGetJson(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		long timeoutInSeconds,
		string pathFileName,
		int64_t fileSizeInBytes,
		string mediaContentType,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15,
		int64_t contentRangeStart = -1,
		int64_t contentRangeEnd_Excluded = -1
	);

	static void downloadFile(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		string destBinaryPathName,
		int maxRetryNumber = 1,
		int secondsToWaitBeforeToRetry = 15
	);

private:
	static string httpPostPutString(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		string requestType,	// POST or PUT
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string body,
		string contentType,	// i.e.: application/json
		int maxRetryNumber,
		int secondsToWaitBeforeToRetry
	);

	static string httpPostPutFile(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		string requestType,	// POST or PUT
		long timeoutInSeconds,
		string basicAuthenticationUser,
		string basicAuthenticationPassword,
		string pathFileName,
		int64_t fileSizeInBytes,
		int maxRetryNumber,
		int secondsToWaitBeforeToRetry,
		int64_t contentRangeStart,
		int64_t contentRangeEnd_Excluded
	);

	static string httpPostPutFormData(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		string requestType,	// POST or PUT
		long timeoutInSeconds,
		int maxRetryNumber,
		int secondsToWaitBeforeToRetry
	);

	static string httpPostPutFileByFormData(
		shared_ptr<spdlog::logger> logger,
		int64_t ingestionJobKey,
		string url,
		vector<pair<string, string>> formData,
		string requestType,	// POST or PUT
		long timeoutInSeconds,
		string pathFileName,
		int64_t fileSizeInBytes,
		string mediaContentType,
		int maxRetryNumber,
		int secondsToWaitBeforeToRetry,
		int64_t contentRangeStart,
		int64_t contentRangeEnd_Excluded
	);
};

#endif

