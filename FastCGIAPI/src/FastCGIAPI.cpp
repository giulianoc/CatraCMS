
#include <deque>
#include <vector>
#include <sstream>
#include <fstream>
#include <regex>
#include <iostream>
#include <sys/utsname.h>
#include "Compressor.h"
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif
#include "spdlog/spdlog.h"
#include "FastCGIAPI.h"

extern char** environ;


FastCGIAPI::FastCGIAPI(
	Json::Value configuration, 
	mutex* fcgiAcceptMutex)
{
	init(configuration, fcgiAcceptMutex);
}


FastCGIAPI::~FastCGIAPI() = default;

void FastCGIAPI::init(
	Json::Value configuration,
	mutex* fcgiAcceptMutex)
{
	_configuration			= configuration;
	_fcgiAcceptMutex		= fcgiAcceptMutex;

	{
		struct utsname unUtsname;
		if (uname (&unUtsname) != -1)
			_hostName = unUtsname. nodename;
	}

	_requestIdentifier = 0;

	_maxAPIContentLength = JSONUtils::asInt64(_configuration["api"], "maxContentLength", 0);
	SPDLOG_INFO("Configuration item"
		", api->maxContentLength: {}", _maxAPIContentLength
	);

}

int FastCGIAPI::operator()()
{
	string sThreadId;
	{
		thread::id threadId = this_thread::get_id();
		stringstream ss;
		ss << threadId;
		sThreadId = ss.str();
	}

	FCGX_Request request;

	// 0 is file number for STDIN by default
	// The fastcgi process is launched by spawn-fcgi (see scripts/mmsApi.sh scripts/mmsEncoder.sh)
	// specifying the port to be used to listen to nginx calls
	// The nginx process is configured to proxy the requests to 127.0.0.1:<port>
	// specified by spawn-fcgi
	int sock_fd = 0;
	SPDLOG_INFO("FastCGIAPI::FCGX_OpenSocket"
		", threadId: {}"
		", sock_fd: {}",
		sThreadId, sock_fd
	);
	FCGX_InitRequest(&request, sock_fd, 0);

	bool shutdown = false;    
	while (!shutdown)
	{
		_requestIdentifier++;

		int returnAcceptCode;
		{
			SPDLOG_DEBUG("FastCGIAPI::ready"
				", _requestIdentifier: {}"
				", threadId: {}",
				_requestIdentifier, sThreadId
			);
			lock_guard<mutex> locker(*_fcgiAcceptMutex);

			SPDLOG_DEBUG("FastCGIAPI::listen"
				", _requestIdentifier: {}"
				", threadId: {}",
				_requestIdentifier, sThreadId
			);

			returnAcceptCode = FCGX_Accept_r(&request);
		}
		SPDLOG_DEBUG("FCGX_Accept_r"
			", _requestIdentifier: {}"
			", threadId: {}"
			", returnAcceptCode: {}",
			_requestIdentifier, sThreadId, returnAcceptCode
		);

		if (returnAcceptCode != 0)
		{
			shutdown = true;

			FCGX_Finish_r(&request);

			continue;
		}

		SPDLOG_DEBUG("Request to be managed"
			", _requestIdentifier: {}"
			", threadId: {}",
			_requestIdentifier, sThreadId
		);        

		unordered_map<string, string> requestDetails;
		unordered_map<string, string> queryParameters;
		string          requestBody;
		unsigned long   contentLength = 0;
		try
		{
			fillEnvironmentDetails(request.envp, requestDetails);
			fillEnvironmentDetails(environ, requestDetails);

			{
				unordered_map<string, string>::iterator it;

				if ((it = requestDetails.find("QUERY_STRING")) != requestDetails.end())
					fillQueryString(it->second, queryParameters);
			}

			{
				unordered_map<string, string>::iterator it;
				if ((it = requestDetails.find("REQUEST_METHOD")) != requestDetails.end() &&
					(it->second == "POST" || it->second == "PUT"))
				{                
					if ((it = requestDetails.find("CONTENT_LENGTH")) != requestDetails.end())
					{
                        if (it->second != "")
                        {
                            contentLength = stol(it->second);
                            if (contentLength > _maxAPIContentLength)
                            {
                                string errorMessage = string("ContentLength too long")
									+ ", _requestIdentifier: " + to_string(_requestIdentifier)
									+ ", threadId: " + sThreadId
                                    + ", contentLength: " + to_string(contentLength)
                                    + ", _maxAPIContentLength: " + to_string(_maxAPIContentLength)
                                ;

                                SPDLOG_ERROR(errorMessage);
            
                                throw runtime_error(errorMessage);
                            }
                        }
                        else
                        {
                            contentLength = 0;
                        }
                    }
                    else
                    {
                        contentLength = 0;
                    }

                    if (contentLength > 0)
                    {
                        char* content = new char[contentLength];

                        contentLength = FCGX_GetStr(content, contentLength, request.in);

                        requestBody.assign(content, contentLength);

                        delete [] content;
                    }
                }
            }
        }
        catch(runtime_error& e)
        {
            SPDLOG_ERROR(e.what());

            sendError(request, 500, e.what());

            FCGX_Finish_r(&request);
            
            // throw runtime_error(errorMessage);
            continue;
        }
        catch(exception& e)
        {
            string errorMessage = string("Internal server error");
            SPDLOG_ERROR(errorMessage);

            sendError(request, 500, errorMessage);

            FCGX_Finish_r(&request);
            
            // throw runtime_error(errorMessage);
            continue;
        }

        string requestURI;
        {
            unordered_map<string, string>::iterator it;

            if ((it = requestDetails.find("REQUEST_URI")) != requestDetails.end())
                requestURI = it->second;
        }

        Json::Value permissionsRoot;
        bool authorizationPresent = basicAuthenticationRequired(requestURI, queryParameters);
		string userName;
		string password;
        if (authorizationPresent)
        {
            try
            {
                unordered_map<string, string>::iterator it;

                if ((it = requestDetails.find("HTTP_AUTHORIZATION")) == requestDetails.end())
                {
                    SPDLOG_ERROR("No 'Basic' authorization is present into the request");

                    throw CheckAuthorizationFailed();
                }

                string authorizationPrefix = "Basic ";
				if (!(it->second.size() >= authorizationPrefix.size()
					&& 0 == it->second.compare(0, authorizationPrefix.size(), authorizationPrefix)))
				{
                    SPDLOG_ERROR("No 'Basic' authorization is present into the request"
						", _requestIdentifier: {}"
						", threadId: {}"
                        ", Authorization: {}",
						_requestIdentifier, sThreadId, it->second
                    );

                    throw CheckAuthorizationFailed();
                }

                string usernameAndPasswordBase64 = it->second.substr(authorizationPrefix.length());
                string usernameAndPassword = base64_decode(usernameAndPasswordBase64);
                size_t userNameSeparator = usernameAndPassword.find(":");
                if (userNameSeparator == string::npos)
                {
                    SPDLOG_ERROR("Wrong Authorization format"
						", _requestIdentifier: {}"
						", threadId: {}"
                        ", usernameAndPasswordBase64: {}"
                        ", usernameAndPassword: {}",
						_requestIdentifier, sThreadId, usernameAndPasswordBase64, usernameAndPassword
                    );

                    throw CheckAuthorizationFailed();
                }

                userName = usernameAndPassword.substr(0, userNameSeparator);
                password = usernameAndPassword.substr(userNameSeparator + 1);

				checkAuthorization(sThreadId, userName, password);
            }
            catch(CheckAuthorizationFailed& e)
            {
                SPDLOG_ERROR("checkAuthorization failed"
					", _requestIdentifier: {}"
					", threadId: {}" 
                    ", e.what(): {}",
					_requestIdentifier, sThreadId, e.what()
                );

                string errorMessage = e.what();
                SPDLOG_ERROR(errorMessage);

                sendError(request, 401, errorMessage);   // unauthorized

                FCGX_Finish_r(&request);

                //  throw runtime_error(errorMessage);
                continue;
            }
            catch(runtime_error& e)
            {
                SPDLOG_ERROR("checkAuthorization failed"
					", _requestIdentifier: {}"
					", threadId: {}"
                    ", e.what(): {}",
					_requestIdentifier, sThreadId, e.what()
                );

                string errorMessage = string("Internal server error");
                SPDLOG_ERROR(errorMessage);

                sendError(request, 500, errorMessage);

                FCGX_Finish_r(&request);

                // throw runtime_error(errorMessage);
                continue;
            }
            catch(exception& e)
            {
                SPDLOG_ERROR("checkAuthorization failed"
					", _requestIdentifier: {}"
					", threadId: {}"
                    ", e.what(): {}",
					_requestIdentifier, sThreadId, e.what()
                );

                string errorMessage = string("Internal server error");
                SPDLOG_ERROR(errorMessage);

                sendError(request, 500, errorMessage);

                FCGX_Finish_r(&request);

                //  throw runtime_error(errorMessage);
                continue;
            }
        }

		chrono::system_clock::time_point startManageRequest = chrono::system_clock::now();
        try
        {
            unordered_map<string, string>::iterator it;

            string requestMethod;
            if ((it = requestDetails.find("REQUEST_METHOD")) != requestDetails.end())
				requestMethod = it->second;

			bool responseBodyCompressed = false;
			{
				unordered_map<string, string>::iterator it;

				if ((it = requestDetails.find("HTTP_X_RESPONSEBODYCOMPRESSED"))
					!= requestDetails.end()
					&& it->second == "true")
				{
					responseBodyCompressed = true;
				}
			}

			manageRequestAndResponse(
				sThreadId, _requestIdentifier, responseBodyCompressed,
				request, requestURI, requestMethod, queryParameters,
				authorizationPresent,
				userName, password,
				contentLength, requestBody, requestDetails);            
        }
        catch(runtime_error& e)
        {
            SPDLOG_ERROR("manageRequestAndResponse failed"
				", _requestIdentifier: {}"
				", threadId: {}"
                ", e: {}",
				_requestIdentifier, sThreadId, e.what()
            );
        }
        catch(exception& e)
        {
            SPDLOG_ERROR("manageRequestAndResponse failed"
				", _requestIdentifier: {}" 
				", threadId: {}"
                ", e: {}",
				_requestIdentifier, sThreadId, e.what()
            );
        }
		{
			string method;

			auto methodIt = queryParameters.find("method");
			if (methodIt != queryParameters.end())
				method = methodIt->second;

			string clientIPAddress = getClientIPAddress(requestDetails);

			chrono::system_clock::time_point endManageRequest = chrono::system_clock::now();
			SPDLOG_INFO("manageRequestAndResponse"
				", _requestIdentifier: {}"
				", threadId: {}"
				", clientIPAddress: @{}@"
				", method: @{}@"
				", requestURI: {}"
				", authorizationPresent: {}"
				", @MMS statistics@ - manageRequestDuration (millisecs): @{}@",
				_requestIdentifier, sThreadId, clientIPAddress, method, requestURI, authorizationPresent,
				chrono::duration_cast<chrono::milliseconds>(endManageRequest - startManageRequest).count()
			);
		}

        SPDLOG_DEBUG("FastCGIAPI::request finished"
			", _requestIdentifier: {}"
            ", threadId: {}", _requestIdentifier, sThreadId
        );

        FCGX_Finish_r(&request);

         // Note: the fcgi_streambuf destructor will auto flush
    }

	SPDLOG_INFO("FastCGIAPI SHUTDOWN"
		", threadId: {}", sThreadId
	);


    return 0;
}

