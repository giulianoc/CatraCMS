/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   EnodingsManager.h
 * Author: giuliano
 *
 * Created on February 4, 2018, 7:18 PM
 */

#ifndef EncoderVideoAudioProxy_h
#define EncoderVideoAudioProxy_h

#ifdef __LOCALENCODER__
#include "FFMpeg.h"
#endif
#include "MMSEngineDBFacade.h"
#include "MMSStorage.h"
#include "EncodersLoadBalancer.h"
#include "spdlog/spdlog.h"
#include "Magick++.h"
#include "catralibraries/MultiEventsSet.h"


#define ENCODERVIDEOAUDIOPROXY                          "EncoderVideoAudioProxy"
#define MMSENGINEPROCESSORNAME                          "MMSEngineProcessor"


struct YouTubeURLNotRetrieved: public exception {
    char const* what() const throw() 
    {
        return "YouTube URL was not retrieved";
    }; 
};

struct MaxConcurrentJobsReached: public exception {
    char const* what() const throw() 
    {
        return "Encoder reached the max number of concurrent jobs";
    }; 
};

struct EncoderError: public exception {
    char const* what() const throw() 
    {
        return "Encoder error";
    }; 
};

struct EncodingKilledByUser: public exception {
    char const* what() const throw()
    {
        return "Encoding was killed by the User";
    };
};

struct EncoderNotReachable: public exception {
    char const* what() const throw()
    {
        return "Encoder not reachable";
    };
};

/*
struct EncodingStatusNotAvailable: public exception {
    char const* what() const throw() 
    {
        return "Encoding status not available";
    }; 
};
*/

class EncoderVideoAudioProxy {
public:
	enum class EncodingJobStatus
	{
		Free,
		ToBeRun,
		GoingToRun,	// EncoderVideoAudioProxy thread created but still not confirmed by Encoder process (ffmpegEncoder.fcgi)
		Running
	};
	static const char* toString(const EncodingJobStatus& encodingJobStatus)
	{
		switch (encodingJobStatus)
		{
			case EncodingJobStatus::Free:
				return "Free";
			case EncodingJobStatus::ToBeRun:
				return "ToBeRun";
			case EncodingJobStatus::GoingToRun:
				return "GoingToRun";
			case EncodingJobStatus::Running:
				return "Running";
			default:
				throw runtime_error(string("Wrong encodingJobStatus"));
		}
	}

    EncoderVideoAudioProxy();

    virtual ~EncoderVideoAudioProxy();
    
    void init(
        int proxyIdentifier, mutex* mtEncodingJobs,
        Json::Value configuration,
        shared_ptr<MultiEventsSet> multiEventsSet,
        shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade,
        shared_ptr<MMSStorage> mmsStorage,
        shared_ptr<EncodersLoadBalancer> encodersLoadBalancer,
		shared_ptr<long> faceRecognitionNumber,
		int maxFaceRecognitionNumber,
        #ifdef __LOCALENCODER__
            int* pRunningEncodingsNumber,
        #else
        #endif
        shared_ptr<spdlog::logger> logger);
    
    void setEncodingData(
        EncodingJobStatus* status,
        shared_ptr<MMSEngineDBFacade::EncodingItem> encodingItem
    );

    void operator ()();

	// int getEncodingProgress();

private:
    shared_ptr<spdlog::logger>          _logger;
    int                                 _proxyIdentifier;
    Json::Value                         _configuration;
    mutex*                              _mtEncodingJobs;
    EncodingJobStatus*                  _status;
    shared_ptr<MultiEventsSet>          _multiEventsSet;
    shared_ptr<MMSEngineDBFacade>       _mmsEngineDBFacade;
    shared_ptr<MMSStorage>              _mmsStorage;
    shared_ptr<EncodersLoadBalancer>    _encodersLoadBalancer;
    shared_ptr<MMSEngineDBFacade::EncodingItem> _encodingItem;
	string								_hostName;

	string								_liveRecorderVirtualVODImageLabel;
    
	shared_ptr<long>					_faceRecognitionNumber;
	int									_maxFaceRecognitionNumber;

	int									_maxSecondsToWaitUpdateEncodingJobLock;

    string                              _mp4Encoder;
    string                              _mpeg2TSEncoder;
    int                                 _intervalInSecondsToCheckEncodingFinished;
    
    // string                              _ffmpegEncoderProtocol;
    // int                                 _ffmpegEncoderPort;
    string                              _ffmpegEncoderUser;
    string                              _ffmpegEncoderPassword;
	int									_ffmpegEncoderTimeoutInSeconds;
    string                              _ffmpegEncoderProgressURI;
    string                              _ffmpegEncoderStatusURI;
    string								_ffmpegEncoderKillEncodingURI;
    string                              _ffmpegEncodeURI;
    string                              _ffmpegOverlayImageOnVideoURI;
    string                              _ffmpegOverlayTextOnVideoURI;
    string                              _ffmpegGenerateFramesURI;
    string                              _ffmpegSlideShowURI;
    string                              _ffmpegLiveRecorderURI;
    string                              _ffmpegLiveProxyURI;
    // string                              _ffmpegVODProxyURI;
    // string								_ffmpegAwaitingTheBeginningURI;
    string                              _ffmpegLiveGridURI;
    string                              _ffmpegVideoSpeedURI;
    string                              _ffmpegPictureInPictureURI;
    string								_ffmpegIntroOutroOverlayURI;
    string								_ffmpegCutFrameAccurateURI;

	int									_timeBeforeToPrepareResourcesInMinutes;
    
    int									_waitingNFSSync_maxMillisecondsToWait;
    int									_waitingNFSSync_milliSecondsWaitingBetweenChecks;

