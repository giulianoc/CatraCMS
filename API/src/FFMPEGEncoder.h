/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   FFMPEGEncoder.h
 * Author: giuliano
 *
 * Created on February 18, 2018, 1:27 AM
 */

#ifndef FFMPEGEncoder_h
#define FFMPEGEncoder_h

#include "APICommon.h"
#include "FFMpeg.h"

struct Encoding
{
        bool                    _running;
        int64_t                 _encodingJobKey;
        shared_ptr<FFMpeg>		_ffmpeg;
		pid_t					_childPid;

		string					_errorMessage;
};

struct LiveProxyAndGrid
{
        bool                    _running;
        int64_t                 _encodingJobKey;
        shared_ptr<FFMpeg>		_ffmpeg;
		pid_t					_childPid;
        bool					_killedBecauseOfNotWorking;	// by monitorThread

		string					_errorMessage;

		int64_t					_ingestionJobKey;
		string					_outputType;
		string					_channelLabel;
		vector<string>			_manifestFilePathNames;
		chrono::system_clock::time_point	_proxyStart;
};

// no encoding, just copying the video/audio tracks
struct LiveRecording
{
        bool                    _running;
        int64_t                 _encodingJobKey;
        shared_ptr<FFMpeg>      _ffmpeg;
		pid_t					_childPid;
        bool					_killedBecauseOfNotWorking;	// by monitorThread

		string					_errorMessage;

		int64_t					_ingestionJobKey;
		Json::Value				_encodingParametersRoot;
		Json::Value				_liveRecorderParametersRoot;
        string					_transcoderStagingContentsPath;
        string					_stagingContentsPath;
        string					_segmentListFileName;
        string					_recordedFileNamePrefix;
		string					_lastRecordedAssetFileName;
		int						_lastRecordedAssetDurationInSeconds;
		string					_channelLabel;
		chrono::system_clock::time_point	_recordingStart;
};

struct EncodingCompleted
{
		int64_t					_encodingJobKey;
		bool					_completedWithError;
		string					_errorMessage;
		bool					_killedByUser;
		bool					_urlForbidden;
		bool					_urlNotFound;
		chrono::system_clock::time_point	_timestamp;
};

class FFMPEGEncoder: public APICommon {
public:
    FFMPEGEncoder(Json::Value configuration, 
		mutex* fcgiAcceptMutex,
		mutex* encodingMutex,
		vector<shared_ptr<Encoding>>* encodingsCapability,
		mutex* liveProxyMutex,
		vector<shared_ptr<LiveProxyAndGrid>>* liveProxiesCapability,
		mutex* liveRecordingMutex,
		vector<shared_ptr<LiveRecording>>* liveRecordingsCapability, 
		mutex* encodingCompletedMutex,
		map<int64_t, shared_ptr<EncodingCompleted>>* encodingCompletedMap,
		chrono::system_clock::time_point* lastEncodingCompletedCheck,
		shared_ptr<spdlog::logger> logger);
    
    ~FFMPEGEncoder();
    
    /*
    virtual void getBinaryAndResponse(
        string requestURI,
        string requestMethod,
        string xCatraMMSResumeHeader,
        unordered_map<string, string> queryParameters,
        tuple<int64_t,shared_ptr<Workspace>,bool,bool,bool>& userKeyWorkspaceAndFlags,
        unsigned long contentLength);
    */

    virtual void manageRequestAndResponse(
            FCGX_Request& request,
            string requestURI,
            string requestMethod,
            unordered_map<string, string> queryParameters,
            bool basicAuthenticationPresent,
            tuple<int64_t,shared_ptr<Workspace>,bool,bool,bool, bool, bool,bool,bool,bool,bool>& userKeyWorkspaceAndFlags,
			string apiKey,
            unsigned long contentLength,
            string requestBody,
            unordered_map<string, string>& requestDetails
    );

	void liveRecorderChunksIngestionThread();
	void stopLiveRecorderChunksIngestionThread();

	void monitorThread();
	void stopMonitorThread();
    
private:
    mutex*						_encodingMutex;
    // int                         _maxEncodingsCapability;
    vector<shared_ptr<Encoding>>* _encodingsCapability;