bool FastCGIAPI::basicAuthenticationRequired(
    string requestURI,
    unordered_map<string, string> queryParameters
)
{
    bool        basicAuthenticationRequired = true;
    
	/*
    auto methodIt = queryParameters.find("method");
    if (methodIt == queryParameters.end())
    {
        string errorMessage = string("The 'method' parameter is not found");
        SPDLOG_ERROR(errorMessage);

        // throw runtime_error(errorMessage);
		return basicAuthenticationRequired;
    }
    string method = methodIt->second;

    if (method == "status"	// often used as healthy check
	)
    {
        basicAuthenticationRequired = false;
    }
	*/

    return basicAuthenticationRequired;
}

void FastCGIAPI::sendSuccess(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
	FCGX_Request& request,
	string requestURI, string requestMethod,
	int htmlResponseCode,
	string responseBody, string contentType,
	string cookieName, string cookieValue, string cookiePath,
	bool enableCorsGETHeader, string originHeader)
{
    string endLine = "\r\n";
    
    string httpStatus = fmt::format("Status: {} {}{}",
		htmlResponseCode, getHtmlStandardMessage(htmlResponseCode), endLine);

    string localContentType;
    if (responseBody != "")
	{
		if (contentType == "")
			localContentType = fmt::format("Content-Type: application/json; charset=utf-8{}", endLine);
		else
			localContentType = fmt::format("{}{}", contentType, endLine);
	}

	string cookieHeader;
	if (cookieName != "" && cookieValue != "")
	{
		cookieHeader = fmt::format("Set-Cookie: {}={}", cookieName, cookieValue);

		if (cookiePath != "")
			cookieHeader += ("; Path=" + cookiePath);

		cookieHeader += endLine;
	}

	string corsGETHeader;
	if (enableCorsGETHeader)
	{
		string origin = "*";
		if (originHeader != "")
			origin = originHeader;

		corsGETHeader = fmt::format("Access-Control-Allow-Origin: {}{}"
			"Access-Control-Allow-Methods: GET, POST, OPTIONS{}"
			"Access-Control-Allow-Credentials: true{}"
			"Access-Control-Allow-Headers: DNT,User-Agent,X-Requested-With,If-Modified-Since,Cache-Control,Content-Type,Range{}"
			"Access-Control-Expose-Headers: Content-Length,Content-Range{}",
			origin, endLine, endLine, endLine, endLine, endLine);
	}

	if (responseBodyCompressed)
	{
		string compressedResponseBody = Compressor::compress_string(responseBody);

		long contentLength = compressedResponseBody.size();

		string headResponse = fmt::format(
			"{}"
			"{}"
			"{}"
			"{}"
			"Content-Length: {}{}"
			"X-CompressedBody: true{}"
			"{}",
			httpStatus,
			localContentType,
			cookieHeader,
			corsGETHeader,
			contentLength, endLine,
			endLine,
			endLine
		);

		FCGX_FPrintF(request.out, headResponse.c_str());

		SPDLOG_INFO("sendSuccess"
			", requestIdentifier: {}"
			", threadId: {}"
			", requestURI: {}"
			", requestMethod: {}"
			", headResponse.size: {}"
			", responseBody.size: @{}@"
			", compressedResponseBody.size: @{}@",
			requestIdentifier, sThreadId, requestURI, requestMethod,
			headResponse.size(), responseBody.size(), contentLength
		);

		FCGX_PutStr(compressedResponseBody.data(), compressedResponseBody.size(),
			request.out);
    }
    else
	{
		string completeHttpResponse;

		// 2020-02-08: content length has to be calculated before the substitution from % to %%
		// because for FCGX_FPrintF (below used) %% is just one character
		long contentLength = responseBody.length();

		// responseBody cannot have the '%' char because FCGX_FPrintF will not work
		if (responseBody.find("%") != string::npos)
		{
			string toBeSearched = "%";
			string replacedWith = "%%";
			string newResponseBody = regex_replace(
				responseBody, regex(toBeSearched), replacedWith);

			completeHttpResponse = fmt::format(
				"{}"
				"{}"
				"{}"
				"{}"
				"Content-Length: {}{}"
				"{}"
				"{}",
				httpStatus, localContentType, cookieHeader,
				corsGETHeader,
				contentLength, endLine,
				endLine,
				newResponseBody);
		}
		else
		{
			completeHttpResponse = fmt::format(
				"{}"
				"{}"
				"{}"
				"{}"
				"Content-Length: {}{}"
				"{}"
				"{}",
				httpStatus, localContentType, cookieHeader,
				corsGETHeader,
				contentLength, endLine,
				endLine, responseBody);
		}

		SPDLOG_INFO("sendSuccess"
			", requestIdentifier: {}"
			", threadId: {}"
			", requestURI: {}"
			", requestMethod: {}"
			", responseBody.size: @{}@",
			requestIdentifier, sThreadId, requestURI, requestMethod, responseBody.size()
		);

		// si potrebbe usare anche FCGX_PutStr, in questo caso
		// non bisogna gestire %% (vedi sopra)
		// FCGX_PutStr(responseBody.data(), responseBody.size(), request.out);
		FCGX_FPrintF(request.out, completeHttpResponse.c_str());
	}
    
//    cout << completeHttpResponse;
}