	long								_retrieveStreamingYouTubeURLPeriodInHours;
	int									_maxEncoderNotReachableFailures;

	string					_keyPairId;
	string					_privateKeyPEMPathName;

    #ifdef __LOCALENCODER__
        shared_ptr<FFMpeg>              _ffmpeg;
        int*                            _pRunningEncodingsNumber;
        int                             _ffmpegMaxCapacity;
    #else
        string                          _currentUsedFFMpegEncoderHost;
		int64_t							_currentUsedFFMpegEncoderKey;
		bool							_currentUsedFFMpegExternalEncoder;
    #endif

	// used only in case of face recognition/identification video generation
	int						_localEncodingProgress;

    string					_computerVisionCascadePath;
    double					_computerVisionDefaultScale;
    int						_computerVisionDefaultMinNeighbors;
    bool					_computerVisionDefaultTryFlip;

	void encodeContentImage();
	void processEncodedImage();
	void readingImageProfile(
        Json::Value encodingProfileRoot,
        string& newFormat,
        int& newWidth,
        int& newHeight,
        bool& newAspectRatio,
        string& sNewInterlaceType,
        Magick::InterlaceType& newInterlaceType);
	void encodingImageFormatValidation(string newFormat);
	Magick::InterlaceType encodingImageInterlaceTypeValidation(string sNewInterlaceType);

    void encodeContentVideoAudio();
    bool encodeContent_VideoAudio_through_ffmpeg();
    void processEncodedContentVideoAudio();    

    bool overlayImageOnVideo();
    bool overlayImageOnVideo_through_ffmpeg();
    void processOverlayedImageOnVideo(bool killedByUser);    

    bool overlayTextOnVideo();
    bool overlayTextOnVideo_through_ffmpeg();
    void processOverlayedTextOnVideo(bool killedByUser);    
    
    bool generateFrames();
    bool generateFrames_through_ffmpeg();
    void processGeneratedFrames(bool killedByUser);

    pair<string, bool> slideShow();
    pair<string, bool> slideShow_through_ffmpeg();
    void processSlideShow(string stagingEncodedAssetPathName, bool killedByUser);    

    string faceRecognition();
    void processFaceRecognition(string stagingEncodedAssetPathName);    

    string faceIdentification();
    void processFaceIdentification(string stagingEncodedAssetPathName);    

    bool liveRecorder();
    bool liveRecorder_through_ffmpeg();
    void processLiveRecorder(bool killedByUser);    
	/*
	bool isLastLiveRecorderFile(time_t currentRecordedFileCreationTime, string contentsPath,
			string recordedFileNamePrefix);
	time_t getMediaLiveRecorderStartTime(string mediaLiveRecorderFileName);
	time_t getMediaLiveRecorderEndTime(string mediaLiveRecorderFileName);
	string processLastGeneratedLiveRecorderFiles(bool highAvailability, bool main, int segmentDurationInSeconds,
		string segmentListPathName, string recordedFileNamePrefix, string contentsPath, string lastRecordedAssetFileName);
	void ingestRecordedMedia(
		string currentRecordedAssetPathName,
		string title,
		Json::Value userDataRoot,
		string fileFormat,
		Json::Value liveRecorderParametersRoot);
	*/

	bool liveProxy(string proxyType);
	bool liveProxy_through_ffmpeg(string proxyType);
    void processLiveProxy(bool killedByUser);

	// bool vodProxy();
	// bool vodProxy_through_ffmpeg();
    // void processVODProxy(bool killedByUser);

	// bool awaitingTheBeginning();
	// bool awaitingTheBeginning_through_ffmpeg();
    // void processAwaitingTheBeginning(bool killedByUser);

	pair<long,string> getLastYouTubeURLDetails(
		int64_t ingestionKey,
		int64_t encodingJobKey,
		int64_t workspaceKey,
		int64_t liveURLConfKey);

	void updateChannelDataWithNewYouTubeURL(
		int64_t ingestionKey,
		int64_t encodingJobKey,
		int64_t workspaceKey,
		int64_t liveURLConfKey,
		string streamingYouTubeLiveURL);

	bool liveGrid();
	bool liveGrid_through_ffmpeg();
    void processLiveGrid(bool killedByUser);

    bool videoSpeed();
    bool videoSpeed_through_ffmpeg();
    void processVideoSpeed(bool killedByUser);    

    pair<string, bool> pictureInPicture();
    pair<string, bool> pictureInPicture_through_ffmpeg();
    void processPictureInPicture(string stagingEncodedAssetPathName, bool killedByUser);    

    pair<string, bool> introOutroOverlay();
    pair<string, bool> introOutroOverlay_through_ffmpeg();
    void processIntroOutroOverlay(string stagingEncodedAssetPathName, bool killedByUser);    

    pair<string, bool> cutFrameAccurate();
    pair<string, bool> cutFrameAccurate_through_ffmpeg();
    void processCutFrameAccurate(string stagingEncodedAssetPathName, bool killedByUser);    

    tuple<bool, bool, bool, string, bool, bool, int, int> getEncodingStatus();

    string generateMediaMetadataToIngest(
        int64_t ingestionJobKey,
        string fileFormat,
		int64_t faceOfVideoMediaItemKey,
		int64_t cutOfVideoMediaItemKey, double startTimeInSeconds, double endTimeInSeconds,
        Json::Value parametersRoot);

	// void killEncodingJob(string transcoderHost, int64_t encodingJobKey);
	void awsStartChannel(int64_t ingestionJobKey,
		string awsChannelIdToBeStarted);

	void awsStopChannel(int64_t ingestionJobKey,
		string awsChannelIdToBeStarted);

	string getAWSSignedURL(string playURL, int expirationInMinutes);

	bool waitingEncoding(int maxConsecutiveEncodingStatusFailures);
};

#endif

