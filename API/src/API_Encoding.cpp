/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   API.cpp
 * Author: giuliano
 * 
 * Created on February 18, 2018, 1:27 AM
 */

#include "JSONUtils.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include "Validator.h"
#include "catralibraries/Convert.h"
#include "API.h"


void API::encodingJobsStatus(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters,
        string requestBody)
{
    string api = "encodingJobsStatus";

    _logger->info(__FILEREF__ + "Received " + api
        + ", requestBody: " + requestBody
    );

    try
    {
        int64_t encodingJobKey = -1;
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt != queryParameters.end() && encodingJobKeyIt->second != "")
        {
            encodingJobKey = stoll(encodingJobKeyIt->second);
        }

        int start = 0;
        auto startIt = queryParameters.find("start");
        if (startIt != queryParameters.end() && startIt->second != "")
        {
            start = stoll(startIt->second);
        }

        int rows = 10;
        auto rowsIt = queryParameters.find("rows");
        if (rowsIt != queryParameters.end() && rowsIt->second != "")
        {
            rows = stoll(rowsIt->second);
			if (rows > _maxPageSize)
			{
				// 2022-02-13: changed to return an error otherwise the user
				//	think to ask for a huge number of items while the return is much less

				// rows = _maxPageSize;

				string errorMessage = __FILEREF__ + "rows parameter too big"
					+ ", rows: " + to_string(rows)
					+ ", _maxPageSize: " + to_string(_maxPageSize)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
        }
        
		/*
        bool startAndEndIngestionDatePresent = false;
        string startIngestionDate;
        string endIngestionDate;
        auto startIngestionDateIt = queryParameters.find("startIngestionDate");
        auto endIngestionDateIt = queryParameters.find("endIngestionDate");
        if (startIngestionDateIt != queryParameters.end()
			|| endIngestionDateIt != queryParameters.end())
        {
			if (startIngestionDateIt != queryParameters.end())
				startIngestionDate = startIngestionDateIt->second;
			else
			{
				tm tmUTCDateTime;
				char sUTCDateTime[64];

				chrono::system_clock::time_point now = chrono::system_clock::now();
				time_t utcNow  = chrono::system_clock::to_time_t(now);

				gmtime_r (&utcNow, &tmUTCDateTime);
				sprintf (sUTCDateTime, "%04d-%02d-%02dT%02d:%02d:%02dZ",
					tmUTCDateTime. tm_year + 1900,
					tmUTCDateTime. tm_mon + 1,
					tmUTCDateTime. tm_mday,
					tmUTCDateTime. tm_hour,
					tmUTCDateTime. tm_min,
					tmUTCDateTime. tm_sec);

				startIngestionDate = sUTCDateTime;
			}

			if (endIngestionDateIt != queryParameters.end())
				endIngestionDate = endIngestionDateIt->second;
			else
			{
				tm tmUTCDateTime;
				char sUTCDateTime[64];

				chrono::system_clock::time_point now = chrono::system_clock::now();
				time_t utcNow  = chrono::system_clock::to_time_t(now);

				gmtime_r (&utcNow, &tmUTCDateTime);
				sprintf (sUTCDateTime, "%04d-%02d-%02dT%02d:%02d:%02dZ",
					tmUTCDateTime. tm_year + 1900,
					tmUTCDateTime. tm_mon + 1,
					tmUTCDateTime. tm_mday,
					tmUTCDateTime. tm_hour,
					tmUTCDateTime. tm_min,
					tmUTCDateTime. tm_sec);

				endIngestionDate = sUTCDateTime;
			}

            startAndEndIngestionDatePresent = true;
        }
		*/
        string startIngestionDate;
        auto startIngestionDateIt = queryParameters.find("startIngestionDate");
        if (startIngestionDateIt != queryParameters.end())
			startIngestionDate = startIngestionDateIt->second;

        string endIngestionDate;
        auto endIngestionDateIt = queryParameters.find("endIngestionDate");
        if (endIngestionDateIt != queryParameters.end())
			endIngestionDate = endIngestionDateIt->second;

		/*
        bool startAndEndEncodingDatePresent = false;
        string startEncodingDate;
        string endEncodingDate;
        auto startEncodingDateIt = queryParameters.find("startEncodingDate");
        auto endEncodingDateIt = queryParameters.find("endEncodingDate");
        if (startEncodingDateIt != queryParameters.end()
			|| endEncodingDateIt != queryParameters.end())
        {
			if (startEncodingDateIt != queryParameters.end())
				startEncodingDate = startEncodingDateIt->second;
			else
			{
				tm tmUTCDateTime;
				char sUTCDateTime[64];

				chrono::system_clock::time_point now = chrono::system_clock::now();
				time_t utcNow  = chrono::system_clock::to_time_t(now);

				gmtime_r (&utcNow, &tmUTCDateTime);
				sprintf (sUTCDateTime, "%04d-%02d-%02dT%02d:%02d:%02dZ",
					tmUTCDateTime. tm_year + 1900,
					tmUTCDateTime. tm_mon + 1,
					tmUTCDateTime. tm_mday,
					tmUTCDateTime. tm_hour,
					tmUTCDateTime. tm_min,
					tmUTCDateTime. tm_sec);

				startEncodingDate = sUTCDateTime;
			}

			if (endEncodingDateIt != queryParameters.end())
				endEncodingDate = endEncodingDateIt->second;
			else
			{
				tm tmUTCDateTime;
				char sUTCDateTime[64];

				chrono::system_clock::time_point now = chrono::system_clock::now();
				time_t utcNow  = chrono::system_clock::to_time_t(now);

				gmtime_r (&utcNow, &tmUTCDateTime);
				sprintf (sUTCDateTime, "%04d-%02d-%02dT%02d:%02d:%02dZ",
					tmUTCDateTime. tm_year + 1900,
					tmUTCDateTime. tm_mon + 1,
					tmUTCDateTime. tm_mday,
					tmUTCDateTime. tm_hour,
					tmUTCDateTime. tm_min,
					tmUTCDateTime. tm_sec);

				endEncodingDate = sUTCDateTime;
			}

            startAndEndEncodingDatePresent = true;
        }
		*/
        string startEncodingDate;
        auto startEncodingDateIt = queryParameters.find("startEncodingDate");
        if (startEncodingDateIt != queryParameters.end())
			startEncodingDate = startEncodingDateIt->second;

        string endEncodingDate;
        auto endEncodingDateIt = queryParameters.find("endEncodingDate");
        if (endEncodingDateIt != queryParameters.end())
			endEncodingDate = endEncodingDateIt->second;

        int64_t encoderKey = -1;
        auto encoderKeyIt = queryParameters.find("encoderKey");
        if (encoderKeyIt != queryParameters.end() && encoderKeyIt->second != "")
        {
            encoderKey = stoll(encoderKeyIt->second);
        }

        bool alsoEncodingJobsFromOtherWorkspaces = false;
        auto alsoEncodingJobsFromOtherWorkspacesIt = queryParameters.find("alsoEncodingJobsFromOtherWorkspaces");
        if (alsoEncodingJobsFromOtherWorkspacesIt != queryParameters.end()
			&& alsoEncodingJobsFromOtherWorkspacesIt->second != "")
        {
            if (alsoEncodingJobsFromOtherWorkspacesIt->second == "true")
                alsoEncodingJobsFromOtherWorkspaces = true;
            else
                alsoEncodingJobsFromOtherWorkspaces = false;
        }

        bool asc = true;
        auto ascIt = queryParameters.find("asc");
        if (ascIt != queryParameters.end() && ascIt->second != "")
        {
            if (ascIt->second == "true")
                asc = true;
            else
                asc = false;
        }

        string status = "all";
        auto statusIt = queryParameters.find("status");
        if (statusIt != queryParameters.end() && statusIt->second != "")
        {
            status = statusIt->second;
        }

        string types = "";
        auto typesIt = queryParameters.find("types");
        if (typesIt != queryParameters.end() && typesIt->second != "")
        {
            types = typesIt->second;

			// 2021-01-07: Remark: we have FIRST to replace + in space and then apply curlpp::unescape
			//	That  because if we have really a + char (%2B into the string), and we do the replace
			//	after curlpp::unescape, this char will be changed to space and we do not want it
			string plus = "\\+";
			string plusDecoded = " ";
			string firstDecoding = regex_replace(types, regex(plus), plusDecoded);

			types = curlpp::unescape(firstDecoding);
        }

        {
            Json::Value encodingStatusRoot = _mmsEngineDBFacade->getEncodingJobsStatus(
                    workspace, encodingJobKey,
                    start, rows,
                    // startAndEndIngestionDatePresent,
					startIngestionDate, endIngestionDate,
                    // startAndEndEncodingDatePresent,
					startEncodingDate, endEncodingDate,
					encoderKey, alsoEncodingJobsFromOtherWorkspaces,
                    asc, status, types
                    );

            Json::StreamWriterBuilder wbuilder;
            string responseBody = Json::writeString(wbuilder, encodingStatusRoot);
            
            sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
				request, "", api, 200, responseBody);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::encodingJobPriority(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters,
        string requestBody)
{
    string api = "encodingJobPriority";

    _logger->info(__FILEREF__ + "Received " + api
        + ", requestBody: " + requestBody
    );

    try
    {
        int64_t encodingJobKey = -1;
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt != queryParameters.end() && encodingJobKeyIt->second != "")
        {
            encodingJobKey = stoll(encodingJobKeyIt->second);
        }

        MMSEngineDBFacade::EncodingPriority newEncodingJobPriority;
        bool newEncodingJobPriorityPresent = false;
        auto newEncodingJobPriorityCodeIt = queryParameters.find("newEncodingJobPriorityCode");
        if (newEncodingJobPriorityCodeIt != queryParameters.end() && newEncodingJobPriorityCodeIt->second != "")
        {
            newEncodingJobPriority = static_cast<MMSEngineDBFacade::EncodingPriority>(stoll(newEncodingJobPriorityCodeIt->second));
            newEncodingJobPriorityPresent = true;
        }

        bool tryEncodingAgain = false;
        auto tryEncodingAgainIt = queryParameters.find("tryEncodingAgain");
        if (tryEncodingAgainIt != queryParameters.end())
        {
            if (tryEncodingAgainIt->second == "false")
                tryEncodingAgain = false;
            else
                tryEncodingAgain = true;
        }

        {
            if (newEncodingJobPriorityPresent)
            {
                _mmsEngineDBFacade->updateEncodingJobPriority(
                    workspace, encodingJobKey, newEncodingJobPriority);
            }
            
            if (tryEncodingAgain)
            {
                _mmsEngineDBFacade->updateEncodingJobTryAgain(
                    workspace, encodingJobKey);
            }
            
            if (!newEncodingJobPriorityPresent && !tryEncodingAgain)
            {
                _logger->warn(__FILEREF__ + "Useless API call, no encoding update was done"
                    + ", newEncodingJobPriorityPresent: " + to_string(newEncodingJobPriorityPresent)
                    + ", tryEncodingAgain: " + to_string(tryEncodingAgain)
                );
            }

            string responseBody;
            
            sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
				request, "", api, 200, responseBody);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::killOrCancelEncodingJob(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
	FCGX_Request& request,
	shared_ptr<Workspace> workspace,
	unordered_map<string, string> queryParameters,
	string requestBody)
{
	string api = "killOrCancelEncodingJob";

    _logger->info(__FILEREF__ + "Received " + api
        + ", requestBody: " + requestBody
    );

    try
    {
		int64_t encodingJobKey = -1;
		auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
		if (encodingJobKeyIt != queryParameters.end() && encodingJobKeyIt->second != "")
			encodingJobKey = stoll(encodingJobKeyIt->second);

		bool lightKill = false;
		auto lightKillIt = queryParameters.find("lightKill");
		if (lightKillIt != queryParameters.end() && lightKillIt->second != "")
			lightKill = lightKillIt->second == "true" ? true : false;

        {
			tuple<int64_t, string, int64_t, MMSEngineDBFacade::EncodingStatus, string>
				encodingJobDetails = _mmsEngineDBFacade->getEncodingJobDetails(
					encodingJobKey);

			int64_t ingestionJobKey;
			string type;
			int64_t encoderKey;
			MMSEngineDBFacade::EncodingStatus status;

			tie(ingestionJobKey, type, encoderKey, status, ignore) = encodingJobDetails;

			_logger->info(__FILEREF__ + "getEncodingJobDetails"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", type: " + type
				+ ", encoderKey: " + to_string(encoderKey)
				+ ", status: " + MMSEngineDBFacade::toString(status)
			);

			if (type == "LiveRecorder")
			{
				{
					if (status == MMSEngineDBFacade::EncodingStatus::Processing)
					{
						// In this case we may have 2 scenarios:
						// 1. process (ffmpeg) is running
						// 2. process (ffmpeg) fails to run and we have the Task in the loop
						//		within EncoderVideoAudioProxy trying to make ffmpeg starting calling the Transcoder.
						//
						// In case 1, the below killEncodingJob works fine and this is the solution
						// In case 2, killEncodingJob will fail because there is no ffmpeg process running.
						//		For this reason we call updateEncodingJobIsKilled and set isKilled
						//		to true. This is an 'aggreement' with EncoderVideoAudioProxy making
						//		the EncoderVideoAudioProxy thread to terminate his loop 
						try
						{
							_logger->info(__FILEREF__ + "killEncodingJob"
								+ ", encoderKey: " + to_string(encoderKey)
								+ ", encodingJobKey: " + to_string(encodingJobKey)
							);
							killEncodingJob(encoderKey, encodingJobKey, lightKill);

							// to make sure EncoderVideoProxyThread resources are released,
							// the isKilled flag is also set
							if (!lightKill)
							{
								// this is the case 2
								bool isKilled = true;

								_logger->info(__FILEREF__ + "Setting isKilled flag"
									+ ", encodingJobKey: " + to_string(encodingJobKey)
									+ ", isKilled: " + to_string(isKilled)
								);
								_mmsEngineDBFacade->updateEncodingJobIsKilled(
									encodingJobKey, isKilled);
							}
						}
						catch (...)
						{
							// this is the case 2
							if (!lightKill)
							{
								bool isKilled = true;

								_logger->info(__FILEREF__ + "Setting isKilled flag"
									+ ", encodingJobKey: " + to_string(encodingJobKey)
									+ ", isKilled: " + to_string(isKilled)
								);
								_mmsEngineDBFacade->updateEncodingJobIsKilled(
									encodingJobKey, isKilled);

								/* 2022-09-03: it should be needed anymore
								_logger->info(__FILEREF__ + "killEncodingJob failed, force update of the status"
									+ ", encoderKey: " + to_string(encoderKey)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", encodingJobKey: " + to_string(encodingJobKey)
								);

								{
									_logger->info(__FILEREF__ + "updateEncodingJob KilledByUser"                                
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)                         
										+ ", encodingJobKey: " + to_string(encodingJobKey)                           
									);

									_mmsEngineDBFacade->updateEncodingJob (encodingJobKey,
										MMSEngineDBFacade::EncodingError::KilledByUser,
										false,  // isIngestionJobFinished: this field is not used by updateEncodingJob
										ingestionJobKey, "killEncodingJob failed");
								}
								*/
							}
						}
					}
				}
			}
			else if (type == "LiveProxy" || type == "VODProxy" || type == "Countdown")
			{
				if (status == MMSEngineDBFacade::EncodingStatus::Processing)
				{
					// In this case we may have 2 scenarios:
					// 1. process (ffmpeg) is running
					// 2. process (ffmpeg) fails to run and we have the Task in the loop
					//		within EncoderVideoAudioProxy trying to make ffmpeg starting calling the Transcoder.
					//
					// In case 1, the below killEncodingJob works fine and this is the solution
					// In case 2, killEncodingJob will fail because there is no ffmpeg process running.
					//		For this reason we call updateEncodingJobIsKilled and set isKilled
					//		to true. This is an 'aggreement' with EncoderVideoAudioProxy making
					//		the EncoderVideoAudioProxy thread to terminate his loop 

					try
					{
						_logger->info(__FILEREF__ + "killEncodingJob"
							+ ", encoderKey: " + to_string(encoderKey)
							+ ", encodingJobKey: " + to_string(encodingJobKey)
						);
						killEncodingJob(encoderKey, encodingJobKey, lightKill);

						// to make sure EncoderVideoProxyThread resources are released,
						// the isKilled flag is also set
						if (!lightKill)
						{
							// this is the case 2
							bool isKilled = true;

							_logger->info(__FILEREF__ + "Setting isKilled flag"
								+ ", encodingJobKey: " + to_string(encodingJobKey)
								+ ", isKilled: " + to_string(isKilled)
							);
							_mmsEngineDBFacade->updateEncodingJobIsKilled(
								encodingJobKey, isKilled);
						}
					}
					catch(runtime_error e)
					{
						// this is the case 2
						if (!lightKill)
						{
							bool isKilled = true;

							_logger->info(__FILEREF__ + "Setting isKilled flag"
								+ ", encodingJobKey: " + to_string(encodingJobKey)
								+ ", isKilled: " + to_string(isKilled)
							);
							_mmsEngineDBFacade->updateEncodingJobIsKilled(
								encodingJobKey, isKilled);
						}
					}
				}
				else if (status == MMSEngineDBFacade::EncodingStatus::ToBeProcessed)
				{
					if (!lightKill)
					{
						MMSEngineDBFacade::EncodingError encodingError
							= MMSEngineDBFacade::EncodingError::CanceledByUser;
						_mmsEngineDBFacade->updateEncodingJob(
							encodingJobKey, encodingError,
							false,  // isIngestionJobFinished: this field is not used by updateEncodingJob
							ingestionJobKey, "Canceled By User");
					}
				}
			}
			else
			{
				if (status == MMSEngineDBFacade::EncodingStatus::Processing)
				{
					_logger->info(__FILEREF__ + "killEncodingJob"
						+ ", encoderKey: " + to_string(encoderKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
					);
					killEncodingJob(encoderKey, encodingJobKey, lightKill);
				}
				else if (status == MMSEngineDBFacade::EncodingStatus::ToBeProcessed)
				{
					if (!lightKill)
					{
						MMSEngineDBFacade::EncodingError encodingError
							= MMSEngineDBFacade::EncodingError::CanceledByUser;
						_mmsEngineDBFacade->updateEncodingJob(
							encodingJobKey, encodingError,
							false,  // isIngestionJobFinished: this field is not used by updateEncodingJob
							ingestionJobKey, "Canceled By User");
					}
				}
			}

            string responseBody;
            sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
				request, "", api, 200, responseBody);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::encodingProfilesSetsList(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters)
{
    string api = "encodingProfilesSetsList";

    _logger->info(__FILEREF__ + "Received " + api
    );

    try
    {
        int64_t encodingProfilesSetKey = -1;
        auto encodingProfilesSetKeyIt = queryParameters.find("encodingProfilesSetKey");
        if (encodingProfilesSetKeyIt != queryParameters.end() && encodingProfilesSetKeyIt->second != "")
        {
            encodingProfilesSetKey = stoll(encodingProfilesSetKeyIt->second);
        }

        bool contentTypePresent = false;
        MMSEngineDBFacade::ContentType contentType;
        auto contentTypeIt = queryParameters.find("contentType");
        if (contentTypeIt != queryParameters.end() && contentTypeIt->second != "")
        {
            contentType = MMSEngineDBFacade::toContentType(contentTypeIt->second);
            
            contentTypePresent = true;
        }
        
        {
            
            Json::Value encodingProfilesSetListRoot = _mmsEngineDBFacade->getEncodingProfilesSetList(
                    workspace->_workspaceKey, encodingProfilesSetKey,
                    contentTypePresent, contentType);

            Json::StreamWriterBuilder wbuilder;
            string responseBody = Json::writeString(wbuilder, encodingProfilesSetListRoot);
            
            sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
				request, "", api, 200, responseBody);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::encodingProfilesList(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters)
{
    string api = "encodingProfilesList";

    _logger->info(__FILEREF__ + "Received " + api
    );

    try
    {
        int64_t encodingProfileKey = -1;
        auto encodingProfileKeyIt = queryParameters.find("encodingProfileKey");
        if (encodingProfileKeyIt != queryParameters.end() && encodingProfileKeyIt->second != "")
        {
            encodingProfileKey = stoll(encodingProfileKeyIt->second);
        }

        bool contentTypePresent = false;
        MMSEngineDBFacade::ContentType contentType;
        auto contentTypeIt = queryParameters.find("contentType");
        if (contentTypeIt != queryParameters.end() && contentTypeIt->second != "")
        {
            contentType = MMSEngineDBFacade::toContentType(contentTypeIt->second);
            
            contentTypePresent = true;
        }

        string label;
        auto labelIt = queryParameters.find("label");
        if (labelIt != queryParameters.end() && labelIt->second != "")
        {
			label = labelIt->second;

			// 2021-01-07: Remark: we have FIRST to replace + in space and then apply curlpp::unescape
			//	That  because if we have really a + char (%2B into the string), and we do the replace
			//	after curlpp::unescape, this char will be changed to space and we do not want it
			string plus = "\\+";
			string plusDecoded = " ";
			string firstDecoding = regex_replace(label, regex(plus), plusDecoded);

			label = curlpp::unescape(firstDecoding);
        }

        {
            Json::Value encodingProfileListRoot = _mmsEngineDBFacade->getEncodingProfileList(
                    workspace->_workspaceKey, encodingProfileKey,
                    contentTypePresent, contentType, label);

            Json::StreamWriterBuilder wbuilder;
            string responseBody = Json::writeString(wbuilder, encodingProfileListRoot);
            
            sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
				request, "", api, 200, responseBody);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::addEncodingProfilesSet(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters,
        string requestBody)
{
    string api = "addEncodingProfilesSet";

    _logger->info(__FILEREF__ + "Received " + api
        + ", requestBody: " + requestBody
    );

    try
    {
        auto sContentTypeIt = queryParameters.find("contentType");
        if (sContentTypeIt == queryParameters.end())
        {
            string errorMessage = string("'contentType' URI parameter is missing");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);            
        }
        MMSEngineDBFacade::ContentType contentType = 
                MMSEngineDBFacade::toContentType(sContentTypeIt->second);
        
        Json::Value encodingProfilesSetRoot;
        try
        {
            {
                Json::CharReaderBuilder builder;
                Json::CharReader* reader = builder.newCharReader();
                string errors;

                bool parsingSuccessful = reader->parse(requestBody.c_str(),
                        requestBody.c_str() + requestBody.size(), 
                        &encodingProfilesSetRoot, &errors);
                delete reader;

                if (!parsingSuccessful)
                {
                    string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                            + ", errors: " + errors
                            + ", requestBody: " + requestBody
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
        }
        catch(runtime_error e)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", requestBody: " + requestBody
                    + ", e.what(): " + e.what()
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        catch(exception e)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }

        string responseBody;    
        shared_ptr<MySQLConnection> conn;

        try
        {            
            conn = _mmsEngineDBFacade->beginIngestionJobs();

            Validator validator(_logger, _mmsEngineDBFacade, _configuration);
            validator.validateEncodingProfilesSetRootMetadata(contentType, encodingProfilesSetRoot);
        
            string field = "Label";
            if (!JSONUtils::isMetadataPresent(encodingProfilesSetRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string label = encodingProfilesSetRoot.get(field, "").asString();

            int64_t encodingProfilesSetKey = _mmsEngineDBFacade->addEncodingProfilesSet(conn,
                    workspace->_workspaceKey, contentType, label);

			field = "Profiles";
			Json::Value profilesRoot = encodingProfilesSetRoot[field];

            for (int profileIndex = 0; profileIndex < profilesRoot.size(); profileIndex++)
            {
                string profileLabel = profilesRoot[profileIndex].asString();
                
                int64_t encodingProfileKey = _mmsEngineDBFacade->addEncodingProfileIntoSet(
                        conn, workspace->_workspaceKey, profileLabel,
                        contentType, encodingProfilesSetKey);

                if (responseBody != "")
                    responseBody += string(", ");
                responseBody += (
                        string("{ ") 
                        + "\"encodingProfileKey\": " + to_string(encodingProfileKey)
                        + ", \"label\": \"" + profileLabel + "\" "
                        + "}"
                        );
            }

            bool commit = true;
            _mmsEngineDBFacade->endIngestionJobs(conn, commit, -1, string());
            
            string beginOfResponseBody = string("{ ")
                + "\"encodingProfilesSet\": { "
                    + "\"encodingProfilesSetKey\": " + to_string(encodingProfilesSetKey)
                    + ", \"label\": \"" + label + "\" "
                    + "}, "
                    + "\"profiles\": [ ";
            responseBody.insert(0, beginOfResponseBody);
            responseBody += " ] }";
        }
        catch(runtime_error e)
        {
            bool commit = false;
            _mmsEngineDBFacade->endIngestionJobs(conn, commit, -1, string());

            _logger->error(__FILEREF__ + "request body parsing failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }
        catch(exception e)
        {
            bool commit = false;
            _mmsEngineDBFacade->endIngestionJobs(conn, commit, -1, string());

            _logger->error(__FILEREF__ + "request body parsing failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }

        sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
			request, "", api, 201, responseBody);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::addEncodingProfile(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters,
        string requestBody)
{
    string api = "addEncodingProfile";

    _logger->info(__FILEREF__ + "Received " + api
        + ", requestBody: " + requestBody
    );

    try
    {
        auto sContentTypeIt = queryParameters.find("contentType");
        if (sContentTypeIt == queryParameters.end())
        {
            string errorMessage = string("'contentType' URI parameter is missing");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);            
        }
        MMSEngineDBFacade::ContentType contentType = 
                MMSEngineDBFacade::toContentType(sContentTypeIt->second);
        
        Json::Value encodingProfileRoot;
        try
        {
            {
                Json::CharReaderBuilder builder;
                Json::CharReader* reader = builder.newCharReader();
                string errors;

                bool parsingSuccessful = reader->parse(requestBody.c_str(),
                        requestBody.c_str() + requestBody.size(), 
                        &encodingProfileRoot, &errors);
                delete reader;

                if (!parsingSuccessful)
                {
                    string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                            + ", errors: " + errors
                            + ", requestBody: " + requestBody
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
        }
        catch(runtime_error e)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", requestBody: " + requestBody
                    + ", e.what(): " + e.what()
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        catch(exception e)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }

        string responseBody;    

        try
        {
            Validator validator(_logger, _mmsEngineDBFacade, _configuration);
            validator.validateEncodingProfileRootMetadata(contentType, encodingProfileRoot);

            string field = "Label";
            if (!JSONUtils::isMetadataPresent(encodingProfileRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string profileLabel = encodingProfileRoot.get(field, "XXX").asString();

            MMSEngineDBFacade::DeliveryTechnology deliveryTechnology;
			{
				field = "FileFormat";
				string fileFormat = encodingProfileRoot.get(field, "").asString();

				deliveryTechnology = MMSEngineDBFacade::fileFormatToDeliveryTechnology(fileFormat);
				/*
				string fileFormatLowerCase;
				fileFormatLowerCase.resize(fileFormat.size());
				transform(fileFormat.begin(), fileFormat.end(),
					fileFormatLowerCase.begin(), [](unsigned char c){return tolower(c); } );

				if (fileFormatLowerCase == "hls"
					|| fileFormatLowerCase == "dash"
				)
					deliveryTechnology = MMSEngineDBFacade::DeliveryTechnology::HTTPStreaming;
				else if (fileFormatLowerCase == "mp4"
					|| fileFormatLowerCase == "mkv"
					|| fileFormatLowerCase == "mov"
				)
					deliveryTechnology = MMSEngineDBFacade::DeliveryTechnology::DownloadAndStreaming;
				else
					deliveryTechnology = MMSEngineDBFacade::DeliveryTechnology::Download;
				*/

				_logger->info(__FILEREF__ + "deliveryTechnology"
					+ ", fileFormat: " + fileFormat
					// + ", fileFormatLowerCase: " + fileFormatLowerCase
					+ ", deliveryTechnology: " + MMSEngineDBFacade::toString(deliveryTechnology)
				);
			}

            string jsonEncodingProfile;
            {
                Json::StreamWriterBuilder wbuilder;

                jsonEncodingProfile = Json::writeString(wbuilder, encodingProfileRoot);
            }
            
            int64_t encodingProfileKey = _mmsEngineDBFacade->addEncodingProfile(
                workspace->_workspaceKey, profileLabel,
                contentType, deliveryTechnology, jsonEncodingProfile);

            responseBody = (
                    string("{ ") 
                    + "\"encodingProfileKey\": " + to_string(encodingProfileKey)
                    + ", \"label\": \"" + profileLabel + "\" "
                    + "}"
                    );            
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->addEncodingProfile failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->addEncodingProfile failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }

        sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
			request, "", api, 201, responseBody);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::removeEncodingProfile(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters)
{
    string api = "removeEncodingProfile";

    _logger->info(__FILEREF__ + "Received " + api
    );

    try
    {
        auto encodingProfileKeyIt = queryParameters.find("encodingProfileKey");
        if (encodingProfileKeyIt == queryParameters.end())
        {
            string errorMessage = string("'encodingProfileKey' URI parameter is missing");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);            
        }
        int64_t encodingProfileKey = stoll(encodingProfileKeyIt->second);
        
        try
        {
            _mmsEngineDBFacade->removeEncodingProfile(
                workspace->_workspaceKey, encodingProfileKey);
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->removeEncodingProfile failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->removeEncodingProfile failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }

        string responseBody;
        
        sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
			request, "", api, 200, responseBody);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::removeEncodingProfilesSet(
	string sThreadId, int64_t requestIdentifier, bool responseBodyCompressed,
        FCGX_Request& request,
        shared_ptr<Workspace> workspace,
        unordered_map<string, string> queryParameters)
{
    string api = "removeEncodingProfilesSet";

    _logger->info(__FILEREF__ + "Received " + api
    );

    try
    {
        auto encodingProfilesSetKeyIt = queryParameters.find("encodingProfilesSetKey");
        if (encodingProfilesSetKeyIt == queryParameters.end())
        {
            string errorMessage = string("'encodingProfilesSetKey' URI parameter is missing");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);            
        }
        int64_t encodingProfilesSetKey = stoll(encodingProfilesSetKeyIt->second);
        
        try
        {
            _mmsEngineDBFacade->removeEncodingProfilesSet(
                workspace->_workspaceKey, encodingProfilesSetKey);
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->removeEncodingProfilesSet failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->removeEncodingProfilesSet failed"
                + ", e.what(): " + e.what()
            );

            throw e;
        }

        string responseBody;
        
        sendSuccess(sThreadId, requestIdentifier, responseBodyCompressed,
			request, "", api, 200, responseBody);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error: ") + e.what();
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "API failed"
            + ", API: " + api
            + ", e.what(): " + e.what()
        );

        string errorMessage = string("Internal server error");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 500, errorMessage);

        throw runtime_error(errorMessage);
    }
}

void API::killEncodingJob(int64_t encoderKey, int64_t encodingJobKey, bool lightKill)
{
	string ffmpegEncoderURL;
	ostringstream response;
	try
	{
		pair<string, bool> encoderDetails = _mmsEngineDBFacade->getEncoderURL(encoderKey);
		string transcoderHost;
		tie(transcoderHost, ignore) = encoderDetails;

		// ffmpegEncoderURL = _ffmpegEncoderProtocol
		// 	+ "://"
		// 	+ transcoderHost + ":"
		// 	+ to_string(_ffmpegEncoderPort)
		ffmpegEncoderURL = 
			transcoderHost
			+ _ffmpegEncoderKillEncodingURI
			+ "/" + to_string(encodingJobKey)
			+ "?lightKill=" + (lightKill ? "true" : "false)
		;

		list<string> header;

		{
			string userPasswordEncoded = Convert::base64_encode(
				_ffmpegEncoderUser + ":" + _ffmpegEncoderPassword);
			string basicAuthorization = string("Authorization: Basic ")
				+ userPasswordEncoded;

			header.push_back(basicAuthorization);
		}
            
		curlpp::Cleanup cleaner;
		curlpp::Easy request;

		// Setting the URL to retrive.
		request.setOpt(new curlpp::options::Url(ffmpegEncoderURL));
		request.setOpt(new curlpp::options::CustomRequest("DELETE"));

		// timeout consistent with nginx configuration (fastcgi_read_timeout)
		request.setOpt(new curlpp::options::Timeout(_ffmpegEncoderTimeoutInSeconds));

		// if (_ffmpegEncoderProtocol == "https")
		string httpsPrefix("https");
		if (ffmpegEncoderURL.size() >= httpsPrefix.size()
			&& 0 == ffmpegEncoderURL.compare(0, httpsPrefix.size(), httpsPrefix))
		{
			/*
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;
			typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;
			typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;
			typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;
			typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;
			typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;
			typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;
			typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;
			*/
                                                                                                
              
			/*
			// cert is stored PEM coded in file... 
			// since PEM is default, we needn't set it for PEM 
			// curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
			curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
			equest.setOpt(sslCertType);

			// set the cert for client authentication
			// "testcert.pem"
			// curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
			curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
			request.setOpt(sslCert);
			*/

			/*
			// sorry, for engine we must set the passphrase
			//   (if the key has one...)
			// const char *pPassphrase = NULL;
			if(pPassphrase)
			curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

			// if we use a key stored in a crypto engine,
			//   we must set the key type to "ENG"
			// pKeyType  = "PEM";
			curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

			// set the private key (file or ID in engine)
			// pKeyName  = "testkey.pem";
			curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

			// set the file with the certs vaildating the server
			// *pCACertFile = "cacert.pem";
			curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);
			*/
              
			// disconnect if we can't validate server's cert
			bool bSslVerifyPeer = false;
			curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER>
				sslVerifyPeer(bSslVerifyPeer);
			request.setOpt(sslVerifyPeer);
              
			curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
			request.setOpt(sslVerifyHost);

			// request.setOpt(new curlpp::options::SslEngineDefault());
		}

		request.setOpt(new curlpp::options::HttpHeader(header));

		request.setOpt(new curlpp::options::WriteStream(&response));

		chrono::system_clock::time_point startEncoding = chrono::system_clock::now();

		_logger->info(__FILEREF__ + "killEncodingJob"
			+ ", ffmpegEncoderURL: " + ffmpegEncoderURL
		);
		request.perform();
		chrono::system_clock::time_point endEncoding = chrono::system_clock::now();
		_logger->info(__FILEREF__ + "killEncodingJob"
			+ ", ffmpegEncoderURL: " + ffmpegEncoderURL
			+ ", @MMS statistics@ - encodingDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endEncoding - startEncoding).count()) + "@"
			+ ", response.str: " + response.str()
		);

		string sResponse = response.str();

		// LF and CR create problems to the json parser...
		while (sResponse.size() > 0 && (sResponse.back() == 10 || sResponse.back() == 13))
			sResponse.pop_back();

		{
			string message = __FILEREF__ + "Kill encoding response"
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", sResponse: " + sResponse
			;
			_logger->info(message);
		}

		long responseCode = curlpp::infos::ResponseCode::get(request);                                        
		if (responseCode != 200)
		{
			string errorMessage = __FILEREF__ + "Kill encoding URL failed"                                       
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", sResponse: " + sResponse                                                                 
				+ ", responseCode: " + to_string(responseCode)
			;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
	}
	catch (curlpp::LogicError & e) 
	{
		_logger->error(__FILEREF__ + "killEncoding URL failed (LogicError)"
			+ ", encodingJobKey: " + to_string(encodingJobKey) 
			+ ", ffmpegEncoderURL: " + ffmpegEncoderURL 
			+ ", exception: " + e.what()
			+ ", response.str(): " + response.str()
		);
            
		throw e;
	}
	catch (curlpp::RuntimeError & e) 
	{ 
		string errorMessage = string("killEncoding URL failed (RuntimeError)")
			+ ", encodingJobKey: " + to_string(encodingJobKey) 
			+ ", ffmpegEncoderURL: " + ffmpegEncoderURL 
			+ ", exception: " + e.what()
			+ ", response.str(): " + response.str()
		;
          
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}
	catch (runtime_error e)
	{
		_logger->error(__FILEREF__ + "killEncoding URL failed (runtime_error)"
			+ ", encodingJobKey: " + to_string(encodingJobKey) 
			+ ", ffmpegEncoderURL: " + ffmpegEncoderURL 
			+ ", exception: " + e.what()
			+ ", response.str(): " + response.str()
		);

		throw e;
	}
	catch (exception e)
	{
		_logger->error(__FILEREF__ + "killEncoding URL failed (exception)"
			+ ", encodingJobKey: " + to_string(encodingJobKey) 
			+ ", ffmpegEncoderURL: " + ffmpegEncoderURL 
			+ ", exception: " + e.what()
			+ ", response.str(): " + response.str()
		);

		throw e;
	}
}