void FastCGIAPI::sendRedirect(FCGX_Request& request, string locationURL)
{
    string endLine = "\r\n";
    
    int htmlResponseCode = 301;
    
    string completeHttpResponse = fmt::format("Status: {} {}{}"
		"Location: {}{}{}",
		htmlResponseCode, getHtmlStandardMessage(htmlResponseCode), endLine, locationURL, endLine, endLine);

    SPDLOG_INFO("HTTP Success"
		", response: {}", completeHttpResponse
    );

    FCGX_FPrintF(request.out, completeHttpResponse.c_str());
    // cout << completeHttpResponse;
}

void FastCGIAPI::sendHeadSuccess(FCGX_Request& request, int htmlResponseCode, unsigned long fileSize)
{
    string endLine = "\r\n";
    
    string httpStatus = fmt::format("Status: {} {}{}",
		htmlResponseCode, getHtmlStandardMessage(htmlResponseCode), endLine);

    string completeHttpResponse = fmt::format("{}"
		"Content-Range: bytes 0-{}{}{}",
		httpStatus, fileSize, endLine, endLine);

    SPDLOG_INFO("HTTP HEAD Success"
		", response: {}", completeHttpResponse
    );

    FCGX_FPrintF(request.out, completeHttpResponse.c_str());
}

