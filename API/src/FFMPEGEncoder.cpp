/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   FFMPEGEncoder.cpp
 * Author: giuliano
 * 
 * Created on February 18, 2018, 1:27 AM
 */

#include <fstream>
#include "catralibraries/ProcessUtility.h"
#include "catralibraries/System.h"
#include "FFMPEGEncoder.h"

extern char** environ;

int main(int argc, char** argv) 
{
    const char* configurationPathName = getenv("MMS_CONFIGPATHNAME");
    if (configurationPathName == nullptr)
    {
        cerr << "MMS API: the MMS_CONFIGPATHNAME environment variable is not defined" << endl;
        
        return 1;
    }
    
    Json::Value configuration = APICommon::loadConfigurationFile(configurationPathName);
    
    string logPathName =  configuration["log"]["encoder"].get("pathName", "XXX").asString();
    bool stdout =  configuration["log"]["encoder"].get("stdout", "XXX").asBool();
    
    std::vector<spdlog::sink_ptr> sinks;
    auto dailySink = make_shared<spdlog::sinks::daily_file_sink_mt> (logPathName.c_str(), 11, 20);
    sinks.push_back(dailySink);
    if (stdout)
    {
        auto stdoutSink = spdlog::sinks::stdout_sink_mt::instance();
        sinks.push_back(stdoutSink);
    }
    auto logger = std::make_shared<spdlog::logger>("Encoder", begin(sinks), end(sinks));
    
    // shared_ptr<spdlog::logger> logger = spdlog::stdout_logger_mt("API");
    // shared_ptr<spdlog::logger> logger = spdlog::daily_logger_mt("API", logPathName.c_str(), 11, 20);
    
    // trigger flush if the log severity is error or higher
    logger->flush_on(spdlog::level::trace);
    
    string logLevel =  configuration["log"]["encoder"].get("level", "XXX").asString();
    logger->info(__FILEREF__ + "Configuration item"
        + ", log->level: " + logLevel
    );
    if (logLevel == "debug")
        spdlog::set_level(spdlog::level::debug); // trace, debug, info, warn, err, critical, off
    else if (logLevel == "info")
        spdlog::set_level(spdlog::level::info); // trace, debug, info, warn, err, critical, off
    else if (logLevel == "err")
        spdlog::set_level(spdlog::level::err); // trace, debug, info, warn, err, critical, off
    string pattern =  configuration["log"]["encoder"].get("pattern", "XXX").asString();
    logger->info(__FILEREF__ + "Configuration item"
        + ", log->pattern: " + pattern
    );
    spdlog::set_pattern(pattern);

    // globally register the loggers so so the can be accessed using spdlog::get(logger_name)
    // spdlog::register_logger(logger);

    /*
    // the log is written in the apache error log (stderr)
    _logger = spdlog::stderr_logger_mt("API");

    // make sure only responses are written to the standard output
    spdlog::set_level(spdlog::level::trace);
    
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid %t] %v");
    
    // globally register the loggers so so the can be accessed using spdlog::get(logger_name)
    // spdlog::register_logger(logger);
     */

    logger->info(__FILEREF__ + "Creating MMSEngineDBFacade"
            );
    shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade = make_shared<MMSEngineDBFacade>(
            configuration, logger);

    logger->info(__FILEREF__ + "Creating MMSStorage"
            );
    shared_ptr<MMSStorage> mmsStorage = make_shared<MMSStorage>(
            configuration, mmsEngineDBFacade, logger);
    
    FCGX_Init();

    mutex fcgiAcceptMutex;

    FFMPEGEncoder ffmpegEncoder(configuration, 
            mmsEngineDBFacade,
            mmsStorage,
            &fcgiAcceptMutex,
            logger);

    return ffmpegEncoder();
}

FFMPEGEncoder::FFMPEGEncoder(Json::Value configuration, 
        shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade,
        shared_ptr<MMSStorage> mmsStorage,
        mutex* fcgiAcceptMutex,
        shared_ptr<spdlog::logger> logger)
    : APICommon(configuration, 
        mmsEngineDBFacade,
        mmsStorage,
        fcgiAcceptMutex,
        logger) 
{
    _maxEncodingsCapability =  _configuration["ffmpeg"].get("maxEncodingsCapability", 0).asInt();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", ffmpeg->maxEncodingsCapability: " + to_string(_maxEncodingsCapability)
    );

    _maxLiveRecordingsCapability =  _configuration["ffmpeg"].get("maxLiveRecordingsCapability", 0).asInt();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", ffmpeg->maxLiveRecordingsCapability: " + to_string(_maxEncodingsCapability)
    );

    _encodingCompletedRetentionInSeconds = _configuration["ffmpeg"].get("encodingCompletedRetentionInSeconds", 0).asInt();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", ffmpeg->encodingCompletedRetentionInSeconds: " + to_string(_encodingCompletedRetentionInSeconds)
    );

    for (int encodingIndex = 0; encodingIndex < _maxEncodingsCapability; encodingIndex++)
    {
        shared_ptr<Encoding>    encoding = make_shared<Encoding>();
        encoding->_running   = false;
        encoding->_childPid		= 0;
        encoding->_ffmpeg   = make_shared<FFMpeg>(_configuration, _logger);

        _encodingsCapability.push_back(encoding);
    }

    for (int liveRecordingIndex = 0; liveRecordingIndex < _maxLiveRecordingsCapability; liveRecordingIndex++)
    {
        shared_ptr<LiveRecording>    liveRecording = make_shared<LiveRecording>();
        liveRecording->_running   = false;
        liveRecording->_childPid		= 0;
        liveRecording->_ffmpeg   = make_shared<FFMpeg>(_configuration, _logger);

        _liveRecordingsCapability.push_back(liveRecording);
    }

	_lastEncodingCompletedCheck = chrono::system_clock::now();
}

