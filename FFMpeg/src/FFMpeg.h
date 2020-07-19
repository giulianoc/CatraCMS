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

#ifndef FFMpeg_h
#define FFMpeg_h

#include <string>
#include "spdlog/spdlog.h"
#include "json/json.h"

#ifndef __FILEREF__
    #ifdef __APPLE__
        #define __FILEREF__ string("[") + string(__FILE__).substr(string(__FILE__).find_last_of("/") + 1) + ":" + to_string(__LINE__) + "] "
    #else
        #define __FILEREF__ string("[") + basename((char *) __FILE__) + ":" + to_string(__LINE__) + "] "
    #endif
#endif

using namespace std;

struct FFMpegEncodingStatusNotAvailable: public exception {
    char const* what() const throw() 
    {
        return "Encoding status not available";
    }; 
};

struct FFMpegEncodingKilledByUser: public exception {
    char const* what() const throw() 
    {
        return "Encoding was killed by the User";
    }; 
};

struct FFMpegURLForbidden: public exception {
    char const* what() const throw() 
    {
        return "URL Forbidden";
    }; 
};

struct FFMpegURLNotFound: public exception {
    char const* what() const throw() 
    {
        return "URL Not Found";
    }; 
};

struct NoEncodingJobKeyFound: public exception {
    char const* what() const throw() 
    {
        return "No encoding job key found";
    }; 
};

struct NoEncodingAvailable: public exception {
    char const* what() const throw() 
    {
        return "No encoding available";
    }; 
};

class FFMpeg {
public:
    FFMpeg(Json::Value configuration,
            shared_ptr<spdlog::logger> logger);
    
    ~FFMpeg();

    void encodeContent(
        string mmsSourceAssetPathName,
        int64_t durationInMilliSeconds,
        // string encodedFileName,
        string stagingEncodedAssetPathName,
        Json::Value encodingProfileDetailsRoot,
        bool isVideo,   // if false it means is audio
		Json::Value videoTracksRoot,
		Json::Value audioTracksRoot,
        int64_t physicalPathKey,
        string customerDirectoryName,
        string relativePath,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid);
    
    void overlayImageOnVideo(
        string mmsSourceVideoAssetPathName,
        int64_t videoDurationInMilliSeconds,
        string mmsSourceImageAssetPathName,
        string imagePosition_X_InPixel,
        string imagePosition_Y_InPixel,
        // string encodedFileName,
        string stagingEncodedAssetPathName,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid);

    void overlayTextOnVideo(
        string mmsSourceVideoAssetPathName,
        int64_t videoDurationInMilliSeconds,

        string text,
        string textPosition_X_InPixel,
        string textPosition_Y_InPixel,
        string fontType,
        int fontSize,
        string fontColor,
        int textPercentageOpacity,
        bool boxEnable,
        string boxColor,
        int boxPercentageOpacity,

        // string encodedFileName,
        string stagingEncodedAssetPathName,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid);

	void videoSpeed(
        string mmsSourceVideoAssetPathName,
        int64_t videoDurationInMilliSeconds,

        string videoSpeedType,
        int videoSpeedSize,

        // string encodedFileName,
        string stagingEncodedAssetPathName,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid);

	void pictureInPicture(
        string mmsMainVideoAssetPathName,
        int64_t mainVideoDurationInMilliSeconds,
        string mmsOverlayVideoAssetPathName,
        int64_t overlayVideoDurationInMilliSeconds,
        bool soundOfMain,
        string overlayPosition_X_InPixel,
        string overlayPosition_Y_InPixel,
        string overlay_Width_InPixel,
        string overlay_Height_InPixel,
        string stagingEncodedAssetPathName,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid);

    int getEncodingProgress();

	bool nonMonotonousDTSInOutputLog();
	bool forbiddenErrorInOutputLog();
	bool isFrameIncreasing(int secondsToWaitBetweenSamples);

    // tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long> getMediaInfo(string mmsAssetPathName);

	pair<int64_t, long> getMediaInfo(string mmsAssetPathName,
		vector<tuple<int, int64_t, string, string, int, int, string, long>>& videoTracks,
		vector<tuple<int, int64_t, string, long, int, long, string>>& audioTracks);

    vector<string> generateFramesToIngest(
        int64_t ingestionJobKey,
        int64_t encodingJobKey,
        string imageDirectory,
        string imageBaseFileName,
        double startTimeInSeconds,
        int framesNumber,
        string videoFilter,
        int periodInSeconds,
        bool mjpeg,
        int imageWidth,
        int imageHeight,
        string mmsAssetPathName,
        int64_t videoDurationInMilliSeconds,
		pid_t* pChildPid);

    void generateConcatMediaToIngest(
        int64_t ingestionJobKey,
        vector<string>& sourcePhysicalPaths,
        string concatenatedMediaPathName);

    void generateSlideshowMediaToIngest(
        int64_t ingestionJobKey,
        int64_t encodingJobKey,
        vector<string>& sourcePhysicalPaths,
        double durationOfEachSlideInSeconds, 
        int outputFrameRate,
        string slideshowMediaPathName,
		pid_t* pChildPid);

    void generateCutMediaToIngest(
        int64_t ingestionJobKey,
        string sourcePhysicalPath,
		bool keyFrameSeeking,
        double startTimeInSeconds,
        double endTimeInSeconds,
        int framesNumber,
        string cutMediaPathName);