void FastCGIAPI::sendHeadSuccess(int htmlResponseCode, unsigned long fileSize)
{
    string endLine = "\r\n";
    
    string httpStatus = fmt::format(
            "Status: {} {}{}",
            htmlResponseCode, getHtmlStandardMessage(htmlResponseCode), endLine);

    string completeHttpResponse = fmt::format(
			"{}"
            "X-CatraMMS-Resume: {}{}"
			"{}",
            httpStatus, fileSize, endLine, endLine);

    SPDLOG_INFO("HTTP HEAD Success"
		", response: {}", completeHttpResponse
    );
}

void FastCGIAPI::sendError(FCGX_Request& request, int htmlResponseCode, string errorMessage)
{
    string endLine = "\r\n";

	long contentLength;

    string responseBody;
	// errorMessage cannot have the '%' char because FCGX_FPrintF will not work
	if (errorMessage.find("%") != string::npos)
	{
		Json::Value temporaryResponseBodyRoot;
		temporaryResponseBodyRoot["status"] = to_string(htmlResponseCode);
		temporaryResponseBodyRoot["error"] = errorMessage;

		string temporaryResponseBody = JSONUtils::toString(temporaryResponseBodyRoot);

		// 2020-02-08: content length has to be calculated before the substitution from % to %%
		// because for FCGX_FPrintF (below used) %% is just one character
		contentLength = temporaryResponseBody.length();

		string toBeSearched = "%";
		string replacedWith = "%%";
		responseBody = regex_replace(
			temporaryResponseBody, regex(toBeSearched), replacedWith);
	}
	else
	{
		Json::Value responseBodyRoot;
		responseBodyRoot["status"] = to_string(htmlResponseCode);
		responseBodyRoot["error"] = errorMessage;

		responseBody = JSONUtils::toString(responseBodyRoot);

		// 2020-02-08: content length has to be calculated before the substitution from % to %%
		// because for FCGX_FPrintF (below used) %% is just one character
		contentLength = responseBody.length();
	}
    
    string httpStatus = fmt::format(
            "Status: {} {}{}",
            htmlResponseCode, getHtmlStandardMessage(htmlResponseCode), endLine);

    string completeHttpResponse = fmt::format(
			"{}"
            "Content-Type: application/json; charset=utf-8{}"
            "Content-Length: {}{}"
			"{}"
			"{}",
            httpStatus, endLine, contentLength, endLine, endLine, responseBody);
    
    SPDLOG_INFO("HTTP Error"
		", response: {}", completeHttpResponse
    );

    FCGX_FPrintF(request.out, completeHttpResponse.c_str());
}