    mutex*						_liveProxyMutex;
    // int							_maxLiveProxiesCapability;
    vector<shared_ptr<LiveProxyAndGrid>>* _liveProxiesCapability;
	int							_monitorCheckInSeconds;
	bool						_monitorThreadShutdown;

    mutex*						_liveRecordingMutex;
    // int                         _maxLiveRecordingsCapability;
    vector<shared_ptr<LiveRecording>>* _liveRecordingsCapability;
	int							_liveRecorderChunksIngestionCheckInSeconds;
	bool						_liveRecorderChunksIngestionThreadShutdown;

    mutex*						_encodingCompletedMutex;
	int							_encodingCompletedRetentionInSeconds;
    map<int64_t, shared_ptr<EncodingCompleted>>*	_encodingCompletedMap;
	chrono::system_clock::time_point*				_lastEncodingCompletedCheck;

    string								_mmsAPIProtocol;
    string								_mmsAPIHostname;
    int									_mmsAPIPort;
    // string								_mmsAPIUser;
    // string								_mmsAPIPassword;
    string								_mmsAPIIngestionURI;
    int									_mmsAPITimeoutInSeconds;

    void encodeContent(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
            int64_t encodingJobKey,
        string requestBody);
    
    void overlayImageOnVideo(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
            int64_t encodingJobKey,
        string requestBody);

    void overlayTextOnVideo(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody);

    void generateFrames(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody);

    void slideShow(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody);

	void liveRecorder(
        // FCGX_Request& request,
        shared_ptr<LiveRecording> liveRecording,
        int64_t encodingJobKey,
        string requestBody);
	pair<string, int> liveRecorder_processLastGeneratedLiveRecorderFiles(
		int64_t ingestionJobKey, int64_t encodingJobKey,
		bool highAvailability, bool main, int segmentDurationInSeconds, string outputFileFormat,
		Json::Value encodingParametersRoot,
		Json::Value liveRecorderParametersRoot,
		string transcoderStagingContentsPath,
		string stagingContentsPath,
		string segmentListFileName,
		string recordedFileNamePrefix,
		string lastRecordedAssetFileName,
		int lastRecordedAssetDurationInSeconds);
	time_t liveRecorder_getMediaLiveRecorderStartTime(int64_t ingestionJobKey, int64_t encodingJobKey,
			string mediaLiveRecorderFileName, int segmentDurationInSeconds, bool isFirstChunk);
	time_t liveRecorder_getMediaLiveRecorderEndTime(int64_t ingestionJobKey, int64_t encodingJobKey,
			string mediaLiveRecorderFileName);
	bool liveRecorder_isLastLiveRecorderFile(int64_t ingestionJobKey, int64_t encodingJobKey,
			time_t currentRecordedFileCreationTime, string transcoderStagingContentsPath,
			string recordedFileNamePrefix, int segmentDurationInSeconds, bool isFirstChunk);
	void liveRecorder_ingestRecordedMedia(
		int64_t ingestionJobKey,
		string transcoderStagingContentsPath, string currentRecordedAssetFileName,
		string stagingContentsPath,
		string addContentTitle,
		string uniqueName,
		bool highAvailability,
		Json::Value userDataRoot,
		string fileFormat,
		Json::Value liveRecorderParametersRoot);

	void liveProxy(
        // FCGX_Request& request,
        shared_ptr<LiveProxyAndGrid> liveProxy,
        int64_t encodingJobKey,
        string requestBody);

	void liveGrid(
        // FCGX_Request& request,
        shared_ptr<LiveProxyAndGrid> liveProxy,
        int64_t encodingJobKey,
        string requestBody);

	void videoSpeed(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody);

	void pictureInPicture(
        // FCGX_Request& request,
        shared_ptr<Encoding> encoding,
        int64_t encodingJobKey,
        string requestBody);

	void addEncodingCompleted(
        int64_t encodingJobKey, bool completedWithError, string errorMessage,
		bool killedByUser, bool urlForbidden, bool urlNotFound);

	void removeEncodingCompletedIfPresent(int64_t encodingJobKey);

	void encodingCompletedRetention();
};

#endif