    void extractTrackMediaToIngest(
        int64_t ingestionJobKey,
        string sourcePhysicalPath,
        vector<pair<string,int>>& tracksToBeExtracted,
        string extractTrackMediaPathName);

	void liveRecorder(
        int64_t ingestionJobKey,
        int64_t encodingJobKey,
		string segmentListPathName,
		string recordedFileNamePrefix,
        string liveURL, string userAgent,
        time_t utcRecordingPeriodStart, 
        time_t utcRecordingPeriodEnd, 
        int segmentDurationInSeconds,
        string outputFileFormat,
		pid_t* pChildPid);

	void liveProxyByHTTPStreaming(
		int64_t ingestionJobKey,
		int64_t encodingJobKey,
		string liveURL, string userAgent,
		string otherOutputOptions,

		string outputType,	// HLS or DASH

		// next are parameters for the output
		int segmentDurationInSeconds,
		int playlistEntriesNumber,
		string manifestDirectoryPath,                                                                        
		string manifestFileName,
		pid_t* pChildPid);

	void liveProxyByCDN(
		int64_t ingestionJobKey,
		int64_t encodingJobKey,
		string liveURL, string userAgent,
		double itsoffset,
		string otherOutputOptions,
		string cdnURL,
		pid_t* pChildPid);

	void liveGridByHTTPStreaming(
		int64_t ingestionJobKey,
		int64_t encodingJobKey,
		Json::Value encodingProfileDetailsRoot,
		string userAgent,
		Json::Value inputChannelsRoot,	// name,url
		int gridColumns,
		int gridWidth,  // i.e.: 1024
		int gridHeight, // i.e.: 578

		string outputType,  // HLS or DASH (only HLS is supported)

		// next are parameters for the output
		int segmentDurationInSeconds,
		int playlistEntriesNumber,
		string manifestDirectoryPath,
		string manifestFileName,
		pid_t* pChildPid);

	void changeFileFormat(
		int64_t ingestionJobKey,
		int64_t sourceKey,
		string sourcePhysicalPath,
		string destinationPathName);

    static void encodingFileFormatValidation(string fileFormat,
        shared_ptr<spdlog::logger> logger);

    static void encodingAudioCodecValidation(string codec,
        shared_ptr<spdlog::logger> logger);

    static void encodingVideoProfileValidation(
        string codec, string profile,
        shared_ptr<spdlog::logger> logger);

    static void encodingVideoCodecValidation(string codec,
        shared_ptr<spdlog::logger> logger);

	pair<string, string> retrieveStreamingYouTubeURL(
		int64_t ingestionJobKey, int64_t encodingJobKey,
		string youTubeURL);

private:
    shared_ptr<spdlog::logger>  _logger;
    string          _ffmpegPath;
    string          _ffmpegTempDir;
    string          _ffmpegTtfFontDir;
    int             _charsToBeReadFromFfmpegErrorOutput;
    bool            _twoPasses;
    string          _outputFfmpegPathFileName;
    bool            _currentlyAtSecondPass;
    
	string			_youTubeDlPath;

	string			_currentApiName;

    int64_t         _currentDurationInMilliSeconds;
    string          _currentMMSSourceAssetPathName;
    string          _currentStagingEncodedAssetPathName;
    int64_t         _currentIngestionJobKey;
    int64_t         _currentEncodingJobKey;

    int				_waitingNFSSync_attemptNumber;
    int				_waitingNFSSync_sleepTimeInSeconds;

    void settingFfmpegParameters(
        Json::Value encodingProfileDetailsRoot,
        bool isVideo,   // if false it means is audio
        
		string& httpStreamingFileFormat,
		string& ffmpegHttpStreamingParameter,

        string& ffmpegFileFormatParameter,

        string& ffmpegVideoCodecParameter,
        string& ffmpegVideoProfileParameter,
        string& ffmpegVideoResolutionParameter,
		int& videoBitRateInKbps,
        string& ffmpegVideoBitRateParameter,
        string& ffmpegVideoOtherParameters,
        bool& twoPasses,
        string& ffmpegVideoMaxRateParameter,
        string& ffmpegVideoBufSizeParameter,
        string& ffmpegVideoFrameRateParameter,
        string& ffmpegVideoKeyFramesRateParameter,

        string& ffmpegAudioCodecParameter,
        string& ffmpegAudioBitRateParameter,
        string& ffmpegAudioOtherParameters,
        string& ffmpegAudioChannelsParameter,
        string& ffmpegAudioSampleRateParameter
    );
	void addToArguments(string parameter, vector<string>& argumentList);
    
    string getLastPartOfFile(
        string pathFileName, int lastCharsToBeRead);
    
    bool isMetadataPresent(Json::Value root, string field);

	int asInt(Json::Value root, string field = "", int defaultValue = 0);

	int64_t asInt64(Json::Value root, string field = "", int64_t defaultValue = 0);

	double asDouble(Json::Value root, string field = "", double defaultValue = 0.0);

	bool asBool(Json::Value root, string field, bool defaultValue);

    void removeHavingPrefixFileName(string directoryName, string prefixFileName);

	long getFrameByOutputLog(string ffmpegEncodingStatus);
};

#endif