void FastCGIAPI::sendError(int htmlResponseCode, string errorMessage)
{
    string endLine = "\r\n";

	long contentLength;

    string responseBody;
	// errorMessage cannot have the '%' char because FCGX_FPrintF will not work
	if (errorMessage.find("%") != string::npos)
	{
		Json::Value temporaryResponseBodyRoot;
		temporaryResponseBodyRoot["status"] = to_string(htmlResponseCode);
		temporaryResponseBodyRoot["error"] = errorMessage;

		string temporaryResponseBody = JSONUtils::toString(temporaryResponseBodyRoot);

		// 2020-02-08: content length has to be calculated before the substitution from % to %%
		// because for FCGX_FPrintF (below used) %% is just one character
		contentLength = temporaryResponseBody.length();

		string toBeSearched = "%";
		string replacedWith = "%%";
		responseBody = regex_replace(
			temporaryResponseBody, regex(toBeSearched), replacedWith);
	}
	else
	{
		Json::Value responseBodyRoot;
		responseBodyRoot["status"] = to_string(htmlResponseCode);
		responseBodyRoot["error"] = errorMessage;

		responseBody = JSONUtils::toString(responseBodyRoot);

		// 2020-02-08: content length has to be calculated before the substitution from % to %%
		// because for FCGX_FPrintF (below used) %% is just one character
		contentLength = responseBody.length();
	}
    
    string httpStatus = fmt::format(
            "Status: {} {}{}",
            htmlResponseCode, getHtmlStandardMessage(htmlResponseCode), endLine);

    string completeHttpResponse = fmt::format(
			"{}"
            "Content-Type: application/json; charset=utf-8{}"
            "Content-Length: {}{}"
			"{}"
			"{}",
            httpStatus, endLine, contentLength, endLine, endLine, responseBody);
    
    SPDLOG_INFO("HTTP Error"
		", response: {}", completeHttpResponse
    );
}