FFMPEGEncoder::~FFMPEGEncoder() {
}

/*
void FFMPEGEncoder::getBinaryAndResponse(
        string requestURI,
        string requestMethod,
        string xCatraMMSResumeHeader,
        unordered_map<string, string> queryParameters,
        tuple<int64_t,shared_ptr<Workspace>,bool,bool,bool>& userKeyWorkspaceAndFlags,
        unsigned long contentLength
)
{
    _logger->error(__FILEREF__ + "FFMPEGEncoder application is able to manage ONLY NON-Binary requests");
    
    string errorMessage = string("Internal server error");
    _logger->error(__FILEREF__ + errorMessage);

    sendError(500, errorMessage);

    throw runtime_error(errorMessage);
}
*/

void FFMPEGEncoder::manageRequestAndResponse(
        FCGX_Request& request,
        string requestURI,
        string requestMethod,
        unordered_map<string, string> queryParameters,
        bool basicAuthenticationPresent,
        tuple<int64_t,shared_ptr<Workspace>,bool,bool,bool,bool,bool,bool>& userKeyWorkspaceAndFlags,
        unsigned long contentLength,
        string requestBody,
        unordered_map<string, string>& requestDetails
)
{
    
    auto methodIt = queryParameters.find("method");
    if (methodIt == queryParameters.end())
    {
        string errorMessage = string("The 'method' parameter is not found");
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 400, errorMessage);

        throw runtime_error(errorMessage);
    }
    string method = methodIt->second;

    if (method == "encodeContent")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
        lock_guard<mutex> locker(_encodingMutex);

        shared_ptr<Encoding>    selectedEncoding;
        bool                    encodingFound = false;
        for (shared_ptr<Encoding> encoding: _encodingsCapability)
        {
            if (!encoding->_running)
            {
                encodingFound = true;
                selectedEncoding = encoding;
                
                break;
            }
        }

        if (!encodingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingAvailableMessage("__NO-ENCODING-AVAILABLE__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingAvailableMessage;
            
            _logger->warn(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            // throw runtime_error(noEncodingAvailableMessage);
            return;
        }
        
        try
        {            
            selectedEncoding->_running = true;
            selectedEncoding->_childPid = 0;
            
            _logger->info(__FILEREF__ + "Creating encodeContent thread"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
            );
            thread encodeContentThread(&FFMPEGEncoder::encodeContent, this, selectedEncoding, encodingJobKey, requestBody);
            encodeContentThread.detach();
        }
        catch(exception e)
        {
            selectedEncoding->_running = false;
            selectedEncoding->_childPid = 0;

            _logger->error(__FILEREF__ + "encodeContentThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }

        try
        {            
            string responseBody = string("{ ")
                    + "\"encodingJobKey\": " + to_string(encodingJobKey) + " "
                    + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
                    + "}";

            sendSuccess(request, 200, responseBody);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "encodeContentThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else if (method == "overlayImageOnVideo")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
        lock_guard<mutex> locker(_encodingMutex);

        shared_ptr<Encoding>    selectedEncoding;
        bool                    encodingFound = false;
        for (shared_ptr<Encoding> encoding: _encodingsCapability)
        {
            if (!encoding->_running)
            {
                encodingFound = true;
                selectedEncoding = encoding;
                
                break;
            }
        }

        if (!encodingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingAvailableMessage("__NO-ENCODING-AVAILABLE__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingAvailableMessage;
            
            _logger->warn(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            // throw runtime_error(noEncodingAvailableMessage);
            return;
        }
        
        try
        {            
            selectedEncoding->_running = true;
            selectedEncoding->_childPid = 0;

            _logger->info(__FILEREF__ + "Creating encodeContent thread"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
            );
            thread overlayImageOnVideoThread(&FFMPEGEncoder::overlayImageOnVideo, this, selectedEncoding, encodingJobKey, requestBody);
            overlayImageOnVideoThread.detach();
        }
        catch(exception e)
        {
            selectedEncoding->_running = false;
            selectedEncoding->_childPid = 0;

            _logger->error(__FILEREF__ + "overlayImageOnVideoThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }

        try
        {            
            string responseBody = string("{ ")
                    + "\"encodingJobKey\": " + to_string(encodingJobKey) + " "
                    + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
                    + "}";

            sendSuccess(request, 200, responseBody);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "overlayImageOnVideoThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else if (method == "overlayTextOnVideo")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
        lock_guard<mutex> locker(_encodingMutex);

        shared_ptr<Encoding>    selectedEncoding;
        bool                    encodingFound = false;
        for (shared_ptr<Encoding> encoding: _encodingsCapability)
        {
            if (!encoding->_running)
            {
                encodingFound = true;
                selectedEncoding = encoding;
                
                break;
            }
        }

        if (!encodingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingAvailableMessage("__NO-ENCODING-AVAILABLE__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingAvailableMessage;
            
            _logger->warn(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            // throw runtime_error(noEncodingAvailableMessage);
            return;
        }
        
        try
        {            
            selectedEncoding->_running = true;
            selectedEncoding->_childPid = 0;

            _logger->info(__FILEREF__ + "Creating encodeContent thread"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
            );
            thread overlayTextOnVideoThread(&FFMPEGEncoder::overlayTextOnVideo, this, selectedEncoding, encodingJobKey, requestBody);
            overlayTextOnVideoThread.detach();
        }
        catch(exception e)
        {
            selectedEncoding->_running = false;
            selectedEncoding->_childPid = 0;
            
            _logger->error(__FILEREF__ + "overlayTextOnVideoThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }

        try
        {            
            string responseBody = string("{ ")
                    + "\"encodingJobKey\": " + to_string(encodingJobKey) + " "
                    + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
                    + "}";

            sendSuccess(request, 200, responseBody);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "overlayTextOnVideoThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else if (method == "generateFrames")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
        lock_guard<mutex> locker(_encodingMutex);

        shared_ptr<Encoding>    selectedEncoding;
        bool                    encodingFound = false;
        for (shared_ptr<Encoding> encoding: _encodingsCapability)
        {
            if (!encoding->_running)
            {
                encodingFound = true;
                selectedEncoding = encoding;
                
                break;
            }
        }

        if (!encodingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingAvailableMessage("__NO-ENCODING-AVAILABLE__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingAvailableMessage;
            
            _logger->warn(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            // throw runtime_error(noEncodingAvailableMessage);
            return;
        }
        
        try
        {            
            selectedEncoding->_running = true;
            selectedEncoding->_childPid = 0;

            _logger->info(__FILEREF__ + "Creating generateFrames thread"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
            );
            thread generateFramesThread(&FFMPEGEncoder::generateFrames, this, selectedEncoding, encodingJobKey, requestBody);
            generateFramesThread.detach();
        }
        catch(exception e)
        {
            selectedEncoding->_running = false;
            selectedEncoding->_childPid = 0;

            _logger->error(__FILEREF__ + "generateFrames failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }

        try
        {            
            string responseBody = string("{ ")
                    + "\"encodingJobKey\": " + to_string(encodingJobKey) + " "
                    + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
                    + "}";

            sendSuccess(request, 200, responseBody);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "generateFramesThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else if (method == "slideShow")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
        lock_guard<mutex> locker(_encodingMutex);

        shared_ptr<Encoding>    selectedEncoding;
        bool                    encodingFound = false;
        for (shared_ptr<Encoding> encoding: _encodingsCapability)
        {
            if (!encoding->_running)
            {
                encodingFound = true;
                selectedEncoding = encoding;
                
                break;
            }
        }

        if (!encodingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingAvailableMessage("__NO-ENCODING-AVAILABLE__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingAvailableMessage;
            
            _logger->warn(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            // throw runtime_error(noEncodingAvailableMessage);
            return;
        }
        
        try
        {            
            selectedEncoding->_running = true;
            selectedEncoding->_childPid = 0;

            _logger->info(__FILEREF__ + "Creating slideShow thread"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
            );
            thread slideShowThread(&FFMPEGEncoder::slideShow, this, selectedEncoding, encodingJobKey, requestBody);
            slideShowThread.detach();
        }
        catch(exception e)
        {
            selectedEncoding->_running = false;
            selectedEncoding->_childPid = 0;

            _logger->error(__FILEREF__ + "slideShow failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }

        try
        {            
            string responseBody = string("{ ")
                    + "\"encodingJobKey\": " + to_string(encodingJobKey) + " "
                    + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
                    + "}";

            sendSuccess(request, 200, responseBody);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "slideShowThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else if (method == "liveRecorder")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
        lock_guard<mutex> locker(_liveRecordingMutex);

        shared_ptr<LiveRecording>	selectedLiveRecording;
        bool						liveRecordingFound = false;
        for (shared_ptr<LiveRecording> liveRecording: _liveRecordingsCapability)
        {
            if (!liveRecording->_running)
            {
                liveRecordingFound = true;
                selectedLiveRecording = liveRecording;
                
                break;
            }
        }

        if (!liveRecordingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingAvailableMessage("__NO-ENCODING-AVAILABLE__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingAvailableMessage;
            
            _logger->warn(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            // throw runtime_error(noEncodingAvailableMessage);
            return;
        }
        
        try
        {            
            selectedLiveRecording->_running = true;
            selectedLiveRecording->_childPid = 0;

            _logger->info(__FILEREF__ + "Creating liveRecorder thread"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
            );
            thread liveRecorderThread(&FFMPEGEncoder::liveRecorder, this, selectedLiveRecording, encodingJobKey, requestBody);
            liveRecorderThread.detach();
        }
        catch(exception e)
        {
            selectedLiveRecording->_running = false;
            selectedLiveRecording->_childPid = 0;

            _logger->error(__FILEREF__ + "liveRecorder failed"
                + ", selectedLiveRecording->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }

        try
        {
            string responseBody = string("{ ")
                    + "\"encodingJobKey\": " + to_string(encodingJobKey) + " "
                    + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
                    + "}";

            sendSuccess(request, 200, responseBody);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "liveRecorderThread failed"
                + ", selectedEncoding->_encodingJobKey: " + to_string(encodingJobKey)
                + ", requestBody: " + requestBody
                + ", e.what(): " + e.what()
            );

            string errorMessage = string("Internal server error");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else if (method == "encodingStatus")
    {
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
		bool                    encodingFound = false;
		shared_ptr<Encoding>    selectedEncoding;

		bool                    liveRecordingFound = false;
		shared_ptr<LiveRecording>    selectedLiveRecording;

		bool                    encodingCompleted = false;
		shared_ptr<EncodingCompleted>    selectedEncodingCompleted;

		{
			lock_guard<mutex> locker(_encodingCompletedMutex);

			map<int64_t, shared_ptr<EncodingCompleted>>::iterator it =
				_encodingCompletedMap.find(encodingJobKey);
			if (it != _encodingCompletedMap.end())
			{
				encodingCompleted = true;
				selectedEncodingCompleted = it->second;
			}
		}

		if (!encodingCompleted)
		{
			lock_guard<mutex> locker(_encodingMutex);

			for (shared_ptr<Encoding> encoding: _encodingsCapability)
			{
				if (encoding->_encodingJobKey == encodingJobKey)
				{
					encodingFound = true;
					selectedEncoding = encoding;
                
					break;
				}
			}

			if (!encodingFound)
			{
				lock_guard<mutex> locker(_liveRecordingMutex);

				for (shared_ptr<LiveRecording> liveRecording: _liveRecordingsCapability)
				{
					if (liveRecording->_encodingJobKey == encodingJobKey)
					{
						liveRecordingFound = true;
						selectedLiveRecording = liveRecording;
                
						break;
					}
				}
			}
		}

		_logger->info(__FILEREF__ + "Encoding Status"
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", encodingFound: " + to_string(encodingFound)
				+ ", liveRecordingFound: " + to_string(liveRecordingFound)
				+ ", encodingCompleted: " + to_string(encodingCompleted)
				);
        string responseBody;
        if (!encodingFound && !liveRecordingFound && !encodingCompleted)
        {
			// it should never happen
            responseBody = string("{ ")
                + "\"encodingJobKey\": " + to_string(encodingJobKey)
                + ", \"pid\": 0"
				+ ", \"killedByUser\": false"
                + ", \"encodingFinished\": true "
                + "}";
        }
        else
        {
            /* in case we see the encoding is finished but encoding->_running is true,
            it means the thread encodingContent is hanged.
             In this case we can implement the method _ffmpeg->stillEncoding
             doing a check may be looking if the encoded file is growing.
             That will allow us to know for sure if the encoding finished and
             reset the Encoding structure
            bool stillEncoding = encoding->_ffmpeg->stillEncoding();
            if (!stillEncoding)
            {
                encoding->_running = false;
            }
            */
            
			if (encodingCompleted)
				responseBody = string("{ ")
					+ "\"encodingJobKey\": " + to_string(selectedEncodingCompleted->_encodingJobKey)
					+ ", \"pid\": 0 "
					+ ", \"killedByUser\": " + (selectedEncodingCompleted->_killedByUser ? "true" : "false")
					+ ", \"encodingFinished\": true "
					+ "}";
			else if (encodingFound)
				responseBody = string("{ ")
					+ "\"encodingJobKey\": " + to_string(selectedEncoding->_encodingJobKey)
					+ ", \"pid\": " + to_string(selectedEncoding->_childPid)
					+ ", \"killedByUser\": false"
					+ ", \"encodingFinished\": " + (selectedEncoding->_running ? "false " : "true ")
					+ "}";
			else // if (liveRecording)
				responseBody = string("{ ")
					+ "\"encodingJobKey\": " + to_string(selectedLiveRecording->_encodingJobKey)
					+ ", \"pid\": " + to_string(selectedLiveRecording->_childPid)
					+ ", \"killedByUser\": false"
					+ ", \"encodingFinished\": " + (selectedLiveRecording->_running ? "false " : "true ")
					+ "}";
        }

        sendSuccess(request, 200, responseBody);
    }
    else if (method == "encodingProgress")
    {
        /*
        bool isAdminAPI = get<1>(workspaceAndFlags);
        if (!isAdminAPI)
        {
            string errorMessage = string("APIKey flags does not have the ADMIN permission"
                    ", isAdminAPI: " + to_string(isAdminAPI)
                    );
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 403, errorMessage);

            throw runtime_error(errorMessage);
        }
        */
        
        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);

        lock_guard<mutex> locker(_encodingMutex);

        shared_ptr<Encoding>    selectedEncoding;
        bool                    encodingFound = false;
        for (shared_ptr<Encoding> encoding: _encodingsCapability)
        {
            if (encoding->_encodingJobKey == encodingJobKey)
            {
                encodingFound = true;
                selectedEncoding = encoding;
                
                break;
            }
        }

        if (!encodingFound)
        {
            // same string declared in EncoderVideoAudioProxy.cpp
            string noEncodingJobKeyFound("__NO-ENCODINGJOBKEY-FOUND__");
            
            string errorMessage = string("EncodingJobKey: ") + to_string(encodingJobKey)
                    + ", " + noEncodingJobKeyFound;
            
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int encodingProgress;
        try
        {
            encodingProgress = selectedEncoding->_ffmpeg->getEncodingProgress();
        }
        catch(FFMpegEncodingStatusNotAvailable e)
        {
            string errorMessage = string("_ffmpeg->getEncodingProgress failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                + ", e.what(): " + e.what()
                    ;
            _logger->info(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            // throw e;
            return;
        }
        catch(exception e)
        {
            string errorMessage = string("_ffmpeg->getEncodingProgress failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                + ", e.what(): " + e.what()
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw e;
        }
        
        string responseBody = string("{ ")
			+ "\"encodingJobKey\": " + to_string(encodingJobKey)
			+ ", \"pid\": " + to_string(selectedEncoding->_childPid)
            + ", \"encodingProgress\": " + to_string(encodingProgress) + " "
            + "}";
        
        sendSuccess(request, 200, responseBody);
    }
    else if (method == "killEncodingJob")
    {
        /*
        bool isAdminAPI = get<1>(workspaceAndFlags);
        if (!isAdminAPI)
        {
            string errorMessage = string("APIKey flags does not have the ADMIN permission"
                    ", isAdminAPI: " + to_string(isAdminAPI)
                    );
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 403, errorMessage);

            throw runtime_error(errorMessage);
        }
        */

        auto encodingJobKeyIt = queryParameters.find("encodingJobKey");
        if (encodingJobKeyIt == queryParameters.end())
        {
            string errorMessage = string("The 'encodingJobKey' parameter is not found");
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 400, errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t encodingJobKey = stoll(encodingJobKeyIt->second);
        
		pid_t			pidToBeKilled;
		bool			encodingFound = false;

		{
			lock_guard<mutex> locker(_encodingMutex);

			for (shared_ptr<Encoding> encoding: _encodingsCapability)
			{
				if (encoding->_encodingJobKey == encodingJobKey)
				{
					encodingFound = true;
					pidToBeKilled = encoding->_childPid;
               
					break;
				}
			}
		}

		if (!encodingFound)
		{
			lock_guard<mutex> locker(_liveRecordingMutex);

			for (shared_ptr<LiveRecording> liveRecording: _liveRecordingsCapability)
			{
				if (liveRecording->_encodingJobKey == encodingJobKey)
				{
					encodingFound = true;
					pidToBeKilled = liveRecording->_childPid;
               
					break;
				}
			}
		}

		_logger->info(__FILEREF__ + "Found Encoding to kill"
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", pidToBeKilled: " + to_string(pidToBeKilled)
				);

        try
        {
			ProcessUtility::killProcess(pidToBeKilled);
        }
        catch(runtime_error e)
        {
            string errorMessage = string("ProcessUtility::killProcess failed")
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", pidToBeKilled: " + to_string(pidToBeKilled)
                + ", e.what(): " + e.what()
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            sendError(request, 500, errorMessage);

            throw e;
        }

		string responseBody = string("{ ")
			+ "\"encodingJobKey\": " + to_string(encodingJobKey)
			+ ", \"pid\": " + to_string(pidToBeKilled)
			+ "}";

        sendSuccess(request, 200, responseBody);
    }
    else
    {
        string errorMessage = string("No API is matched")
            + ", requestURI: " +requestURI
            + ", requestMethod: " +requestMethod;
        _logger->error(__FILEREF__ + errorMessage);

        sendError(request, 400, errorMessage);

        throw runtime_error(errorMessage);
    }

	if (chrono::system_clock::now() - _lastEncodingCompletedCheck >=
			chrono::seconds(_encodingCompletedRetentionInSeconds))
	{
		_lastEncodingCompletedCheck = chrono::system_clock::now();
		encodingCompletedRetention();
	}
}

void FFMPEGEncoder::encodeContent(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody)
{
    string api = "encodeContent";

    _logger->info(__FILEREF__ + "Received " + api
                    + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", requestBody: " + requestBody
    );

    try
    {
        encoding->_encodingJobKey = encodingJobKey;
		removeEncodingCompletedIfPresent(encodingJobKey);

        /*
        {
            "mmsSourceAssetPathName": "...",
            "durationInMilliSeconds": 111,
            "encodedFileName": "...",
            "stagingEncodedAssetPathName": "...",
            "encodingProfileDetails": {
                ....
            },
            "contentType": "...",
            "physicalPathKey": 1111,
            "workspaceDirectoryName": "...",
            "relativePath": "...",
            "encodingJobKey": 1111,
            "ingestionJobKey": 1111,
        }
        */
        Json::Value encodingMedatada;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(requestBody.c_str(),
                    requestBody.c_str() + requestBody.size(), 
                    &encodingMedatada, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", errors: " + errors
                        + ", requestBody: " + requestBody
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }

        string mmsSourceAssetPathName = encodingMedatada.get("mmsSourceAssetPathName", "XXX").asString();
        int64_t durationInMilliSeconds = encodingMedatada.get("durationInMilliSeconds", -1).asInt64();
        // string encodedFileName = encodingMedatada.get("encodedFileName", "XXX").asString();
        string stagingEncodedAssetPathName = encodingMedatada.get("stagingEncodedAssetPathName", "XXX").asString();
        string encodingProfileDetails;
        {
            Json::StreamWriterBuilder wbuilder;
            
            encodingProfileDetails = Json::writeString(wbuilder, encodingMedatada["encodingProfileDetails"]);
        }
        MMSEngineDBFacade::ContentType contentType = MMSEngineDBFacade::toContentType(encodingMedatada.get("contentType", "XXX").asString());
        int64_t physicalPathKey = encodingMedatada.get("physicalPathKey", -1).asInt64();
        string workspaceDirectoryName = encodingMedatada.get("workspaceDirectoryName", "XXX").asString();
        string relativePath = encodingMedatada.get("relativePath", "XXX").asString();
        int64_t encodingJobKey = encodingMedatada.get("encodingJobKey", -1).asInt64();
        int64_t ingestionJobKey = encodingMedatada.get("ingestionJobKey", -1).asInt64();

		// chrono::system_clock::time_point startEncoding = chrono::system_clock::now();
        encoding->_ffmpeg->encodeContent(
                mmsSourceAssetPathName,
                durationInMilliSeconds,
                // encodedFileName,
                stagingEncodedAssetPathName,
                encodingProfileDetails,
                contentType == MMSEngineDBFacade::ContentType::Video,
                physicalPathKey,
                workspaceDirectoryName,
                relativePath,
                encodingJobKey,
                ingestionJobKey,
				&(encoding->_childPid));
		// chrono::system_clock::time_point endEncoding = chrono::system_clock::now();

//        string responseBody = string("{ ")
//                + "\"ingestionJobKey\": " + to_string(ingestionJobKey) + " "
//                + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
//                + "}";

        // sendSuccess(request, 200, responseBody);
        
        encoding->_running = false;
        encoding->_childPid = 0;
        
        _logger->info(__FILEREF__ + "Encode content finished"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

		bool completedWithError			= false;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
	catch(FFMpegEncodingKilledByUser e)
	{
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= false;
		bool killedByUser				= true;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(runtime_error e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
        // this method run on a detached thread, we will not generate exception
        // The ffmpeg method will make sure the encoded file is removed 
        // (this is checked in EncoderVideoAudioProxy)
        // throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
        // this method run on a detached thread, we will not generate exception
        // The ffmpeg method will make sure the encoded file is removed 
        // (this is checked in EncoderVideoAudioProxy)
        // throw runtime_error(errorMessage);
    }
}

void FFMPEGEncoder::overlayImageOnVideo(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody)
{
    string api = "overlayImageOnVideo";

    _logger->info(__FILEREF__ + "Received " + api
                    + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", requestBody: " + requestBody
    );

    try
    {
        encoding->_encodingJobKey = encodingJobKey;
		removeEncodingCompletedIfPresent(encodingJobKey);

        Json::Value overlayMedatada;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(requestBody.c_str(),
                    requestBody.c_str() + requestBody.size(), 
                    &overlayMedatada, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", errors: " + errors
                        + ", requestBody: " + requestBody
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        
        string mmsSourceVideoAssetPathName = overlayMedatada.get("mmsSourceVideoAssetPathName", "XXX").asString();
        int64_t videoDurationInMilliSeconds = overlayMedatada.get("videoDurationInMilliSeconds", -1).asInt64();
        string mmsSourceImageAssetPathName = overlayMedatada.get("mmsSourceImageAssetPathName", "XXX").asString();
        string imagePosition_X_InPixel = overlayMedatada.get("imagePosition_X_InPixel", "XXX").asString();
        string imagePosition_Y_InPixel = overlayMedatada.get("imagePosition_Y_InPixel", "XXX").asString();

        // string encodedFileName = overlayMedatada.get("encodedFileName", "XXX").asString();
        string stagingEncodedAssetPathName = overlayMedatada.get("stagingEncodedAssetPathName", "XXX").asString();
        int64_t encodingJobKey = overlayMedatada.get("encodingJobKey", -1).asInt64();
        int64_t ingestionJobKey = overlayMedatada.get("ingestionJobKey", -1).asInt64();

		// chrono::system_clock::time_point startEncoding = chrono::system_clock::now();
        encoding->_ffmpeg->overlayImageOnVideo(
            mmsSourceVideoAssetPathName,
            videoDurationInMilliSeconds,
            mmsSourceImageAssetPathName,
            imagePosition_X_InPixel,
            imagePosition_Y_InPixel,
            // encodedFileName,
            stagingEncodedAssetPathName,
            encodingJobKey,
            ingestionJobKey,
			&(encoding->_childPid));
		// chrono::system_clock::time_point endEncoding = chrono::system_clock::now();

//        string responseBody = string("{ ")
//                + "\"ingestionJobKey\": " + to_string(ingestionJobKey) + " "
//                + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
//                + "}";

        // sendSuccess(request, 200, responseBody);
        
        encoding->_running = false;
        encoding->_childPid = 0;
        
        _logger->info(__FILEREF__ + "Encode content finished"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

		bool completedWithError			= false;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
	catch(FFMpegEncodingKilledByUser e)
	{
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= false;
		bool killedByUser				= true;
		addEncodingCompleted(encoding->_encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(runtime_error e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
        // this method run on a detached thread, we will not generate exception
        // The ffmpeg method will make sure the encoded file is removed 
        // (this is checked in EncoderVideoAudioProxy)
        // throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
        // this method run on a detached thread, we will not generate exception
        // The ffmpeg method will make sure the encoded file is removed 
        // (this is checked in EncoderVideoAudioProxy)
        // throw runtime_error(errorMessage);
    }
}

void FFMPEGEncoder::overlayTextOnVideo(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody)
{
    string api = "overlayTextOnVideo";

    _logger->info(__FILEREF__ + "Received " + api
                    + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", requestBody: " + requestBody
    );

    try
    {
        encoding->_encodingJobKey = encodingJobKey;
		removeEncodingCompletedIfPresent(encodingJobKey);

        Json::Value overlayTextMedatada;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(requestBody.c_str(),
                    requestBody.c_str() + requestBody.size(), 
                    &overlayTextMedatada, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", errors: " + errors
                        + ", requestBody: " + requestBody
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        
        string mmsSourceVideoAssetPathName = overlayTextMedatada.get("mmsSourceVideoAssetPathName", "XXX").asString();
        int64_t videoDurationInMilliSeconds = overlayTextMedatada.get("videoDurationInMilliSeconds", -1).asInt64();

        string text = overlayTextMedatada.get("text", "XXX").asString();
        
        string textPosition_X_InPixel;
        string field = "textPosition_X_InPixel";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            textPosition_X_InPixel = overlayTextMedatada.get(field, "XXX").asString();
        
        string textPosition_Y_InPixel;
        field = "textPosition_Y_InPixel";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            textPosition_Y_InPixel = overlayTextMedatada.get(field, "XXX").asString();
        
        string fontType;
        field = "fontType";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            fontType = overlayTextMedatada.get(field, "XXX").asString();

        int fontSize = -1;
        field = "fontSize";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            fontSize = overlayTextMedatada.get(field, -1).asInt();

        string fontColor;
        field = "fontColor";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            fontColor = overlayTextMedatada.get(field, "XXX").asString();

        int textPercentageOpacity = -1;
        field = "textPercentageOpacity";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            textPercentageOpacity = overlayTextMedatada.get(field, -1).asInt();

        bool boxEnable = false;
        field = "boxEnable";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            boxEnable = overlayTextMedatada.get(field, 0).asBool();

        string boxColor;
        field = "boxColor";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            boxColor = overlayTextMedatada.get(field, "XXX").asString();
        
        int boxPercentageOpacity = -1;
        field = "boxPercentageOpacity";
        if (_mmsEngineDBFacade->isMetadataPresent(overlayTextMedatada, field))
            boxPercentageOpacity = overlayTextMedatada.get("boxPercentageOpacity", -1).asInt();

        // string encodedFileName = overlayTextMedatada.get("encodedFileName", "XXX").asString();
        string stagingEncodedAssetPathName = overlayTextMedatada.get("stagingEncodedAssetPathName", "XXX").asString();
        int64_t encodingJobKey = overlayTextMedatada.get("encodingJobKey", -1).asInt64();
        int64_t ingestionJobKey = overlayTextMedatada.get("ingestionJobKey", -1).asInt64();

		// chrono::system_clock::time_point startEncoding = chrono::system_clock::now();
        encoding->_ffmpeg->overlayTextOnVideo(
                mmsSourceVideoAssetPathName,
                videoDurationInMilliSeconds,

                text,
                textPosition_X_InPixel,
                textPosition_Y_InPixel,
                fontType,
                fontSize,
                fontColor,
                textPercentageOpacity,
                boxEnable,
                boxColor,
                boxPercentageOpacity,

                // encodedFileName,
                stagingEncodedAssetPathName,
                encodingJobKey,
                ingestionJobKey,
				&(encoding->_childPid));
		// chrono::system_clock::time_point endEncoding = chrono::system_clock::now();

//        string responseBody = string("{ ")
//                + "\"ingestionJobKey\": " + to_string(ingestionJobKey) + " "
//                + ", \"ffmpegEncoderHost\": \"" + System::getHostName() + "\" "
//                + "}";

        // sendSuccess(request, 200, responseBody);
        
        encoding->_running = false;
        encoding->_childPid = 0;
        
        _logger->info(__FILEREF__ + "Encode content finished"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

		bool completedWithError			= false;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
	catch(FFMpegEncodingKilledByUser e)
	{
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= false;
		bool killedByUser				= true;
		addEncodingCompleted(encoding->_encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(runtime_error e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
        // this method run on a detached thread, we will not generate exception
        // The ffmpeg method will make sure the encoded file is removed 
        // (this is checked in EncoderVideoAudioProxy)
        // throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
        // this method run on a detached thread, we will not generate exception
        // The ffmpeg method will make sure the encoded file is removed 
        // (this is checked in EncoderVideoAudioProxy)
        // throw runtime_error(errorMessage);
    }
}

void FFMPEGEncoder::generateFrames(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody)
{
    string api = "generateFrames";

    _logger->info(__FILEREF__ + "Received " + api
                    + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", requestBody: " + requestBody
    );

    try
    {
        encoding->_encodingJobKey = encodingJobKey;
		removeEncodingCompletedIfPresent(encodingJobKey);

        Json::Value generateFramesMedatada;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(requestBody.c_str(),
                    requestBody.c_str() + requestBody.size(), 
                    &generateFramesMedatada, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", errors: " + errors
                        + ", requestBody: " + requestBody
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        
        string imageDirectory = generateFramesMedatada.get("imageDirectory", "XXX").asString();
        double startTimeInSeconds = generateFramesMedatada.get("startTimeInSeconds", -1).asDouble();
        int maxFramesNumber = generateFramesMedatada.get("maxFramesNumber", -1).asInt();
        string videoFilter = generateFramesMedatada.get("videoFilter", "XXX").asString();
        int periodInSeconds = generateFramesMedatada.get("periodInSeconds", -1).asInt();
        bool mjpeg = generateFramesMedatada.get("mjpeg", -1).asBool();
        int imageWidth = generateFramesMedatada.get("imageWidth", -1).asInt();
        int imageHeight = generateFramesMedatada.get("imageHeight", -1).asInt();
        int64_t ingestionJobKey = generateFramesMedatada.get("ingestionJobKey", -1).asInt64();
        string mmsSourceVideoAssetPathName = generateFramesMedatada.get("mmsSourceVideoAssetPathName", "XXX").asString();
        int64_t videoDurationInMilliSeconds = generateFramesMedatada.get("videoDurationInMilliSeconds", -1).asInt64();

        vector<string> generatedFramesFileNames = encoding->_ffmpeg->generateFramesToIngest(
                ingestionJobKey,
                encodingJobKey,
                imageDirectory,
                to_string(ingestionJobKey),    // imageBaseFileName,
                startTimeInSeconds,
                maxFramesNumber,
                videoFilter,
                periodInSeconds,
                mjpeg,
                imageWidth, 
                imageHeight,
                mmsSourceVideoAssetPathName,
                videoDurationInMilliSeconds,
				&(encoding->_childPid)
        );
        
        encoding->_running = false;
        encoding->_childPid = 0;
        
        _logger->info(__FILEREF__ + "generateFrames finished"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", encodingJobKey: " + to_string(encodingJobKey)
        );

		bool completedWithError			= false;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
	catch(FFMpegEncodingKilledByUser e)
	{
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= false;
		bool killedByUser				= true;
		addEncodingCompleted(encoding->_encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(runtime_error e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(exception e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
}

void FFMPEGEncoder::slideShow(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody)
{
    string api = "slideShow";

    _logger->info(__FILEREF__ + "Received " + api
                    + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", requestBody: " + requestBody
    );

    try
    {
        encoding->_encodingJobKey = encodingJobKey;
		removeEncodingCompletedIfPresent(encodingJobKey);

        Json::Value slideShowMedatada;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(requestBody.c_str(),
                    requestBody.c_str() + requestBody.size(), 
                    &slideShowMedatada, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", errors: " + errors
                        + ", requestBody: " + requestBody
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        
        int64_t ingestionJobKey = slideShowMedatada.get("ingestionJobKey", -1).asInt64();
        double durationOfEachSlideInSeconds = slideShowMedatada.get("durationOfEachSlideInSeconds", -1).asDouble();
        int outputFrameRate = slideShowMedatada.get("outputFrameRate", -1).asInt();
        string slideShowMediaPathName = slideShowMedatada.get("slideShowMediaPathName", "XXX").asString();
        
        vector<string> sourcePhysicalPaths;
        Json::Value sourcePhysicalPathsRoot(Json::arrayValue);
        sourcePhysicalPathsRoot = slideShowMedatada["sourcePhysicalPaths"];
        for (int sourcePhysicalPathIndex = 0; sourcePhysicalPathIndex < sourcePhysicalPathsRoot.size(); ++sourcePhysicalPathIndex)
        {
            string sourcePhysicalPathName = sourcePhysicalPathsRoot.get(sourcePhysicalPathIndex, "XXX").asString();

            sourcePhysicalPaths.push_back(sourcePhysicalPathName);
        }

        encoding->_ffmpeg->generateSlideshowMediaToIngest(ingestionJobKey, encodingJobKey,
                sourcePhysicalPaths, durationOfEachSlideInSeconds,
                outputFrameRate, slideShowMediaPathName,
				&(encoding->_childPid));
        
        encoding->_running = false;
        encoding->_childPid = 0;
        
        _logger->info(__FILEREF__ + "slideShow finished"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", encodingJobKey: " + to_string(encodingJobKey)
        );

		bool completedWithError			= false;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
	catch(FFMpegEncodingKilledByUser e)
	{
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= false;
		bool killedByUser				= true;
		addEncodingCompleted(encoding->_encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(runtime_error e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(exception e)
    {
        encoding->_running = false;
        encoding->_childPid = 0;

        string errorMessage = string("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
}

void FFMPEGEncoder::liveRecorder(
        // FCGX_Request& request,
        shared_ptr<LiveRecording> liveRecording,
        int64_t encodingJobKey,
        string requestBody)
{
    string api = "liveRecorder";

    _logger->info(__FILEREF__ + "Received " + api
                    + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", requestBody: " + requestBody
    );

    try
    {
        liveRecording->_encodingJobKey = encodingJobKey;
		removeEncodingCompletedIfPresent(encodingJobKey);

        Json::Value liveRecorderMedatada;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(requestBody.c_str(),
                    requestBody.c_str() + requestBody.size(), 
                    &liveRecorderMedatada, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the requestBody"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", errors: " + errors
                        + ", requestBody: " + requestBody
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("requestBody json is not well format")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", requestBody: " + requestBody
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
        
        int64_t ingestionJobKey = liveRecorderMedatada.get("ingestionJobKey", -1).asInt64();
        string segmentListPathName = liveRecorderMedatada.get("segmentListPathName", "XXX").asString();
        string recordedFileNamePrefix = liveRecorderMedatada.get("recordedFileNamePrefix", "XXX").asString();
        string liveURL = liveRecorderMedatada.get("liveURL", "XXX").asString();
        time_t utcRecordingPeriodStart = liveRecorderMedatada.get("utcRecordingPeriodStart", -1).asInt64();
        time_t utcRecordingPeriodEnd = liveRecorderMedatada.get("utcRecordingPeriodEnd", -1).asInt64();
        int segmentDurationInSeconds = liveRecorderMedatada.get("segmentDurationInSeconds", -1).asInt();
        string outputFileFormat = liveRecorderMedatada.get("outputFileFormat", "XXX").asString();

        liveRecording->_ffmpeg->liveRecorder(
                ingestionJobKey,
				encodingJobKey,
                segmentListPathName,
				recordedFileNamePrefix,
                liveURL,
                utcRecordingPeriodStart,
                utcRecordingPeriodEnd,
                segmentDurationInSeconds,
                outputFileFormat,
				&(liveRecording->_childPid)
        );
        
        liveRecording->_running = false;
        liveRecording->_childPid = 0;
        
        _logger->info(__FILEREF__ + "liveRecorded finished"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", encodingJobKey: " + to_string(encodingJobKey)
        );

		bool completedWithError			= false;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
	catch(FFMpegEncodingKilledByUser e)
	{
        liveRecording->_running = false;
        liveRecording->_childPid = 0;

        string errorMessage = string ("API failed")
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= false;
		bool killedByUser				= true;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(runtime_error e)
    {
        liveRecording->_running = false;
        liveRecording->_childPid = 0;

        string errorMessage = string ("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
    catch(exception e)
    {
        liveRecording->_running = false;
        liveRecording->_childPid = 0;

        string errorMessage = string("API failed")
                    + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", API: " + api
            + ", requestBody: " + requestBody
            + ", e.what(): " + e.what()
        ;

        _logger->error(__FILEREF__ + errorMessage);

		bool completedWithError			= true;
		bool killedByUser				= false;
		addEncodingCompleted(encodingJobKey,
				completedWithError, killedByUser);
    }
}

void FFMPEGEncoder::addEncodingCompleted(
        int64_t encodingJobKey, bool completedWithError,
		bool killedByUser)
{
	lock_guard<mutex> locker(_encodingCompletedMutex);

	shared_ptr<EncodingCompleted> encodingCompleted = make_shared<EncodingCompleted>();

	encodingCompleted->_encodingJobKey		= encodingJobKey;
	encodingCompleted->_completedWithError	= completedWithError;
	encodingCompleted->_killedByUser		= killedByUser;
	encodingCompleted->_timestamp			= chrono::system_clock::now();

	_encodingCompletedMap.insert(make_pair(encodingCompleted->_encodingJobKey, encodingCompleted));

	_logger->info(__FILEREF__ + "addEncodingCompleted"
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", encodingCompletedRetention.size: " + to_string(_encodingCompletedMap.size())
			);
}

void FFMPEGEncoder::removeEncodingCompletedIfPresent(int64_t encodingJobKey)
{

	lock_guard<mutex> locker(_encodingCompletedMutex);

	map<int64_t, shared_ptr<EncodingCompleted>>::iterator it =
		_encodingCompletedMap.find(encodingJobKey);
	if (it != _encodingCompletedMap.end())
	{
		_encodingCompletedMap.erase(it);

		_logger->info(__FILEREF__ + "removeEncodingCompletedIfPresent"
			+ ", encodingCompletedRetention.size: " + to_string(_encodingCompletedMap.size())
			);
	}
}

void FFMPEGEncoder::encodingCompletedRetention()
{

	lock_guard<mutex> locker(_encodingCompletedMutex);

	chrono::system_clock::time_point now = chrono::system_clock::now();

	for(map<int64_t, shared_ptr<EncodingCompleted>>::iterator it = _encodingCompletedMap.begin();
			it != _encodingCompletedMap.end(); )
	{
		if(now - (it->second->_timestamp) >= chrono::seconds(_encodingCompletedRetentionInSeconds))
			it = _encodingCompletedMap.erase(it);
		else
			it++;
	}

	_logger->info(__FILEREF__ + "encodingCompletedRetention"
			+ ", encodingCompletedRetention.size: " + to_string(_encodingCompletedMap.size())
			);
}

