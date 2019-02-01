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
#include "catralibraries/MultiEventsSet.h"


#define ENCODERVIDEOAUDIOPROXY                          "EncoderVideoAudioProxy"
#define MMSENGINEPROCESSORNAME                          "MMSEngineProcessor"


struct MaxConcurrentJobsReached: public exception {
    char const* what() const throw() 
    {
        return "Encoder reached the max number of concurrent jobs";
    }; 
};

struct NoEncodingJobKeyFound: public exception {
    char const* what() const throw() 
    {
        return "No Encoding Job Key Found";
    }; 
};

struct EncoderError: public exception {
    char const* what() const throw() 
    {
        return "Encoder error";
    }; 
};

enum class EncodingJobStatus
{
    Free,
    ToBeRun,
    Running
};

struct EncodingStatusNotAvailable: public exception {
    char const* what() const throw() 
    {
        return "Encoding status not available";
    }; 
};

class EncoderVideoAudioProxy {
public:
    EncoderVideoAudioProxy();

    virtual ~EncoderVideoAudioProxy();
    
    void init(
        int proxyIdentifier, mutex* mtEncodingJobs,
        Json::Value configuration,
        shared_ptr<MultiEventsSet> multiEventsSet,
        shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade,
        shared_ptr<MMSStorage> mmsStorage,
        shared_ptr<EncodersLoadBalancer> encodersLoadBalancer,
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

	int getEncodingProgress();

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
    
    string                              _mp4Encoder;
    string                              _mpeg2TSEncoder;
    int                                 _intervalInSecondsToCheckEncodingFinished;
	int									_secondsToWaitNFSBuffers;
    
    string                              _ffmpegEncoderProtocol;
    int                                 _ffmpegEncoderPort;
    string                              _ffmpegEncoderUser;
    string                              _ffmpegEncoderPassword;
    string                              _ffmpegEncoderProgressURI;
    string                              _ffmpegEncoderStatusURI;
    string                              _ffmpegEncodeURI;
    string                              _ffmpegOverlayImageOnVideoURI;
    string                              _ffmpegOverlayTextOnVideoURI;
    string                              _ffmpegGenerateFramesURI;
    string                              _ffmpegSlideShowURI;
    string                              _ffmpegLiveRecorderURI;
    
    #ifdef __LOCALENCODER__
        shared_ptr<FFMpeg>              _ffmpeg;
        int*                            _pRunningEncodingsNumber;
        int                             _ffmpegMaxCapacity;
    #else
        string                          _currentUsedFFMpegEncoderHost;
    #endif

	// used only in case of face recognition/identification video generation
	int						_localEncodingProgress;

    string					_computerVisionCascadePath;
    double					_computerVisionDefaultScale;
    int						_computerVisionDefaultMinNeighbors;
    bool					_computerVisionDefaultTryFlip;

    string encodeContentVideoAudio();
    string encodeContent_VideoAudio_through_ffmpeg();

    int64_t processEncodedContentVideoAudio(string stagingEncodedAssetPathName);    

    string overlayImageOnVideo();
    string overlayImageOnVideo_through_ffmpeg();
    void processOverlayedImageOnVideo(string stagingEncodedAssetPathName);    

    string overlayTextOnVideo();
    string overlayTextOnVideo_through_ffmpeg();
    void processOverlayedTextOnVideo(string stagingEncodedAssetPathName);    
    
    void generateFrames();
    void generateFrames_through_ffmpeg();
    void processGeneratedFrames();

    string slideShow();
    string slideShow_through_ffmpeg();
    void processSlideShow(string stagingEncodedAssetPathName);    

    string faceRecognition();
    void processFaceRecognition(string stagingEncodedAssetPathName);    

    string faceIdentification();
    void processFaceIdentification(string stagingEncodedAssetPathName);    

    string liveRecorder();
    string liveRecorder_through_ffmpeg();
    void processLiveRecorder(string stagingEncodedAssetPathName);    
	string processLastGeneratedLiveRecorderFiles(string segmentListPathName,
		string contentsPath, string lastRecordedAssetFileName);

    bool getEncodingStatus();

    string generateMediaMetadataToIngest(
        int64_t ingestionJobKey,
        string fileFormat,
        Json::Value parametersRoot);
};

#endif