string FastCGIAPI::getClientIPAddress(
	unordered_map<string, string>& requestDetails
)
{

	string clientIPAddress;

	// REMOTE_ADDR is the address of the load balancer
	// auto remoteAddrIt = requestDetails.find("REMOTE_ADDR");
	auto remoteAddrIt = requestDetails.find("HTTP_X_FORWARDED_FOR");
	if (remoteAddrIt != requestDetails.end())
		clientIPAddress = remoteAddrIt->second;

	return clientIPAddress;
}

string FastCGIAPI::getHtmlStandardMessage(int htmlResponseCode)
{
    switch(htmlResponseCode)
    {
        case 200:
            return string("OK");
        case 201:
            return string("Created");
        case 301:
            return string("Moved Permanently");
        case 302:
            return string("Found");
        case 307:
            return string("Temporary Redirect");
        case 403:
            return string("Forbidden");
        case 400:
            return string("Bad Request");
        case 401:
            return string("Unauthorized");
        case 500:
            return string("Internal Server Error");
        default:
            string errorMessage = fmt::format("HTTP status code not managed"
                ", htmlResponseCode: {}", htmlResponseCode);
            SPDLOG_ERROR(errorMessage);
            
            throw runtime_error(errorMessage);
    }
    
}

void FastCGIAPI::fillEnvironmentDetails(
        const char * const * envp, 
        unordered_map<string, string>& requestDetails)
{

    int valueIndex;

    for ( ; *envp; ++envp)
    {
        string environmentKeyValue = *envp;

        if ((valueIndex = environmentKeyValue.find("=")) == string::npos)
        {
            SPDLOG_ERROR("Unexpected environment variable"
				", environmentKeyValue: {}", environmentKeyValue
            );
            
            continue;
        }

        string key = environmentKeyValue.substr(0, valueIndex);
        string value = environmentKeyValue.substr(valueIndex + 1);
        
        requestDetails[key] = value;

        if (key == "REQUEST_URI")
            SPDLOG_DEBUG("Environment variable"
				", key/Name: {}={}", key, value
            );
        else
            SPDLOG_DEBUG("Environment variable"
				", key/Name: {}={}", key, value
            );
    }
}

void FastCGIAPI::fillQueryString(
        string queryString,
        unordered_map<string, string>& queryParameters)
{

    stringstream ss(queryString);
    string token;
    char delim = '&';
    while (getline(ss, token, delim)) 
    {
        if (!token.empty())
        {
            size_t keySeparator;
            
            if ((keySeparator = token.find("=")) == string::npos)
            {
                SPDLOG_ERROR("Wrong query parameter format"
					", token: {}", token
                );
                
                continue;
            }

            string key = token.substr(0, keySeparator);
            string value = token.substr(keySeparator + 1);
            
            queryParameters[key] = value;

            SPDLOG_DEBUG("Query parameter"
				", key/Name: {}={}", key, value
            );
        }
    }    
}

Json::Value FastCGIAPI::loadConfigurationFile(const char* configurationPathName)
{
    Json::Value configurationJson;

    try
    {
        ifstream configurationFile(configurationPathName, ifstream::binary);
        configurationFile >> configurationJson;
    }
    catch(...)
    {
        cerr << fmt::format("wrong json configuration format"
                ", configurationPathName: {}", configurationPathName)
            << endl;
    }

    return configurationJson;
}

string FastCGIAPI::base64_encode(const string& in)
{
	string out;

	int val=0, valb=-6;
	for (unsigned char c : in) {
		val = (val<<8) + c;
		valb += 8;
		while (valb>=0) {
			out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val>>valb)&0x3F]);
			valb-=6;
		}
	}
	if (valb>-6) out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val<<8)>>(valb+8))&0x3F]);
	while (out.size()%4) out.push_back('=');
	return out;
}

string FastCGIAPI::base64_decode(const string &in)
{
	string out;

	vector<int> T(256,-1);
	for (int i=0; i<64; i++) T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;

	int val=0, valb=-8;
	for (unsigned char c : in) {
		if (T[c] == -1) break;
		val = (val<<6) + T[c];
		valb += 6;
		if (valb>=0) {
			out.push_back(char((val>>valb)&0xFF));
			valb-=8;
		}
	}
	return out;
}

