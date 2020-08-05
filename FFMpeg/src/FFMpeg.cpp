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
#include <sstream>
#include <regex>
#include "catralibraries/ProcessUtility.h"
#include "catralibraries/FileIO.h"
#include "catralibraries/StringUtils.h"
#include "FFMpeg.h"


FFMpeg::FFMpeg(Json::Value configuration,
        shared_ptr<spdlog::logger> logger) 
{
    _logger             = logger;

    _ffmpegPath = configuration["ffmpeg"].get("path", "").asString();
    _ffmpegTempDir = configuration["ffmpeg"].get("tempDir", "").asString();
    _ffmpegTtfFontDir = configuration["ffmpeg"].get("ttfFontDir", "").asString();

    _youTubeDlPath = configuration["youTubeDl"].get("path", "").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", youTubeDl->path: " + _youTubeDlPath
    );

    _waitingNFSSync_attemptNumber = asInt(configuration["storage"],
		"waitingNFSSync_attemptNumber", 1);
	/*
    _logger->info(__FILEREF__ + "Configuration item"
        + ", storage->waitingNFSSync_attemptNumber: "
		+ to_string(_waitingNFSSync_attemptNumber)
    );
	*/
    _waitingNFSSync_sleepTimeInSeconds = asInt(configuration["storage"],
		"waitingNFSSync_sleepTimeInSeconds", 3);
	/*
    _logger->info(__FILEREF__ + "Configuration item"
        + ", storage->waitingNFSSync_sleepTimeInSeconds: "
		+ to_string(_waitingNFSSync_sleepTimeInSeconds)
    );
	*/

    _startCheckingFrameInfoInMinutes = asInt(configuration["ffmpeg"],
		"startCheckingFrameInfoInMinutes", 5);

    _charsToBeReadFromFfmpegErrorOutput     = 2024;
    
    _twoPasses = false;
    _currentlyAtSecondPass = false;

    _currentIngestionJobKey             = -1;	// just for log
    _currentEncodingJobKey              = -1;	// just for log
    _currentDurationInMilliSeconds      = -1;	// in case of some functionalities, it is important for getEncodingProgress
    _currentMMSSourceAssetPathName      = "";	// just for log
    _currentStagingEncodedAssetPathName = "";	// just for log

	_startFFMpegMethod = chrono::system_clock::now();
}

FFMpeg::~FFMpeg() 
{
    
}

void FFMpeg::encodeContent(
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
		pid_t* pChildPid)
{
	int iReturnedStatus = 0;

	_currentApiName = "encodeContent";

	setStatus(
		ingestionJobKey,
		encodingJobKey,
		durationInMilliSeconds,
		mmsSourceAssetPathName,
		stagingEncodedAssetPathName
	);

    try
    {
		_logger->info(__FILEREF__ + "Received " + _currentApiName
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
			+ ", videoTracksRoot.size: " + to_string(videoTracksRoot.size())
			+ ", audioTracksRoot.size: " + to_string(audioTracksRoot.size())
		);

        string httpStreamingFileFormat;    
		string ffmpegHttpStreamingParameter = "";

        string ffmpegFileFormatParameter = "";

        string ffmpegVideoCodecParameter = "";
        string ffmpegVideoProfileParameter = "";
        string ffmpegVideoResolutionParameter = "";
		int videoBitRateInKbps = -1;
        string ffmpegVideoBitRateParameter = "";
        string ffmpegVideoOtherParameters = "";
        string ffmpegVideoMaxRateParameter = "";
        string ffmpegVideoBufSizeParameter = "";
        string ffmpegVideoFrameRateParameter = "";
        string ffmpegVideoKeyFramesRateParameter = "";

        string ffmpegAudioCodecParameter = "";
        string ffmpegAudioBitRateParameter = "";
        string ffmpegAudioOtherParameters = "";
        string ffmpegAudioChannelsParameter = "";
        string ffmpegAudioSampleRateParameter = "";


        // _currentDurationInMilliSeconds      = durationInMilliSeconds;
        // _currentMMSSourceAssetPathName      = mmsSourceAssetPathName;
        // _currentStagingEncodedAssetPathName = stagingEncodedAssetPathName;
        // _currentIngestionJobKey             = ingestionJobKey;
        // _currentEncodingJobKey              = encodingJobKey;
        
        _currentlyAtSecondPass = false;

        // we will set by default _twoPasses to false otherwise, since the ffmpeg class is reused
        // it could remain set to true from a previous call
        _twoPasses = false;
        
        settingFfmpegParameters(
            encodingProfileDetailsRoot,
            isVideo,

            httpStreamingFileFormat,
			ffmpegHttpStreamingParameter,

            ffmpegFileFormatParameter,

            ffmpegVideoCodecParameter,
            ffmpegVideoProfileParameter,
            ffmpegVideoResolutionParameter,
			videoBitRateInKbps,
            ffmpegVideoBitRateParameter,
            ffmpegVideoOtherParameters,
            _twoPasses,
            ffmpegVideoMaxRateParameter,
            ffmpegVideoBufSizeParameter,
            ffmpegVideoFrameRateParameter,
            ffmpegVideoKeyFramesRateParameter,

            ffmpegAudioCodecParameter,
            ffmpegAudioBitRateParameter,
            ffmpegAudioOtherParameters,
            ffmpegAudioChannelsParameter,
            ffmpegAudioSampleRateParameter
        );

        _outputFfmpegPathFileName =
                _ffmpegTempDir + "/"
                + to_string(_currentIngestionJobKey)
                + "_"
                + to_string(_currentEncodingJobKey)
                + ".ffmpegoutput";

		// special case:
		//	- input is mp4
		//	- output is hls
		//	- more than 1 audio track
		//	- one video track
		// In this case we will create:
		//  - one m3u8 for each track (video and audio)
		//  - one main m3u8 having a group for AUDIO
		string suffix = ".mp4";
		if (
			// input is mp4
			mmsSourceAssetPathName.size() >= suffix.size()
			&& 0 == mmsSourceAssetPathName.compare(mmsSourceAssetPathName.size()-suffix.size(), suffix.size(), suffix)

			// output is hls
			&& httpStreamingFileFormat == "hls"

			// more than 1 audio track
			&& audioTracksRoot.size() > 1

			// one video track
			&& videoTracksRoot.size() == 1
		)
		{
			/*
			 * The command will be like this:

			ffmpeg -y -i /var/catramms/storage/MMSRepository/MMS_0000/ws2/000/228/001/1247989_source.mp4

				-map 0:1 -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/ita/1247992_384637_%04d.ts -f hls /home/mms/tmp/ita/1247992_384637.m3u8

				-map 0:2 -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/eng/1247992_384637_%04d.ts -f hls /home/mms/tmp/eng/1247992_384637.m3u8

				-map 0:0 -codec:v libx264 -profile:v high422 -b:v 800k -preset veryfast -level 4.0 -crf 22 -r 25 -vf scale=640:360 -threads 0 -hls_time 10 -hls_list_size 0 -hls_segment_filename /home/mms/tmp/low/1247992_384637_%04d.ts -f hls /home/mms/tmp/low/1247992_384637.m3u8

			Manifest will be like:
			#EXTM3U
			#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="ita",NAME="ita",AUTOSELECT=YES, DEFAULT=YES,URI="ita/8896718_1509416.m3u8"
			#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="eng",NAME="eng",AUTOSELECT=YES, DEFAULT=YES,URI="eng/8896718_1509416.m3u8"
			#EXT-X-STREAM-INF:PROGRAM-ID=1,AUDIO="audio"
			0/8896718_1509416.m3u8


			https://developer.apple.com/documentation/http_live_streaming/example_playlists_for_http_live_streaming/adding_alternate_media_to_a_playlist#overview
			https://github.com/videojs/http-streaming/blob/master/docs/multiple-alternative-audio-tracks.md

			*/

			_logger->info(__FILEREF__ + "Special encoding in order to allow audio/language selection by the player"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
			);

			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;

			{
				bool noErrorIfExists = true;
				bool recursive = true;
				_logger->info(__FILEREF__ + "Creating directory (if needed)"
					+ ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
				);
				FileIO::createDirectory(stagingEncodedAssetPathName,
					S_IRUSR | S_IWUSR | S_IXUSR |
					S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, noErrorIfExists, recursive);

				for (int index = 0; index < audioTracksRoot.size(); index++)
				{
					Json::Value audioTrack = audioTracksRoot[index];

					string audioTrackDirectoryName = audioTrack.get("language", "").asString();

					string audioPathName = stagingEncodedAssetPathName + "/"
						+ audioTrackDirectoryName;

					_logger->info(__FILEREF__ + "Creating directory (if needed)"
						+ ", audioPathName: " + audioPathName
					);
					FileIO::createDirectory(audioPathName,
						S_IRUSR | S_IWUSR | S_IXUSR |
						S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, noErrorIfExists, recursive);
				}

				{
					string videoTrackDirectoryName;
					{
						Json::Value videoTrack = videoTracksRoot[0];

						videoTrackDirectoryName = to_string(videoTrack.get("trackIndex", -1).asInt());
					}

					string videoPathName = stagingEncodedAssetPathName + "/"
						+ videoTrackDirectoryName;

					_logger->info(__FILEREF__ + "Creating directory (if needed)"
						+ ", videoPathName: " + videoPathName
					);
					FileIO::createDirectory(videoPathName,
						S_IRUSR | S_IWUSR | S_IXUSR |
						S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, noErrorIfExists, recursive);
				}
			}

			// the manifestFileName naming convention is used also in EncoderVideoAudioProxy.cpp
			string manifestFileName = to_string(ingestionJobKey)
				+ "_" + to_string(encodingJobKey)
				+ ".m3u8";

            if (_twoPasses)
            {
                string passlogFileName = 
                    to_string(_currentIngestionJobKey)
                    + "_"
                    + to_string(_currentEncodingJobKey) + ".passlog";
                string ffmpegPassLogPathFileName = _ffmpegTempDir // string(stagingEncodedAssetPath)
                    + "/"
                    + passlogFileName
                    ;

                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				for (int index = 0; index < audioTracksRoot.size(); index++)
				{
					Json::Value audioTrack = audioTracksRoot[index];

					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						string("0:") + to_string(audioTrack.get("trackIndex", -1).asInt()));

					addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
					addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

					addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

					string audioTrackDirectoryName = audioTrack.get("language", "").asString();

					{
						string segmentPathFileName =
							stagingEncodedAssetPathName 
							+ "/"
							+ audioTrackDirectoryName
							+ "/"
							+ to_string(_currentIngestionJobKey)
							+ "_"
							+ to_string(_currentEncodingJobKey)
							+ "_%04d.ts"
						;
						ffmpegArgumentList.push_back("-hls_segment_filename");
						ffmpegArgumentList.push_back(segmentPathFileName);
					}

					addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
					{
						string stagingManifestAssetPathName =
							stagingEncodedAssetPathName
							+ "/" + audioTrackDirectoryName
							+ "/" + manifestFileName;
						ffmpegArgumentList.push_back(stagingManifestAssetPathName);
					}
				}

				{
					Json::Value videoTrack = videoTracksRoot[0];

					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						string("0:") + to_string(videoTrack.get("trackIndex", -1).asInt()));
				}
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				ffmpegArgumentList.push_back("-pass");
				ffmpegArgumentList.push_back("1");
				ffmpegArgumentList.push_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				// 2020-01-20: I removed the hls file format parameter because it was not working
				//	and added -f mp4. At the end it has to generate just the log file
				//	to be used in the second step
				// addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);
				//
				// addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-f");
				ffmpegArgumentList.push_back("mp4");

				ffmpegArgumentList.push_back("/dev/null");

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

					_logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command (first step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					);

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
					{
						string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						;            
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					_logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command (first step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
					);
				}
				catch(runtime_error e)
				{
					string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                        _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					_logger->error(errorMessage);

					bool exceptionInCaseOfError = false;
                    removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);

					_logger->info(__FILEREF__ + "Remove"
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
					FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				ffmpegArgumentList.clear();
				ffmpegArgumentListStream.clear();

                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				for (int index = 0; index < audioTracksRoot.size(); index++)
				{
					Json::Value audioTrack = audioTracksRoot[index];

					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						string("0:") + to_string(audioTrack.get("trackIndex", -1).asInt()));

					addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
					addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

					addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

					string audioTrackDirectoryName = audioTrack.get("language", "").asString();

					{
						string segmentPathFileName =
							stagingEncodedAssetPathName 
							+ "/"
							+ audioTrackDirectoryName
							+ "/"
							+ to_string(_currentIngestionJobKey)
							+ "_"
							+ to_string(_currentEncodingJobKey)
							+ "_%04d.ts"
						;
						ffmpegArgumentList.push_back("-hls_segment_filename");
						ffmpegArgumentList.push_back(segmentPathFileName);
					}

					addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
					{
						string stagingManifestAssetPathName =
							stagingEncodedAssetPathName
							+ "/" + audioTrackDirectoryName
							+ "/" + manifestFileName;
						ffmpegArgumentList.push_back(stagingManifestAssetPathName);
					}
				}

				{
					Json::Value videoTrack = videoTracksRoot[0];

					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						string("0:") + to_string(videoTrack.get("trackIndex", -1).asInt()));
				}
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				ffmpegArgumentList.push_back("-pass");
				ffmpegArgumentList.push_back("2");
				ffmpegArgumentList.push_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);

				addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

				string videoTrackDirectoryName;
				{
					Json::Value videoTrack = videoTracksRoot[0];

					videoTrackDirectoryName = to_string(videoTrack.get("trackIndex", -1).asInt());
				}

				{
					string segmentPathFileName =
						stagingEncodedAssetPathName 
						+ "/"
						+ videoTrackDirectoryName
						+ "/"
						+ to_string(_currentIngestionJobKey)
						+ "_"
						+ to_string(_currentEncodingJobKey)
						+ "_%04d.ts"
					;
					ffmpegArgumentList.push_back("-hls_segment_filename");
					ffmpegArgumentList.push_back(segmentPathFileName);
				}

				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				{
					string stagingManifestAssetPathName =
						stagingEncodedAssetPathName
						+ "/" + videoTrackDirectoryName
						+ "/" + manifestFileName;
					ffmpegArgumentList.push_back(stagingManifestAssetPathName);
				}

                _currentlyAtSecondPass = true;
				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

					_logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command (second step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					);

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
					{
						string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						;            
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					_logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command (second step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
					);
				}
				catch(runtime_error e)
				{
					string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                        _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					_logger->error(errorMessage);

					bool exceptionInCaseOfError = false;
                    removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);

					_logger->info(__FILEREF__ + "Remove"
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
					FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				bool exceptionInCaseOfError = false;
				removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);

				_logger->info(__FILEREF__ + "Remove"
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
				FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
			}
			else
            {
                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				for (int index = 0; index < audioTracksRoot.size(); index++)
				{
					Json::Value audioTrack = audioTracksRoot[index];

					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						string("0:") + to_string(audioTrack.get("trackIndex", -1).asInt()));

					addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
					addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

					addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

					string audioTrackDirectoryName = audioTrack.get("language", "").asString();

					{
						string segmentPathFileName =
							stagingEncodedAssetPathName 
							+ "/"
							+ audioTrackDirectoryName
							+ "/"
							+ to_string(_currentIngestionJobKey)
							+ "_"
							+ to_string(_currentEncodingJobKey)
							+ "_%04d.ts"
						;
						ffmpegArgumentList.push_back("-hls_segment_filename");
						ffmpegArgumentList.push_back(segmentPathFileName);
					}

					addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
					{
						string stagingManifestAssetPathName =
							stagingEncodedAssetPathName
							+ "/" + audioTrackDirectoryName
							+ "/" + manifestFileName;
						ffmpegArgumentList.push_back(stagingManifestAssetPathName);
					}
				}

				{
					Json::Value videoTrack = videoTracksRoot[0];

					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						string("0:") + to_string(videoTrack.get("trackIndex", -1).asInt()));
				}
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");

				addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

				string videoTrackDirectoryName;
				{
					Json::Value videoTrack = videoTracksRoot[0];

					videoTrackDirectoryName = to_string(videoTrack.get("trackIndex", -1).asInt());
				}

				{
					string segmentPathFileName =
						stagingEncodedAssetPathName 
						+ "/"
						+ videoTrackDirectoryName
						+ "/"
						+ to_string(_currentIngestionJobKey)
						+ "_"
						+ to_string(_currentEncodingJobKey)
						+ "_%04d.ts"
					;
					ffmpegArgumentList.push_back("-hls_segment_filename");
					ffmpegArgumentList.push_back(segmentPathFileName);
				}

				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				{
					string stagingManifestAssetPathName =
						stagingEncodedAssetPathName
						+ "/" + videoTrackDirectoryName
						+ "/" + manifestFileName;
					ffmpegArgumentList.push_back(stagingManifestAssetPathName);
				}

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

					_logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					);

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
					{
						string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						;            
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					_logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
					);
				}
				catch(runtime_error e)
				{
					string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                        _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					_logger->error(errorMessage);

					bool exceptionInCaseOfError = false;
					_logger->info(__FILEREF__ + "Remove"
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
					FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				bool exceptionInCaseOfError = false;
				_logger->info(__FILEREF__ + "Remove"
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
				FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
			}

			long long llDirSize = -1;
			// if (FileIO::fileExisting(stagingEncodedAssetPathName))
			{
				llDirSize = FileIO::getDirectorySizeInBytes (
					stagingEncodedAssetPathName);
			}

            _logger->info(__FILEREF__ + "Encoded file generated"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
				+ ", llDirSize: " + to_string(llDirSize)
				+ ", _twoPasses: " + to_string(_twoPasses)
            );

            if (llDirSize == 0)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded dir size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }

			// create manifest file
			{
				string mainManifestPathName = stagingEncodedAssetPathName + "/"
					+ manifestFileName;

				string mainManifest;

				mainManifest = string("#EXTM3U") + "\n";

				for (int index = 0; index < audioTracksRoot.size(); index++)
				{
					Json::Value audioTrack = audioTracksRoot[index];

					string audioTrackDirectoryName = audioTrack.get("language", "").asString();

					string audioLanguage = audioTrack.get("language", "").asString();

					string audioManifestLine = "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",LANGUAGE=\""
						+ audioLanguage + "\",NAME=\"" + audioLanguage + "\",AUTOSELECT=YES, DEFAULT=YES,URI=\""
						+ audioTrackDirectoryName + "/" + manifestFileName + "\"";
						
					mainManifest += (audioManifestLine + "\n");
				}

				string videoManifestLine = "#EXT-X-STREAM-INF:PROGRAM-ID=1,AUDIO=\"audio\"";
				mainManifest += (videoManifestLine + "\n");

				string videoTrackDirectoryName;
				{
					Json::Value videoTrack = videoTracksRoot[0];

					videoTrackDirectoryName = to_string(videoTrack.get("trackIndex", -1).asInt());
				}
				mainManifest += (videoTrackDirectoryName + "/" + manifestFileName + "\n");

				ofstream manifestFile(mainManifestPathName);
				manifestFile << mainManifest;
			}
        }
		else if (httpStreamingFileFormat != "")
        {
			// hls or dash

			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;

			{
				bool noErrorIfExists = true;
				bool recursive = true;
				_logger->info(__FILEREF__ + "Creating directory (if needed)"
					+ ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
				);
				FileIO::createDirectory(stagingEncodedAssetPathName,
					S_IRUSR | S_IWUSR | S_IXUSR |
					S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, noErrorIfExists, recursive);
			}

			// the manifestFileName naming convention is used also in EncoderVideoAudioProxy.cpp
			string manifestFileName = to_string(ingestionJobKey) +
				"_" + to_string(encodingJobKey);
			if (httpStreamingFileFormat == "hls")
				manifestFileName += ".m3u8";
			else	// if (httpStreamingFileFormat == "dash")
				manifestFileName += ".mpd";

            if (_twoPasses)
            {
                string passlogFileName = 
                    to_string(_currentIngestionJobKey)
                    + "_"
                    + to_string(_currentEncodingJobKey) + ".passlog";
                string ffmpegPassLogPathFileName = _ffmpegTempDir // string(stagingEncodedAssetPath)
                    + "/"
                    + passlogFileName
                    ;

                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				ffmpegArgumentList.push_back("-pass");
				ffmpegArgumentList.push_back("1");
				ffmpegArgumentList.push_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//  it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//  So, this is the reason, I'm adding phase 2 as well
				// + "-an "    // disable audio
				addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

				// 2020-01-20: I removed the hls file format parameter because it was not working
				//	and added -f mp4. At the end it has to generate just the log file
				//	to be used in the second step
				// addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);
				//
				// addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-f");
				ffmpegArgumentList.push_back("mp4");

				ffmpegArgumentList.push_back("/dev/null");

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

					_logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command (first step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					);

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
					{
						string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						;            
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					_logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command (first step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
					);
				}
				catch(runtime_error e)
				{
					string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                        _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					_logger->error(errorMessage);

					bool exceptionInCaseOfError = false;
                    removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);

					_logger->info(__FILEREF__ + "Remove"
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
					FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				string segmentPathFileName;
				if (httpStreamingFileFormat == "hls")
					segmentPathFileName =
						stagingEncodedAssetPathName 
						+ "/"
						+ to_string(_currentIngestionJobKey)
						+ "_"
						+ to_string(_currentEncodingJobKey)
						+ "_%04d.ts"
					;

				string stagingManifestAssetPathName =
					stagingEncodedAssetPathName
					+ "/" + manifestFileName;

				ffmpegArgumentList.clear();
				ffmpegArgumentListStream.clear();

				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				ffmpegArgumentList.push_back("-pass");
				ffmpegArgumentList.push_back("2");
				ffmpegArgumentList.push_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

				addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

				if (httpStreamingFileFormat == "hls")
				{
					ffmpegArgumentList.push_back("-hls_segment_filename");
					ffmpegArgumentList.push_back(segmentPathFileName);
				}

				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingManifestAssetPathName);

                _currentlyAtSecondPass = true;
				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

					_logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command (second step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					);

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
					{
						string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						;            
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					_logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command (second step)"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
					);
				}
				catch(runtime_error e)
				{
					string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                        _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					_logger->error(errorMessage);

					bool exceptionInCaseOfError = false;
                    removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);

					_logger->info(__FILEREF__ + "Remove"
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
					FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				bool exceptionInCaseOfError = false;
				removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);

				_logger->info(__FILEREF__ + "Remove"
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
				FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
			}
			else
            {
				string segmentPathFileName;
				if (httpStreamingFileFormat == "hls")
					segmentPathFileName =
						stagingEncodedAssetPathName 
						+ "/"
						+ to_string(_currentIngestionJobKey)
						+ "_"
						+ to_string(_currentEncodingJobKey)
						+ "_%04d.ts"
					;

				string stagingManifestAssetPathName =
					stagingEncodedAssetPathName
					+ "/" + manifestFileName;

				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

				addToArguments(ffmpegHttpStreamingParameter, ffmpegArgumentList);

				if (httpStreamingFileFormat == "hls")
				{
					ffmpegArgumentList.push_back("-hls_segment_filename");
					ffmpegArgumentList.push_back(segmentPathFileName);
				}

				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingManifestAssetPathName);

				try
				{
					chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

					_logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					);

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
					{
						string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						;            
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}

					chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

					_logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command"
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
						+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
					);
				}
				catch(runtime_error e)
				{
					string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                        _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					_logger->error(errorMessage);

					bool exceptionInCaseOfError = false;
					_logger->info(__FILEREF__ + "Remove"
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
					FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
				}

				bool exceptionInCaseOfError = false;
				_logger->info(__FILEREF__ + "Remove"
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
				FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
			}

			long long llDirSize = -1;
			// if (FileIO::fileExisting(stagingEncodedAssetPathName))
			{
				llDirSize = FileIO::getDirectorySizeInBytes (
					stagingEncodedAssetPathName);
			}

            _logger->info(__FILEREF__ + "Encoded file generated"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
				+ ", llDirSize: " + to_string(llDirSize)
				+ ", _twoPasses: " + to_string(_twoPasses)
            );

            if (llDirSize == 0)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded dir size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }

            // changes to be done to the manifest, see EncoderThread.cpp
        }
        else
        {
			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;

            if (_twoPasses)
            {
                string passlogFileName = 
                    to_string(_currentIngestionJobKey)
                    + "_"
                    + to_string(_currentEncodingJobKey) + ".passlog";
                string ffmpegPassLogPathFileName = _ffmpegTempDir // string(stagingEncodedAssetPath)
                    + "/"
                    + passlogFileName
                    ;

                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>

				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				ffmpegArgumentList.push_back("-pass");
				ffmpegArgumentList.push_back("1");
				ffmpegArgumentList.push_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				// It should be useless to add the audio parameters in phase 1 but,
				// it happened once that the passed 2 failed. Looking on Internet (https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=2464)
				//	it suggested to add the audio parameters too in phase 1. Really, adding the audio prameters, phase 2 was successful.
				//	So, this is the reason, I'm adding phase 2 as well
                // + "-an "    // disable audio
				addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("/dev/null");

                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();
                    
					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command (first step)"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }

                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

                    _logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
                    _logger->error(errorMessage);

                    bool exceptionInCaseOfError = false;
                    removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);
                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

				ffmpegArgumentList.clear();
				ffmpegArgumentListStream.clear();

				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				ffmpegArgumentList.push_back("-pass");
				ffmpegArgumentList.push_back("2");
				ffmpegArgumentList.push_back("-passlogfile");
				ffmpegArgumentList.push_back(ffmpegPassLogPathFileName);
				addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingEncodedAssetPathName);

                _currentlyAtSecondPass = true;
                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command (second step)"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed (second step)"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    
                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

                    _logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command (second step)"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
                    _logger->error(errorMessage);

                    bool exceptionInCaseOfError = false;
                    removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);
                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

                bool exceptionInCaseOfError = false;
                removeHavingPrefixFileName(_ffmpegTempDir /* stagingEncodedAssetPath */, passlogFileName);
                _logger->info(__FILEREF__ + "Remove"
                    + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
            }
            else
            {
				ffmpegArgumentList.clear();
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceAssetPathName);
				// output options
				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");
				addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
				addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegFileFormatParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingEncodedAssetPathName);

                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "encodeContent: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "encodeContent: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }

                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();
                    _logger->info(__FILEREF__ + "encodeContent: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
                    _logger->error(errorMessage);

                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    bool exceptionInCaseOfError = false;
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

                _logger->info(__FILEREF__ + "Remove"
                    + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                bool exceptionInCaseOfError = false;
                FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
            }

			long long llFileSize = -1;
			// if (FileIO::fileExisting(stagingEncodedAssetPathName))
			{
				bool inCaseOfLinkHasItToBeRead = false;
				llFileSize = FileIO::getFileSizeInBytes (
					stagingEncodedAssetPathName, inCaseOfLinkHasItToBeRead);
			}

            _logger->info(__FILEREF__ + "Encoded file generated"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
				+ ", llFileSize: " + to_string(llFileSize)
				+ ", _twoPasses: " + to_string(_twoPasses)
            );

            if (llFileSize == 0)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
    }
    catch(FFMpegEncodingKilledByUser e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg encode failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", physicalPathKey: " + to_string(physicalPathKey)
            + ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
			|| FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg encode failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", physicalPathKey: " + to_string(physicalPathKey)
            + ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg encode failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", physicalPathKey: " + to_string(physicalPathKey)
            + ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
}

void FFMpeg::overlayImageOnVideo(
        string mmsSourceVideoAssetPathName,
        int64_t videoDurationInMilliSeconds,
        string mmsSourceImageAssetPathName,
        string imagePosition_X_InPixel,
        string imagePosition_Y_InPixel,
        // string encodedFileName,
        string stagingEncodedAssetPathName,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid)
{
	int iReturnedStatus = 0;

	_currentApiName = "overlayImageOnVideo";

	setStatus(
		ingestionJobKey,
		encodingJobKey,
		videoDurationInMilliSeconds,
		mmsSourceVideoAssetPathName,
		stagingEncodedAssetPathName
	);

    try
    {
        // _currentDurationInMilliSeconds      = videoDurationInMilliSeconds;
        // _currentMMSSourceAssetPathName      = mmsSourceVideoAssetPathName;
        // _currentStagingEncodedAssetPathName = stagingEncodedAssetPathName;
        // _currentIngestionJobKey             = ingestionJobKey;
        // _currentEncodingJobKey              = encodingJobKey;
        

        _outputFfmpegPathFileName =
                _ffmpegTempDir + "/"
                + to_string(_currentIngestionJobKey)
                + "_"
                + to_string(_currentEncodingJobKey)
                + ".ffmpegoutput";

        {
            string ffmpegImagePosition_X_InPixel = 
                    regex_replace(imagePosition_X_InPixel, regex("video_width"), "main_w");
            ffmpegImagePosition_X_InPixel = 
                    regex_replace(ffmpegImagePosition_X_InPixel, regex("image_width"), "overlay_w");
            
            string ffmpegImagePosition_Y_InPixel = 
                    regex_replace(imagePosition_Y_InPixel, regex("video_height"), "main_h");
            ffmpegImagePosition_Y_InPixel = 
                    regex_replace(ffmpegImagePosition_Y_InPixel, regex("image_height"), "overlay_h");

			/*
            string ffmpegFilterComplex = string("-filter_complex 'overlay=")
                    + ffmpegImagePosition_X_InPixel + ":"
                    + ffmpegImagePosition_Y_InPixel + "'"
                    ;
			*/
            string ffmpegFilterComplex = string("-filter_complex overlay=")
                    + ffmpegImagePosition_X_InPixel + ":"
                    + ffmpegImagePosition_Y_InPixel
                    ;
		#ifdef __EXECUTE__
            string ffmpegExecuteCommand;
		#else
			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;
		#endif
            {
			#ifdef __EXECUTE__
                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
                string globalOptions = "-y ";
                string inputOptions = "";
                string outputOptions =
                        ffmpegFilterComplex + " "
                        ;
                ffmpegExecuteCommand =
                        _ffmpegPath + "/ffmpeg "
                        + globalOptions
                        + inputOptions
                        + "-i " + mmsSourceVideoAssetPathName + " "
                        + "-i " + mmsSourceImageAssetPathName + " "
                        + outputOptions
                        
                        + stagingEncodedAssetPathName + " "
                        + "> " + _outputFfmpegPathFileName 
                        + " 2>&1"
                ;

                #ifdef __APPLE__
                    ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
                #endif
			#else
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceVideoAssetPathName);
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceImageAssetPathName);
				// output options
				addToArguments(ffmpegFilterComplex, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingEncodedAssetPathName);
			#endif

                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "overlayImageOnVideo: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                    );

                    int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
                    if (executeCommandStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "overlayImageOnVideo: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", executeCommandStatus: " + to_string(executeCommandStatus)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#else
					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "overlayImageOnVideo: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "overlayImageOnVideo: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#endif

                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "overlayImageOnVideo: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#else
                    _logger->info(__FILEREF__ + "overlayImageOnVideo: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#endif
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
				#ifdef __EXECUTE__
                    string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                            + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                            + ", e.what(): " + e.what()
                    ;
				#else
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
				#endif
                    _logger->error(errorMessage);

                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    bool exceptionInCaseOfError = false;
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

                _logger->info(__FILEREF__ + "Remove"
                    + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                bool exceptionInCaseOfError = false;
                FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
            }

            _logger->info(__FILEREF__ + "Overlayed file generated"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            bool inCaseOfLinkHasItToBeRead = false;
            unsigned long ulFileSize = FileIO::getFileSizeInBytes (
                stagingEncodedAssetPathName, inCaseOfLinkHasItToBeRead);

            if (ulFileSize == 0)
            {
			#ifdef __EXECUTE__
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                ;
			#else
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;
			#endif

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }        
    }
    catch(FFMpegEncodingKilledByUser e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg overlay failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", mmsSourceImageAssetPathName: " + mmsSourceImageAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg overlay failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", mmsSourceImageAssetPathName: " + mmsSourceImageAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg overlay failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", mmsSourceImageAssetPathName: " + mmsSourceImageAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
}

void FFMpeg::overlayTextOnVideo(
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
		pid_t* pChildPid)
{
	int iReturnedStatus = 0;

	_currentApiName = "overlayTextOnVideo";

	setStatus(
		ingestionJobKey,
		encodingJobKey,
		videoDurationInMilliSeconds,
		mmsSourceVideoAssetPathName,
		stagingEncodedAssetPathName
	);

    try
    {
        // _currentDurationInMilliSeconds      = videoDurationInMilliSeconds;
        // _currentMMSSourceAssetPathName      = mmsSourceVideoAssetPathName;
        // _currentStagingEncodedAssetPathName = stagingEncodedAssetPathName;
        // _currentIngestionJobKey             = ingestionJobKey;
        // _currentEncodingJobKey              = encodingJobKey;
        

        _outputFfmpegPathFileName =
                _ffmpegTempDir + "/"
                + to_string(_currentIngestionJobKey)
                + "_"
                + to_string(_currentEncodingJobKey)
                + ".ffmpegoutput";

        {
            string ffmpegTextPosition_X_InPixel = 
                    regex_replace(textPosition_X_InPixel, regex("video_width"), "w");
            ffmpegTextPosition_X_InPixel = 
                    regex_replace(ffmpegTextPosition_X_InPixel, regex("text_width"), "text_w");
            ffmpegTextPosition_X_InPixel = 
                    regex_replace(ffmpegTextPosition_X_InPixel, regex("line_width"), "line_w");
            ffmpegTextPosition_X_InPixel = 
                    regex_replace(ffmpegTextPosition_X_InPixel, regex("timestampInSeconds"), "t");
            
            string ffmpegTextPosition_Y_InPixel = 
                    regex_replace(textPosition_Y_InPixel, regex("video_height"), "h");
            ffmpegTextPosition_Y_InPixel = 
                    regex_replace(ffmpegTextPosition_Y_InPixel, regex("text_height"), "text_h");
            ffmpegTextPosition_Y_InPixel = 
                    regex_replace(ffmpegTextPosition_Y_InPixel, regex("line_height"), "line_h");
            ffmpegTextPosition_Y_InPixel = 
                    regex_replace(ffmpegTextPosition_Y_InPixel, regex("timestampInSeconds"), "t");

            string ffmpegDrawTextFilter = string("drawtext=text=") + text;
            if (textPosition_X_InPixel != "")
                ffmpegDrawTextFilter += (":x=" + ffmpegTextPosition_X_InPixel);
            if (textPosition_Y_InPixel != "")
                ffmpegDrawTextFilter += (":y=" + ffmpegTextPosition_Y_InPixel);               
            if (fontType != "")
                ffmpegDrawTextFilter += (":fontfile=" + _ffmpegTtfFontDir + "/" + fontType);
            if (fontSize != -1)
                ffmpegDrawTextFilter += (":fontsize=" + to_string(fontSize));
            if (fontColor != "")
            {
                ffmpegDrawTextFilter += (":fontcolor=" + fontColor);                
                if (textPercentageOpacity != -1)
                {
                    char opacity[64];
                    
                    sprintf(opacity, "%.1f", ((float) textPercentageOpacity) / 100.0);
                    
                    ffmpegDrawTextFilter += ("@" + string(opacity));                
                }
            }
            if (boxEnable)
            {
                ffmpegDrawTextFilter += (":box=1");
                
                if (boxColor != "")
                {
                    ffmpegDrawTextFilter += (":boxcolor=" + boxColor);                
                    if (boxPercentageOpacity != -1)
                    {
                        char opacity[64];

                        sprintf(opacity, "%.1f", ((float) boxPercentageOpacity) / 100.0);

                        ffmpegDrawTextFilter += ("@" + string(opacity));                
                    }
                }
            }

		#ifdef __EXECUTE__
            string ffmpegExecuteCommand;
		#else
			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;
		#endif
            {
			#ifdef __EXECUTE__
                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
                string globalOptions = "-y ";
                string inputOptions = "";
                string outputOptions =
                        ffmpegDrawTextFilter + " "
                        ;
                
                ffmpegExecuteCommand =
                        _ffmpegPath + "/ffmpeg "
                        + globalOptions
                        + inputOptions
                        + "-i " + mmsSourceVideoAssetPathName + " "
                        + outputOptions
                        + stagingEncodedAssetPathName + " "
                        + "> " + _outputFfmpegPathFileName 
                        + " 2>&1"
                ;

                #ifdef __APPLE__
                    ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
                #endif
			#else
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceVideoAssetPathName);
				// output options
				// addToArguments(ffmpegDrawTextFilter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-vf");
				ffmpegArgumentList.push_back(ffmpegDrawTextFilter);
				ffmpegArgumentList.push_back(stagingEncodedAssetPathName);
			#endif

                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "overlayTextOnVideo: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                    );

                    int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
                    if (executeCommandStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "overlayTextOnVideo: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", executeCommandStatus: " + to_string(executeCommandStatus)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#else
					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "overlayTextOnVideo: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "overlayTextOnVideo: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#endif

                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "overlayTextOnVideo: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#else
                    _logger->info(__FILEREF__ + "overlayTextOnVideo: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#endif
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
				#ifdef __EXECUTE__
                    string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                            + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                            + ", e.what(): " + e.what()
                    ;
				#else
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
				#endif
                    _logger->error(errorMessage);

                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    bool exceptionInCaseOfError = false;
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

                _logger->info(__FILEREF__ + "Remove"
                    + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                bool exceptionInCaseOfError = false;
                FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
            }

            _logger->info(__FILEREF__ + "Drawtext file generated"
                + ", encodingJobKey: " + to_string(encodingJobKey)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            bool inCaseOfLinkHasItToBeRead = false;
            unsigned long ulFileSize = FileIO::getFileSizeInBytes (
                stagingEncodedAssetPathName, inCaseOfLinkHasItToBeRead);

            if (ulFileSize == 0)
            {
			#ifdef __EXECUTE__
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                ;
			#else
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;
			#endif

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }        
    }
    catch(FFMpegEncodingKilledByUser e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg drawtext failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg drawtext failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg drawtext failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
}

void FFMpeg::videoSpeed(
        string mmsSourceVideoAssetPathName,
        int64_t videoDurationInMilliSeconds,

        string videoSpeedType,
        int videoSpeedSize,

        // string encodedFileName,
        string stagingEncodedAssetPathName,
        int64_t encodingJobKey,
        int64_t ingestionJobKey,
		pid_t* pChildPid)
{
	int iReturnedStatus = 0;

	_currentApiName = "videoSpeed";

	setStatus(
		ingestionJobKey,
		encodingJobKey,
		videoDurationInMilliSeconds,
		mmsSourceVideoAssetPathName,
		stagingEncodedAssetPathName
	);

    try
    {
        // _currentDurationInMilliSeconds      = videoDurationInMilliSeconds;
        // _currentMMSSourceAssetPathName      = mmsSourceVideoAssetPathName;
        // _currentStagingEncodedAssetPathName = stagingEncodedAssetPathName;
        // _currentIngestionJobKey             = ingestionJobKey;
        // _currentEncodingJobKey              = encodingJobKey;

        _outputFfmpegPathFileName =
                _ffmpegTempDir + "/"
                + to_string(_currentIngestionJobKey)
                + "_"
                + to_string(_currentEncodingJobKey)
                + ".ffmpegoutput";

        {
			string videoPTS;
			string audioTempo;

			if (videoSpeedType == "SlowDown")
			{
				switch(videoSpeedSize)
				{
					case 1:
						videoPTS = "1.1";
						audioTempo = "(1/1.1)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds / 100);

						break;
					case 2:
						videoPTS = "1.2";
						audioTempo = "(1/1.2)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 20 / 100);

						break;
					case 3:
						videoPTS = "1.3";
						audioTempo = "(1/1.3)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 30 / 100);

						break;
					case 4:
						videoPTS = "1.4";
						audioTempo = "(1/1.4)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 40 / 100);

						break;
					case 5:
						videoPTS = "1.5";
						audioTempo = "(1/1.5)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 50 / 100);

						break;
					case 6:
						videoPTS = "1.6";
						audioTempo = "(1/1.6)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 60 / 100);

						break;
					case 7:
						videoPTS = "1.7";
						audioTempo = "(1/1.7)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 70 / 100);

						break;
					case 8:
						videoPTS = "1.8";
						audioTempo = "(1/1.8)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 80 / 100);

						break;
					case 9:
						videoPTS = "1.9";
						audioTempo = "(1/1.9)";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 90 / 100);

						break;
					case 10:
						videoPTS = "2";
						audioTempo = "0.5";
						_currentDurationInMilliSeconds      += (videoDurationInMilliSeconds * 100 / 100);

						break;
					default:
						videoPTS = "1.3";
						audioTempo = "(1/1.3)";

						break;
				}
			}
			else // if (videoSpeedType == "SpeedUp")
			{
				switch(videoSpeedSize)
				{
					case 1:
						videoPTS = "(1/1.1)";
						audioTempo = "1.1";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 10 / 100);

						break;
					case 2:
						videoPTS = "(1/1.2)";
						audioTempo = "1.2";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 20 / 100);

						break;
					case 3:
						videoPTS = "(1/1.3)";
						audioTempo = "1.3";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 30 / 100);

						break;
					case 4:
						videoPTS = "(1/1.4)";
						audioTempo = "1.4";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 40 / 100);

						break;
					case 5:
						videoPTS = "(1/1.5)";
						audioTempo = "1.5";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 50 / 100);

						break;
					case 6:
						videoPTS = "(1/1.6)";
						audioTempo = "1.6";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 60 / 100);

						break;
					case 7:
						videoPTS = "(1/1.7)";
						audioTempo = "1.7";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 70 / 100);

						break;
					case 8:
						videoPTS = "(1/1.8)";
						audioTempo = "1.8";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 80 / 100);

						break;
					case 9:
						videoPTS = "(1/1.9)";
						audioTempo = "1.9";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 90 / 100);

						break;
					case 10:
						videoPTS = "0.5";
						audioTempo = "2";
						_currentDurationInMilliSeconds      -= (videoDurationInMilliSeconds * 100 / 100);

						break;
					default:
						videoPTS = "(1/1.3)";
						audioTempo = "1.3";

						break;
				}
			}

			string complexFilter = "-filter_complex [0:v]setpts=" + videoPTS + "*PTS[v];[0:a]atempo=" + audioTempo + "[a]";
			string videoMap = "-map [v]";
			string audioMap = "-map [a]";
		#ifdef __EXECUTE__
            string ffmpegExecuteCommand;
		#else
			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;
		#endif
            {
			#ifdef __EXECUTE__
			#else
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				ffmpegArgumentList.push_back("-i");
				ffmpegArgumentList.push_back(mmsSourceVideoAssetPathName);
				// output options
				addToArguments(complexFilter, ffmpegArgumentList);
				addToArguments(videoMap, ffmpegArgumentList);
				addToArguments(audioMap, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingEncodedAssetPathName);
			#endif

                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "videoSpeed: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                    );

                    int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
                    if (executeCommandStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "videoSpeed: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", executeCommandStatus: " + to_string(executeCommandStatus)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#else
					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "videoSpeed: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "videoSpeed: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#endif

                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "videoSpeed: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#else
                    _logger->info(__FILEREF__ + "videoSpeed: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#endif
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
				#ifdef __EXECUTE__
                    string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                            + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                            + ", e.what(): " + e.what()
                    ;
				#else
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
				#endif
                    _logger->error(errorMessage);

                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    bool exceptionInCaseOfError = false;
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

                _logger->info(__FILEREF__ + "Remove"
                    + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                bool exceptionInCaseOfError = false;
                FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
            }

            _logger->info(__FILEREF__ + "VideoSpeed file generated"
                + ", encodingJobKey: " + to_string(encodingJobKey)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            bool inCaseOfLinkHasItToBeRead = false;
            unsigned long ulFileSize = FileIO::getFileSizeInBytes (
                stagingEncodedAssetPathName, inCaseOfLinkHasItToBeRead);

            if (ulFileSize == 0)
            {
			#ifdef __EXECUTE__
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                ;
			#else
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, encoded file size is 0"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;
			#endif

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }        
    }
    catch(FFMpegEncodingKilledByUser e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg VideoSpeed failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg VideoSpeed failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg VideoSpeed failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsSourceVideoAssetPathName: " + mmsSourceVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                    + ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
}

void FFMpeg::pictureInPicture(
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
		pid_t* pChildPid)
{
	int iReturnedStatus = 0;

	_currentApiName = "pictureInPicture";

	setStatus(
		ingestionJobKey,
		encodingJobKey,
		mainVideoDurationInMilliSeconds,
		mmsMainVideoAssetPathName,
		stagingEncodedAssetPathName
	);

    try
    {
		if (mainVideoDurationInMilliSeconds < overlayVideoDurationInMilliSeconds)
		{
			string errorMessage = __FILEREF__ + "pictureInPicture: overlay video duration cannot be bigger than main video diration"
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", mainVideoDurationInMilliSeconds: " + to_string(mainVideoDurationInMilliSeconds)
				+ ", overlayVideoDurationInMilliSeconds: " + to_string(overlayVideoDurationInMilliSeconds)
			;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}

        // _currentDurationInMilliSeconds      = mainVideoDurationInMilliSeconds;
        // _currentMMSSourceAssetPathName      = mmsMainVideoAssetPathName;
        // _currentStagingEncodedAssetPathName = stagingEncodedAssetPathName;
        // _currentIngestionJobKey             = ingestionJobKey;
        // _currentEncodingJobKey              = encodingJobKey;
        

        _outputFfmpegPathFileName =
                _ffmpegTempDir + "/"
                + to_string(_currentIngestionJobKey)
                + "_"
                + to_string(_currentEncodingJobKey)
                + ".ffmpegoutput";

        {
            string ffmpegOverlayPosition_X_InPixel = 
                    regex_replace(overlayPosition_X_InPixel, regex("mainVideo_width"), "main_w");
            ffmpegOverlayPosition_X_InPixel = 
                    regex_replace(ffmpegOverlayPosition_X_InPixel, regex("overlayVideo_width"), "overlay_w");
            
            string ffmpegOverlayPosition_Y_InPixel = 
                    regex_replace(overlayPosition_Y_InPixel, regex("mainVideo_height"), "main_h");
            ffmpegOverlayPosition_Y_InPixel = 
                    regex_replace(ffmpegOverlayPosition_Y_InPixel, regex("overlayVideo_height"), "overlay_h");

			string ffmpegOverlay_Width_InPixel = 
				regex_replace(overlay_Width_InPixel, regex("overlayVideo_width"), "iw");

			string ffmpegOverlay_Height_InPixel = 
				regex_replace(overlay_Height_InPixel, regex("overlayVideo_height"), "ih");

			/*
            string ffmpegFilterComplex = string("-filter_complex 'overlay=")
                    + ffmpegImagePosition_X_InPixel + ":"
                    + ffmpegImagePosition_Y_InPixel + "'"
                    ;
			*/
            string ffmpegFilterComplex = string("-filter_complex ");
			if (soundOfMain)
				ffmpegFilterComplex += "[1]scale=";
			else
				ffmpegFilterComplex += "[0]scale=";
			ffmpegFilterComplex +=
				(ffmpegOverlay_Width_InPixel + ":" + ffmpegOverlay_Height_InPixel)
			;
			ffmpegFilterComplex += "[pip];";

			if (soundOfMain)
			{
				ffmpegFilterComplex += "[0][pip]overlay=";
			}
			else
			{
				ffmpegFilterComplex += "[pip][0]overlay=";
			}
			ffmpegFilterComplex +=
				(ffmpegOverlayPosition_X_InPixel + ":" + ffmpegOverlayPosition_Y_InPixel)
			;
		#ifdef __EXECUTE__
            string ffmpegExecuteCommand;
		#else
			vector<string> ffmpegArgumentList;
			ostringstream ffmpegArgumentListStream;
		#endif
            {
			#ifdef __EXECUTE__
                // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
                string globalOptions = "-y ";
                string inputOptions = "";
                string outputOptions =
                        ffmpegFilterComplex + " "
                        ;
                ffmpegExecuteCommand =
                        _ffmpegPath + "/ffmpeg "
                        + globalOptions
                        + inputOptions;
				if (soundOfMain)
					ffmpegExecuteCommand +=
                        ("-i " + mmsMainVideoAssetPathName + " " + "-i " + mmsOverlayVideoAssetPathName + " ");
				else
					ffmpegExecuteCommand +=
                        ("-i " + mmsOverlayVideoAssetPathName + " " + "-i " + mmsMainVideoAssetPathName + " ");
				ffmpegExecuteCommand +=
					(outputOptions

					+ stagingEncodedAssetPathName + " "
					+ "> " + _outputFfmpegPathFileName 
					+ " 2>&1")
                ;

                #ifdef __APPLE__
                    ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
                #endif
			#else
				ffmpegArgumentList.push_back("ffmpeg");
				// global options
				ffmpegArgumentList.push_back("-y");
				// input options
				if (soundOfMain)
				{
					ffmpegArgumentList.push_back("-i");
					ffmpegArgumentList.push_back(mmsMainVideoAssetPathName);
					ffmpegArgumentList.push_back("-i");
					ffmpegArgumentList.push_back(mmsOverlayVideoAssetPathName);
				}
				else
				{
					ffmpegArgumentList.push_back("-i");
					ffmpegArgumentList.push_back(mmsOverlayVideoAssetPathName);
					ffmpegArgumentList.push_back("-i");
					ffmpegArgumentList.push_back(mmsMainVideoAssetPathName);
				}
				// output options
				addToArguments(ffmpegFilterComplex, ffmpegArgumentList);
				ffmpegArgumentList.push_back(stagingEncodedAssetPathName);
			#endif

                try
                {
                    chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "pictureInPicture: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                    );

                    int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
                    if (executeCommandStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "pictureInPicture: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", executeCommandStatus: " + to_string(executeCommandStatus)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#else
					if (!ffmpegArgumentList.empty())
						copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
							ostream_iterator<string>(ffmpegArgumentListStream, " "));

                    _logger->info(__FILEREF__ + "pictureInPicture: Executing ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                    );

					bool redirectionStdOutput = true;
					bool redirectionStdError = true;

					ProcessUtility::forkAndExec (
						_ffmpegPath + "/ffmpeg",
						ffmpegArgumentList,
						_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
						pChildPid, &iReturnedStatus);
					if (iReturnedStatus != 0)
                    {
                        string errorMessage = __FILEREF__ + "pictureInPicture: ffmpeg command failed"
                            + ", encodingJobKey: " + to_string(encodingJobKey)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", iReturnedStatus: " + to_string(iReturnedStatus)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        ;            
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
				#endif

                    chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

				#ifdef __EXECUTE__
                    _logger->info(__FILEREF__ + "pictureInPicture: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#else
                    _logger->info(__FILEREF__ + "pictureInPicture: Executed ffmpeg command"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                        + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
                    );
				#endif
                }
                catch(runtime_error e)
                {
                    string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                            _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
				#ifdef __EXECUTE__
                    string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                            + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                            + ", e.what(): " + e.what()
                    ;
				#else
					string errorMessage;
					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
					else
						errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
							+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
							+ ", encodingJobKey: " + to_string(encodingJobKey)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
							+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
							+ ", e.what(): " + e.what()
						;
				#endif
                    _logger->error(errorMessage);

                    _logger->info(__FILEREF__ + "Remove"
                        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                    bool exceptionInCaseOfError = false;
                    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

					if (iReturnedStatus == 9)	// 9 means: SIGKILL
						throw FFMpegEncodingKilledByUser();
					else
						throw e;
                }

                _logger->info(__FILEREF__ + "Remove"
                    + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
                bool exceptionInCaseOfError = false;
                FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
            }

            _logger->info(__FILEREF__ + "pictureInPicture file generated"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            bool inCaseOfLinkHasItToBeRead = false;
            unsigned long ulFileSize = FileIO::getFileSizeInBytes (
                stagingEncodedAssetPathName, inCaseOfLinkHasItToBeRead);

            if (ulFileSize == 0)
            {
			#ifdef __EXECUTE__
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, pictureInPicture encoded file size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                ;
			#else
                string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, pictureInPicture encoded file size is 0"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
                ;
			#endif

                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }        
    }
    catch(FFMpegEncodingKilledByUser e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg pictureInPicture failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsMainVideoAssetPathName: " + mmsMainVideoAssetPathName
            + ", mmsOverlayVideoAssetPathName: " + mmsOverlayVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg pictureInPicture failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsMainVideoAssetPathName: " + mmsMainVideoAssetPathName
            + ", mmsOverlayVideoAssetPathName: " + mmsOverlayVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: ffmpeg pictureInPicture failed"
            + ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsMainVideoAssetPathName: " + mmsMainVideoAssetPathName
            + ", mmsOverlayVideoAssetPathName: " + mmsOverlayVideoAssetPathName
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

        if (FileIO::fileExisting(stagingEncodedAssetPathName)
                || FileIO::directoryExisting(stagingEncodedAssetPathName))
        {
            FileIO::DirectoryEntryType_t detSourceFileType = FileIO::getDirectoryEntryType(stagingEncodedAssetPathName);

            _logger->info(__FILEREF__ + "Remove"
                        + ", encodingJobKey: " + to_string(encodingJobKey)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );

            // file in case of .3gp content OR directory in case of IPhone content
            if (detSourceFileType == FileIO::TOOLS_FILEIO_DIRECTORY)
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                Boolean_t bRemoveRecursively = true;
                FileIO::removeDirectory(stagingEncodedAssetPathName, bRemoveRecursively);
            }
            else if (detSourceFileType == FileIO::TOOLS_FILEIO_REGULARFILE) 
            {
                _logger->info(__FILEREF__ + "Remove"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName);
                FileIO::remove(stagingEncodedAssetPathName);
            }
        }

        throw e;
    }
}

/*
tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long>
	FFMpeg::getMediaInfo(string mmsAssetPathName)
{
	_currentApiName = "getMediaInfo";

	_logger->info(__FILEREF__ + "getMediaInfo"
			", mmsAssetPathName: " + mmsAssetPathName
			);

    size_t fileNameIndex = mmsAssetPathName.find_last_of("/");
    if (fileNameIndex == string::npos)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: No fileName find in the asset path name"
                + ", mmsAssetPathName: " + mmsAssetPathName;
        _logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
    
    string sourceFileName = mmsAssetPathName.substr(fileNameIndex + 1);

    string      detailsPathFileName =
            _ffmpegTempDir + "/" + sourceFileName + ".json";
    
    // ffprobe:
    //   "-v quiet": Don't output anything else but the desired raw data value
    //   "-print_format": Use a certain format to print out the data
    //   "compact=": Use a compact output format
    //   "print_section=0": Do not print the section name
    //   ":nokey=1": do not print the key of the key:value pair
    //   ":escape=csv": escape the value
    //   "-show_entries format=duration": Get entries of a field named duration inside a section named format
    string ffprobeExecuteCommand = 
            _ffmpegPath + "/ffprobe "
            // + "-v quiet -print_format compact=print_section=0:nokey=1:escape=csv -show_entries format=duration "
            + "-v quiet -print_format json -show_streams -show_format "
            + mmsAssetPathName + " "
            + "> " + detailsPathFileName 
            + " 2>&1"
            ;

    #ifdef __APPLE__
        ffprobeExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif

    try
    {
        _logger->info(__FILEREF__ + "getMediaInfo: Executing ffprobe command"
            + ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
        );

        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

		// The check/retries below was done to manage the scenario where the file was created
		// by another MMSEngine and it is not found just because of nfs delay.
		// Really, looking the log, we saw the file is just missing and it is not an nfs delay
		int attemptIndex = 0;
		bool executeDone = false;
		while (!executeDone)
		{
			int executeCommandStatus = ProcessUtility::execute(ffprobeExecuteCommand);
			if (executeCommandStatus != 0)
			{
				if (FileIO::fileExisting(mmsAssetPathName))
				{
					string errorMessage = __FILEREF__ +
						"getMediaInfo: ffmpeg: ffprobe command failed"
						+ ", executeCommandStatus: " + to_string(executeCommandStatus)
						+ ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
					;

					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}
				else
				{
					if (attemptIndex < _waitingNFSSync_attemptNumber)
					{
						attemptIndex++;

						string errorMessage = __FILEREF__
							+ "getMediaInfo: The file does not exist, waiting because of nfs delay"
							+ ", executeCommandStatus: " + to_string(executeCommandStatus)
							+ ", attemptIndex: " + to_string(attemptIndex)
							+ ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
						;

						_logger->warn(errorMessage);

						this_thread::sleep_for(
								chrono::seconds(_waitingNFSSync_sleepTimeInSeconds));
					}
					else
					{
						string errorMessage = __FILEREF__
							+ "getMediaInfo: ffmpeg: ffprobe command failed because the file does not exist"
							+ ", executeCommandStatus: " + to_string(executeCommandStatus)
							+ ", attemptIndex: " + to_string(attemptIndex)
							+ ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
						;
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}
				}
			}
			else
			{
				executeDone = true;
			}
        }
        
        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "getMediaInfo: Executed ffmpeg command"
            + ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
            + ", statistics duration (secs): "
				+ to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count())
        );
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                detailsPathFileName, _charsToBeReadFromFfmpegErrorOutput);
        string errorMessage = __FILEREF__ + "ffmpeg: ffprobe command failed"
                + ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);

        throw e;
    }

    int64_t durationInMilliSeconds = -1;
    long bitRate = -1;
    string videoCodecName;
    string videoProfile;
    int videoWidth = -1;
    int videoHeight = -1;
    string videoAvgFrameRate;
    long videoBitRate = -1;
    string audioCodecName;
    long audioSampleRate = -1;
    int audioChannels = -1;
    long audioBitRate = -1;
    try
    {
        // json output will be like:
        //    {
        //        "streams": [
        //            {
        //                "index": 0,
        //                "codec_name": "mpeg4",
        //                "codec_long_name": "MPEG-4 part 2",
        //                "profile": "Advanced Simple Profile",
        //                "codec_type": "video",
        //                "codec_time_base": "1/25",
        //                "codec_tag_string": "XVID",
        //                "codec_tag": "0x44495658",
        //                "width": 712,
        //                "height": 288,
        //                "coded_width": 712,
        //                "coded_height": 288,
        //                "has_b_frames": 1,
        //                "sample_aspect_ratio": "1:1",
        //                "display_aspect_ratio": "89:36",
        //                "pix_fmt": "yuv420p",
        //                "level": 5,
        //                "chroma_location": "left",
        //                "refs": 1,
        //                "quarter_sample": "false",
        //                "divx_packed": "false",
        //                "r_frame_rate": "25/1",
        //                "avg_frame_rate": "25/1",
        //                "time_base": "1/25",
        //                "start_pts": 0,
        //                "start_time": "0.000000",
        //                "duration_ts": 142100,
        //                "duration": "5684.000000",
        //                "bit_rate": "873606",
        //                "nb_frames": "142100",
        //                "disposition": {
        //                    "default": 0,
        //                    "dub": 0,
        //                    "original": 0,
        //                    "comment": 0,
        //                    "lyrics": 0,
        //                    "karaoke": 0,
        //                    "forced": 0,
        //                    "hearing_impaired": 0,
        //                    "visual_impaired": 0,
        //                    "clean_effects": 0,
        //                    "attached_pic": 0,
        //                    "timed_thumbnails": 0
        //                }
        //            },
        //            {
        //                "index": 1,
        //                "codec_name": "mp3",
        //                "codec_long_name": "MP3 (MPEG audio layer 3)",
        //                "codec_type": "audio",
        //                "codec_time_base": "1/48000",
        //                "codec_tag_string": "U[0][0][0]",
        //                "codec_tag": "0x0055",
        //                "sample_fmt": "s16p",
        //                "sample_rate": "48000",
        //                "channels": 2,
        //                "channel_layout": "stereo",
        //                "bits_per_sample": 0,
        //                "r_frame_rate": "0/0",
        //                "avg_frame_rate": "0/0",
        //                "time_base": "3/125",
        //                "start_pts": 0,
        //                "start_time": "0.000000",
        //                "duration_ts": 236822,
        //                "duration": "5683.728000",
        //                "bit_rate": "163312",
        //                "nb_frames": "236822",
        //                "disposition": {
        //                    "default": 0,
        //                    "dub": 0,
        //                    "original": 0,
        //                    "comment": 0,
        //                    "lyrics": 0,
        //                    "karaoke": 0,
        //                    "forced": 0,
        //                    "hearing_impaired": 0,
        //                    "visual_impaired": 0,
        //                    "clean_effects": 0,
        //                    "attached_pic": 0,
        //                    "timed_thumbnails": 0
        //                }
        //            }
        //        ],
        //        "format": {
        //            "filename": "/Users/multi/VitadaCamper.avi",
        //            "nb_streams": 2,
        //            "nb_programs": 0,
        //            "format_name": "avi",
        //            "format_long_name": "AVI (Audio Video Interleaved)",
        //            "start_time": "0.000000",
        //            "duration": "5684.000000",
        //            "size": "745871360",
        //            "bit_rate": "1049783",
        //            "probe_score": 100,
        //            "tags": {
        //                "encoder": "VirtualDubMod 1.5.10.2 (build 2540/release)"
        //            }
        //        }
        //    }

        ifstream detailsFile(detailsPathFileName);
        stringstream buffer;
        buffer << detailsFile.rdbuf();
        
        _logger->info(__FILEREF__ + "Details found"
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", details: " + buffer.str()
        );

        string mediaDetails = buffer.str();
        // LF and CR create problems to the json parser...
        while (mediaDetails.back() == 10 || mediaDetails.back() == 13)
            mediaDetails.pop_back();

        Json::Value detailsRoot;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(mediaDetails.c_str(),
                    mediaDetails.c_str() + mediaDetails.size(), 
                    &detailsRoot, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: failed to parse the media details"
                        + ", mmsAssetPathName: " + mmsAssetPathName
                        + ", errors: " + errors
                        + ", mediaDetails: " + mediaDetails
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("ffmpeg: media json is not well format")
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", mediaDetails: " + mediaDetails
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
                
        string field = "streams";
        if (!isMetadataPresent(detailsRoot, field))
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        Json::Value streamsRoot = detailsRoot[field];
        bool videoFound = false;
        bool audioFound = false;
        for(int streamIndex = 0; streamIndex < streamsRoot.size(); streamIndex++) 
        {
            Json::Value streamRoot = streamsRoot[streamIndex];
            
            field = "codec_type";
            if (!isMetadataPresent(streamRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", mmsAssetPathName: " + mmsAssetPathName
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string codecType = streamRoot.get(field, "XXX").asString();
            
            if (codecType == "video" && !videoFound)
            {
                videoFound = true;

                field = "codec_name";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoCodecName = streamRoot.get(field, "XXX").asString();

                field = "profile";
                if (isMetadataPresent(streamRoot, field))
                    videoProfile = streamRoot.get(field, "XXX").asString();
                else
                {
                    // if (videoCodecName != "mjpeg")
                    // {
                    //     string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    //             + ", mmsAssetPathName: " + mmsAssetPathName
                    //             + ", Field: " + field;
                    //     _logger->error(errorMessage);

                    //     throw runtime_error(errorMessage);
                    // }
                }

                field = "width";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoWidth = asInt(streamRoot, field, 0);

                field = "height";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoHeight = asInt(streamRoot, field, 0);
                
                field = "avg_frame_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoAvgFrameRate = streamRoot.get(field, "XXX").asString();

                field = "bit_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    if (videoCodecName != "mjpeg")
                    {
                        // I didn't find bit_rate also in a ts file, let's set it as a warning
                        
                        string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                                + ", mmsAssetPathName: " + mmsAssetPathName
                                + ", Field: " + field;
                        _logger->warn(errorMessage);

                        // throw runtime_error(errorMessage);
                    }
                }
                else
                    videoBitRate = stol(streamRoot.get(field, "XXX").asString());
            }
            else if (codecType == "audio" && !audioFound)
            {
                audioFound = true;

                field = "codec_name";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                audioCodecName = streamRoot.get(field, "XXX").asString();

                field = "sample_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                audioSampleRate = stol(streamRoot.get(field, "XXX").asString());

                field = "channels";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                audioChannels = asInt(streamRoot, field, 0);
                
                field = "bit_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    // I didn't find bit_rate in a webm file, let's set it as a warning

                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->warn(errorMessage);

                    // throw runtime_error(errorMessage);
                }
				else
					audioBitRate = stol(streamRoot.get(field, "XXX").asString());
            }
        }

        field = "format";
        if (!isMetadataPresent(detailsRoot, field))
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        Json::Value formatRoot = detailsRoot[field];

        field = "duration";
        if (!isMetadataPresent(formatRoot, field))
        {
			// I didn't find it in a .avi file generated using OpenCV::VideoWriter
			// let's log it as a warning
            if (videoCodecName != "" && videoCodecName != "mjpeg")
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
                _logger->warn(errorMessage);

                // throw runtime_error(errorMessage);
            }            
        }
        else
        {
            string duration = formatRoot.get(field, "XXX").asString();

			// 2020-01-13: atoll remove the milliseconds and this is wrong
            // durationInMilliSeconds = atoll(duration.c_str()) * 1000;

            double dDurationInMilliSeconds = stod(duration);
            durationInMilliSeconds = dDurationInMilliSeconds * 1000;
        }

        field = "bit_rate";
        if (!isMetadataPresent(formatRoot, field))
        {
            if (videoCodecName != "" && videoCodecName != "mjpeg")
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }            
        }
        else
        {
            string bit_rate = formatRoot.get(field, "XXX").asString();
            bitRate = atoll(bit_rate.c_str());
        }

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);
    }
    catch(runtime_error e)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: error processing ffprobe output"
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);

        throw e;
    }
    catch(exception e)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: error processing ffprobe output"
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);

        throw e;
    }

    // if (durationInMilliSeconds == -1)
    // {
    //     string errorMessage = __FILEREF__ + "ffmpeg: durationInMilliSeconds was not able to be retrieved from media"
    //             + ", mmsAssetPathName: " + mmsAssetPathName
    //             + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds);
    //     _logger->error(errorMessage);

    //     throw runtime_error(errorMessage);
    // }
    // else if (width == -1 || height == -1)
    // {
    //     string errorMessage = __FILEREF__ + "ffmpeg: width/height were not able to be retrieved from media"
    //             + ", mmsAssetPathName: " + mmsAssetPathName
    //             + ", width: " + to_string(width)
    //             + ", height: " + to_string(height)
    //             ;
    //     _logger->error(errorMessage);

    //     throw runtime_error(errorMessage);
    // }
    
    _logger->info(__FILEREF__ + "FFMpeg::getMediaInfo"
        + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
        + ", bitRate: " + to_string(bitRate)
        + ", videoCodecName: " + videoCodecName
        + ", videoProfile: " + videoProfile
        + ", videoWidth: " + to_string(videoWidth)
        + ", videoHeight: " + to_string(videoHeight)
        + ", videoAvgFrameRate: " + videoAvgFrameRate
        + ", videoBitRate: " + to_string(videoBitRate)
        + ", audioCodecName: " + audioCodecName
        + ", audioSampleRate: " + to_string(audioSampleRate)
        + ", audioChannels: " + to_string(audioChannels)
        + ", audioBitRate: " + to_string(audioBitRate)
    );
    
    return make_tuple(durationInMilliSeconds, bitRate, 
            videoCodecName, videoProfile, videoWidth, videoHeight, videoAvgFrameRate, videoBitRate,
            audioCodecName, audioSampleRate, audioChannels, audioBitRate
            );
}
*/

pair<int64_t, long> FFMpeg::getMediaInfo(string mmsAssetPathName,
	vector<tuple<int, int64_t, string, string, int, int, string, long>>& videoTracks,
	vector<tuple<int, int64_t, string, long, int, long, string>>& audioTracks)
{
	_currentApiName = "getMediaInfo";

	_logger->info(__FILEREF__ + "getMediaInfo"
			", mmsAssetPathName: " + mmsAssetPathName
			);

    size_t fileNameIndex = mmsAssetPathName.find_last_of("/");
    if (fileNameIndex == string::npos)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: No fileName find in the asset path name"
                + ", mmsAssetPathName: " + mmsAssetPathName;
        _logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
    
    string sourceFileName = mmsAssetPathName.substr(fileNameIndex + 1);

    string      detailsPathFileName =
            _ffmpegTempDir + "/" + sourceFileName + ".json";
    
    /*
     * ffprobe:
        "-v quiet": Don't output anything else but the desired raw data value
        "-print_format": Use a certain format to print out the data
        "compact=": Use a compact output format
        "print_section=0": Do not print the section name
        ":nokey=1": do not print the key of the key:value pair
        ":escape=csv": escape the value
        "-show_entries format=duration": Get entries of a field named duration inside a section named format
    */
    string ffprobeExecuteCommand = 
            _ffmpegPath + "/ffprobe "
            // + "-v quiet -print_format compact=print_section=0:nokey=1:escape=csv -show_entries format=duration "
            + "-v quiet -print_format json -show_streams -show_format "
            + mmsAssetPathName + " "
            + "> " + detailsPathFileName 
            + " 2>&1"
            ;

    #ifdef __APPLE__
        ffprobeExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif

    try
    {
        _logger->info(__FILEREF__ + "getMediaInfo: Executing ffprobe command"
            + ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
        );

        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

		// The check/retries below was done to manage the scenario where the file was created
		// by another MMSEngine and it is not found just because of nfs delay.
		// Really, looking the log, we saw the file is just missing and it is not an nfs delay
		int attemptIndex = 0;
		bool executeDone = false;
		while (!executeDone)
		{
			int executeCommandStatus = ProcessUtility::execute(ffprobeExecuteCommand);
			if (executeCommandStatus != 0)
			{
				if (FileIO::fileExisting(mmsAssetPathName))
				{
					string errorMessage = __FILEREF__ +
						"getMediaInfo: ffmpeg: ffprobe command failed"
						+ ", executeCommandStatus: " + to_string(executeCommandStatus)
						+ ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
					;
					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}
				else
				{
					if (attemptIndex < _waitingNFSSync_attemptNumber)
					{
						attemptIndex++;

						string errorMessage = __FILEREF__
							+ "getMediaInfo: The file does not exist, waiting because of nfs delay"
							+ ", executeCommandStatus: " + to_string(executeCommandStatus)
							+ ", attemptIndex: " + to_string(attemptIndex)
							+ ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
						;

						_logger->warn(errorMessage);

						this_thread::sleep_for(
								chrono::seconds(_waitingNFSSync_sleepTimeInSeconds));
					}
					else
					{
						string errorMessage = __FILEREF__
							+ "getMediaInfo: ffmpeg: ffprobe command failed because the file does not exist"
							+ ", executeCommandStatus: " + to_string(executeCommandStatus)
							+ ", attemptIndex: " + to_string(attemptIndex)
							+ ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
						;
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}
				}
			}
			else
			{
				executeDone = true;
			}
        }
        
        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "getMediaInfo: Executed ffmpeg command"
            + ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
            + ", @FFMPEG statistics@ - duration (secs): @"
				+ to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                detailsPathFileName, _charsToBeReadFromFfmpegErrorOutput);
        string errorMessage = __FILEREF__ + "ffmpeg: ffprobe command failed"
                + ", ffprobeExecuteCommand: " + ffprobeExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);

        throw e;
    }

	int64_t durationInMilliSeconds = -1;
	long bitRate = -1;

    try
    {
        // json output will be like:
        /*
            {
                "streams": [
                    {
                        "index": 0,
                        "codec_name": "mpeg4",
                        "codec_long_name": "MPEG-4 part 2",
                        "profile": "Advanced Simple Profile",
                        "codec_type": "video",
                        "codec_time_base": "1/25",
                        "codec_tag_string": "XVID",
                        "codec_tag": "0x44495658",
                        "width": 712,
                        "height": 288,
                        "coded_width": 712,
                        "coded_height": 288,
                        "has_b_frames": 1,
                        "sample_aspect_ratio": "1:1",
                        "display_aspect_ratio": "89:36",
                        "pix_fmt": "yuv420p",
                        "level": 5,
                        "chroma_location": "left",
                        "refs": 1,
                        "quarter_sample": "false",
                        "divx_packed": "false",
                        "r_frame_rate": "25/1",
                        "avg_frame_rate": "25/1",
                        "time_base": "1/25",
                        "start_pts": 0,
                        "start_time": "0.000000",
                        "duration_ts": 142100,
                        "duration": "5684.000000",
                        "bit_rate": "873606",
                        "nb_frames": "142100",
                        "disposition": {
                            "default": 0,
                            "dub": 0,
                            "original": 0,
                            "comment": 0,
                            "lyrics": 0,
                            "karaoke": 0,
                            "forced": 0,
                            "hearing_impaired": 0,
                            "visual_impaired": 0,
                            "clean_effects": 0,
                            "attached_pic": 0,
                            "timed_thumbnails": 0
                        }
                    },
                    {
                        "index": 1,
                        "codec_name": "mp3",
                        "codec_long_name": "MP3 (MPEG audio layer 3)",
                        "codec_type": "audio",
                        "codec_time_base": "1/48000",
                        "codec_tag_string": "U[0][0][0]",
                        "codec_tag": "0x0055",
                        "sample_fmt": "s16p",
                        "sample_rate": "48000",
                        "channels": 2,
                        "channel_layout": "stereo",
                        "bits_per_sample": 0,
                        "r_frame_rate": "0/0",
                        "avg_frame_rate": "0/0",
                        "time_base": "3/125",
                        "start_pts": 0,
                        "start_time": "0.000000",
                        "duration_ts": 236822,
                        "duration": "5683.728000",
                        "bit_rate": "163312",
                        "nb_frames": "236822",
                        "disposition": {
                            "default": 0,
                            "dub": 0,
                            "original": 0,
                            "comment": 0,
                            "lyrics": 0,
                            "karaoke": 0,
                            "forced": 0,
                            "hearing_impaired": 0,
                            "visual_impaired": 0,
                            "clean_effects": 0,
                            "attached_pic": 0,
                            "timed_thumbnails": 0
                        }
                    }
                ],
                "format": {
                    "filename": "/Users/multi/VitadaCamper.avi",
                    "nb_streams": 2,
                    "nb_programs": 0,
                    "format_name": "avi",
                    "format_long_name": "AVI (Audio Video Interleaved)",
                    "start_time": "0.000000",
                    "duration": "5684.000000",
                    "size": "745871360",
                    "bit_rate": "1049783",
                    "probe_score": 100,
                    "tags": {
                        "encoder": "VirtualDubMod 1.5.10.2 (build 2540/release)"
                    }
                }
            }
         */

        ifstream detailsFile(detailsPathFileName);
        stringstream buffer;
        buffer << detailsFile.rdbuf();
        
        _logger->info(__FILEREF__ + "Details found"
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", details: " + buffer.str()
        );

        string mediaDetails = buffer.str();
        // LF and CR create problems to the json parser...
        while (mediaDetails.back() == 10 || mediaDetails.back() == 13)
            mediaDetails.pop_back();

        Json::Value detailsRoot;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(mediaDetails.c_str(),
                    mediaDetails.c_str() + mediaDetails.size(), 
                    &detailsRoot, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: failed to parse the media details"
                        + ", mmsAssetPathName: " + mmsAssetPathName
                        + ", errors: " + errors
                        + ", mediaDetails: " + mediaDetails
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("ffmpeg: media json is not well format")
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", mediaDetails: " + mediaDetails
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
                
        string field = "streams";
        if (!isMetadataPresent(detailsRoot, field))
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        Json::Value streamsRoot = detailsRoot[field];
        bool videoFound = false;
        bool audioFound = false;
		string firstVideoCodecName;
        for(int streamIndex = 0; streamIndex < streamsRoot.size(); streamIndex++) 
        {
            Json::Value streamRoot = streamsRoot[streamIndex];
            
            field = "codec_type";
            if (!isMetadataPresent(streamRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", mmsAssetPathName: " + mmsAssetPathName
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string codecType = streamRoot.get(field, "XXX").asString();
            
            if (codecType == "video")
            {
                videoFound = true;

				int trackIndex;
				int64_t videoDurationInMilliSeconds = -1;
				string videoCodecName;
				string videoProfile;
				int videoWidth = -1;
				int videoHeight = -1;
				string videoAvgFrameRate;
				long videoBitRate = -1;

                field = "index";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                trackIndex = asInt(streamRoot, field, 0);

                field = "codec_name";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoCodecName = streamRoot.get(field, "XXX").asString();

				if (firstVideoCodecName == "")
					firstVideoCodecName = videoCodecName;

                field = "profile";
                if (isMetadataPresent(streamRoot, field))
                    videoProfile = streamRoot.get(field, "XXX").asString();
                else
                {
                    /*
                    if (videoCodecName != "mjpeg")
                    {
                        string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                                + ", mmsAssetPathName: " + mmsAssetPathName
                                + ", Field: " + field;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                     */
                }

                field = "width";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoWidth = asInt(streamRoot, field, 0);

                field = "height";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoHeight = asInt(streamRoot, field, 0);
                
                field = "avg_frame_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                videoAvgFrameRate = streamRoot.get(field, "XXX").asString();

                field = "bit_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    if (videoCodecName != "mjpeg")
                    {
                        // I didn't find bit_rate also in a ts file, let's set it as a warning
                        
                        string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                                + ", mmsAssetPathName: " + mmsAssetPathName
                                + ", Field: " + field;
                        _logger->warn(errorMessage);

                        // throw runtime_error(errorMessage);
                    }
                }
                else
                    videoBitRate = stol(streamRoot.get(field, "").asString());

				field = "duration";
				if (!isMetadataPresent(streamRoot, field))
				{
					// I didn't find it in a .avi file generated using OpenCV::VideoWriter
					// let's log it as a warning
					if (videoCodecName != "" && videoCodecName != "mjpeg")
					{
						string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
							+ ", mmsAssetPathName: " + mmsAssetPathName
							+ ", Field: " + field;
						_logger->warn(errorMessage);

						// throw runtime_error(errorMessage);
					}            
				}
				else
				{
					string duration = streamRoot.get(field, "0").asString();

					// 2020-01-13: atoll remove the milliseconds and this is wrong
					// durationInMilliSeconds = atoll(duration.c_str()) * 1000;

					double dDurationInMilliSeconds = stod(duration);
					videoDurationInMilliSeconds = dDurationInMilliSeconds * 1000;
				}

				videoTracks.push_back(make_tuple(trackIndex, videoDurationInMilliSeconds,
					videoCodecName, videoProfile, videoWidth, videoHeight,
					videoAvgFrameRate, videoBitRate));
            }
            else if (codecType == "audio")
            {
                audioFound = true;

				int trackIndex;
				int64_t audioDurationInMilliSeconds = -1;
				string audioCodecName;
				long audioSampleRate = -1;
				int audioChannels = -1;
				long audioBitRate = -1;

                field = "index";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                trackIndex = asInt(streamRoot, field, 0);

                field = "codec_name";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                audioCodecName = streamRoot.get(field, "XXX").asString();

                field = "sample_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                audioSampleRate = stol(streamRoot.get(field, "XXX").asString());

                field = "channels";
                if (!isMetadataPresent(streamRoot, field))
                {
                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                audioChannels = asInt(streamRoot, field, 0);
                
                field = "bit_rate";
                if (!isMetadataPresent(streamRoot, field))
                {
                    // I didn't find bit_rate in a webm file, let's set it as a warning

                    string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                            + ", mmsAssetPathName: " + mmsAssetPathName
                            + ", Field: " + field;
                    _logger->warn(errorMessage);

                    // throw runtime_error(errorMessage);
                }
				else
					audioBitRate = stol(streamRoot.get(field, "XXX").asString());

				field = "duration";
				if (isMetadataPresent(streamRoot, field))
				{
					string duration = streamRoot.get(field, "0").asString();

					// 2020-01-13: atoll remove the milliseconds and this is wrong
					// durationInMilliSeconds = atoll(duration.c_str()) * 1000;

					double dDurationInMilliSeconds = stod(duration);
					audioDurationInMilliSeconds = dDurationInMilliSeconds * 1000;
				}

				string language;
                string tagsField = "tags";
                if (isMetadataPresent(streamRoot, tagsField))
                {
					field = "language";
					if (isMetadataPresent(streamRoot[tagsField], field))
					{
						language = streamRoot[tagsField].get(field, "").asString();
					}
                }

				audioTracks.push_back(make_tuple(trackIndex, audioDurationInMilliSeconds,
					audioCodecName, audioSampleRate, audioChannels, audioBitRate, language));
            }
        }

        field = "format";
        if (!isMetadataPresent(detailsRoot, field))
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        Json::Value formatRoot = detailsRoot[field];

        field = "duration";
        if (!isMetadataPresent(formatRoot, field))
        {
			// I didn't find it in a .avi file generated using OpenCV::VideoWriter
			// let's log it as a warning
            if (firstVideoCodecName != "" && firstVideoCodecName != "mjpeg")
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
                _logger->warn(errorMessage);

                // throw runtime_error(errorMessage);
            }            
        }
        else
        {
            string duration = formatRoot.get(field, "XXX").asString();

			// 2020-01-13: atoll remove the milliseconds and this is wrong
            // durationInMilliSeconds = atoll(duration.c_str()) * 1000;

            double dDurationInMilliSeconds = stod(duration);
            durationInMilliSeconds = dDurationInMilliSeconds * 1000;
        }

        field = "bit_rate";
        if (!isMetadataPresent(formatRoot, field))
        {
            if (firstVideoCodecName != "" && firstVideoCodecName != "mjpeg")
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", mmsAssetPathName: " + mmsAssetPathName
                    + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }            
        }
        else
        {
            string bit_rate = formatRoot.get(field, "XXX").asString();
            bitRate = atoll(bit_rate.c_str());
        }

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);
    }
    catch(runtime_error e)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: error processing ffprobe output"
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);

        throw e;
    }
    catch(exception e)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: error processing ffprobe output"
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", detailsPathFileName: " + detailsPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(detailsPathFileName, exceptionInCaseOfError);

        throw e;
    }

    /*
    if (durationInMilliSeconds == -1)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: durationInMilliSeconds was not able to be retrieved from media"
                + ", mmsAssetPathName: " + mmsAssetPathName
                + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds);
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }
    else if (width == -1 || height == -1)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: width/height were not able to be retrieved from media"
                + ", mmsAssetPathName: " + mmsAssetPathName
                + ", width: " + to_string(width)
                + ", height: " + to_string(height)
                ;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }
     */
    
	/*
    _logger->info(__FILEREF__ + "FFMpeg::getMediaInfo"
        + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
        + ", bitRate: " + to_string(bitRate)
        + ", videoCodecName: " + videoCodecName
        + ", videoProfile: " + videoProfile
        + ", videoWidth: " + to_string(videoWidth)
        + ", videoHeight: " + to_string(videoHeight)
        + ", videoAvgFrameRate: " + videoAvgFrameRate
        + ", videoBitRate: " + to_string(videoBitRate)
        + ", audioCodecName: " + audioCodecName
        + ", audioSampleRate: " + to_string(audioSampleRate)
        + ", audioChannels: " + to_string(audioChannels)
        + ", audioBitRate: " + to_string(audioBitRate)
    );
	*/

	return make_pair(durationInMilliSeconds, bitRate);
}

vector<string> FFMpeg::generateFramesToIngest(
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
		pid_t* pChildPid)
{
	_currentApiName = "generateFramesToIngest";

	setStatus(
		ingestionJobKey,
		encodingJobKey,
		videoDurationInMilliSeconds,
		mmsAssetPathName
		// stagingEncodedAssetPathName
	);

    _logger->info(__FILEREF__ + "generateFramesToIngest"
        + ", ingestionJobKey: " + to_string(ingestionJobKey)
        + ", encodingJobKey: " + to_string(encodingJobKey)
        + ", imageDirectory: " + imageDirectory
        + ", imageBaseFileName: " + imageBaseFileName
        + ", startTimeInSeconds: " + to_string(startTimeInSeconds)
        + ", framesNumber: " + to_string(framesNumber)
        + ", videoFilter: " + videoFilter
        + ", periodInSeconds: " + to_string(periodInSeconds)
        + ", mjpeg: " + to_string(mjpeg)
        + ", imageWidth: " + to_string(imageWidth)
        + ", imageHeight: " + to_string(imageHeight)
        + ", mmsAssetPathName: " + mmsAssetPathName
        + ", videoDurationInMilliSeconds: " + to_string(videoDurationInMilliSeconds)
    );
    
	int iReturnedStatus = 0;

    // _currentDurationInMilliSeconds      = videoDurationInMilliSeconds;
    // _currentMMSSourceAssetPathName      = mmsAssetPathName;
    // _currentIngestionJobKey             = ingestionJobKey;
    // _currentEncodingJobKey              = encodingJobKey;
        
    vector<string> generatedFramesFileNames;
    
    _outputFfmpegPathFileName =
            _ffmpegTempDir + "/"
            + to_string(_currentIngestionJobKey)
            + "_"
            + to_string(_currentEncodingJobKey)
            + ".generateFrame.log"
            ;
        
    string localImageFileName;
    if (mjpeg)
    {
        localImageFileName = imageBaseFileName + ".mjpeg";
    }
    else
    {
        if (framesNumber == -1 || framesNumber > 1)
            localImageFileName = imageBaseFileName + "_%04d.jpg";
        else
            localImageFileName = imageBaseFileName + ".jpg";
    }

    string videoFilterParameters;
    if (videoFilter == "PeriodicFrame")
    {
        videoFilterParameters = "-vf fps=1/" + to_string(periodInSeconds) + " ";
    }
    else if (videoFilter == "All-I-Frames")
    {
        if (mjpeg)
            videoFilterParameters = "-vf select='eq(pict_type,PICT_TYPE_I)' ";
        else
            videoFilterParameters = "-vf select='eq(pict_type,PICT_TYPE_I)' -vsync vfr ";
    }
    
    /*
        ffmpeg -y -i [source.wmv] -f mjpeg -ss [10] -vframes 1 -an -s [176x144] [thumbnail_image.jpg]
        -y: overwrite output files
        -i: input file name
        -f: force format
        -ss: When used as an output option (before an output url), decodes but discards input 
            until the timestamps reach position.
            Format: HH:MM:SS.xxx (xxx are decimals of seconds) or in seconds (sec.decimals)
        -vframes: set the number of video frames to record
        -an: disable audio
        -s set frame size (WxH or abbreviation)
     */
    // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
#ifdef __EXECUTE__
    string globalOptions = "-y ";
    string inputOptions = "";
    string outputOptions =
            "-ss " + to_string(startTimeInSeconds) + " "
            + (framesNumber != -1 ? ("-vframes " + to_string(framesNumber)) : "") + " "
            + videoFilterParameters
            + (mjpeg ? "-f mjpeg " : "")
            + "-an -s " + to_string(imageWidth) + "x" + to_string(imageHeight) + " "
            ;
    string ffmpegExecuteCommand = 
            _ffmpegPath + "/ffmpeg "
            + globalOptions
            + inputOptions
            + "-i " + mmsAssetPathName + " "
            + outputOptions
            + imageDirectory + "/" + localImageFileName + " "
            + "> " + _outputFfmpegPathFileName + " "
            + "2>&1"
            ;

    #ifdef __APPLE__
        ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif
#else
	vector<string> ffmpegArgumentList;
	ostringstream ffmpegArgumentListStream;

	ffmpegArgumentList.push_back("ffmpeg");
	// global options
	ffmpegArgumentList.push_back("-y");
	// input options
	ffmpegArgumentList.push_back("-i");
	ffmpegArgumentList.push_back(mmsAssetPathName);
	// output options
	ffmpegArgumentList.push_back("-ss");
	ffmpegArgumentList.push_back(to_string(startTimeInSeconds));
	if (framesNumber != -1)
	{
		ffmpegArgumentList.push_back("-vframes");
		ffmpegArgumentList.push_back(to_string(framesNumber));
	}
	addToArguments(videoFilterParameters, ffmpegArgumentList);
	if (mjpeg)
	{
		ffmpegArgumentList.push_back("-f");
		ffmpegArgumentList.push_back("mjpeg");
	}
	ffmpegArgumentList.push_back("-an");
	ffmpegArgumentList.push_back("-s");
	ffmpegArgumentList.push_back(to_string(imageWidth) + "x" + to_string(imageHeight));
	ffmpegArgumentList.push_back(imageDirectory + "/" + localImageFileName);
#endif

    try
    {
        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

	#ifdef __EXECUTE__
        _logger->info(__FILEREF__ + "generateFramesToIngest: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );

        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
            string errorMessage = __FILEREF__ + "generateFramesToIngest: ffmpeg command failed"
                    + ", executeCommandStatus: " + to_string(executeCommandStatus)
                    + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
	#else
		if (!ffmpegArgumentList.empty())
			copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
				ostream_iterator<string>(ffmpegArgumentListStream, " "));

        _logger->info(__FILEREF__ + "generateFramesToIngest: Executing ffmpeg command"
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
        );

		bool redirectionStdOutput = true;
		bool redirectionStdError = true;

		ProcessUtility::forkAndExec (
			_ffmpegPath + "/ffmpeg",
			ffmpegArgumentList,
			_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
			pChildPid, &iReturnedStatus);
		if (iReturnedStatus != 0)
        {
			string errorMessage = __FILEREF__ + "generateFramesToIngest: ffmpeg command failed"
				+ ", encodingJobKey: " + to_string(encodingJobKey)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", iReturnedStatus: " + to_string(iReturnedStatus)
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
            ;            
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
	#endif

        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();
        
	#ifdef __EXECUTE__
        _logger->info(__FILEREF__ + "generateFramesToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
	#else
        _logger->info(__FILEREF__ + "generateFramesToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
	#endif
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
	#ifdef __EXECUTE__
        string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
	#else
		string errorMessage;
		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
		else
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
	#endif
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else
			throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);
     
    if (mjpeg || framesNumber == 1)
        generatedFramesFileNames.push_back(localImageFileName);
    else
    {
        // get files from file system
    
        FileIO::DirectoryEntryType_t detDirectoryEntryType;
        shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (imageDirectory + "/");

        bool scanDirectoryFinished = false;
        while (!scanDirectoryFinished)
        {
            string directoryEntry;
            try
            {
                string directoryEntry = FileIO::readDirectory (directory,
                    &detDirectoryEntryType);
                
                if (detDirectoryEntryType != FileIO::TOOLS_FILEIO_REGULARFILE)
                    continue;

                if (directoryEntry.size() >= imageBaseFileName.size() && 0 == directoryEntry.compare(0, imageBaseFileName.size(), imageBaseFileName))
                    generatedFramesFileNames.push_back(directoryEntry);
            }
            catch(DirectoryListFinished e)
            {
                scanDirectoryFinished = true;
            }
            catch(runtime_error e)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: listing directory failed"
                       + ", e.what(): " + e.what()
                ;
                _logger->error(errorMessage);

                throw e;
            }
            catch(exception e)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: listing directory failed"
                       + ", e.what(): " + e.what()
                ;
                _logger->error(errorMessage);

                throw e;
            }
        }

        FileIO::closeDirectory (directory);
    }
    
    /*
    bool inCaseOfLinkHasItToBeRead = false;
    unsigned long ulFileSize = FileIO::getFileSizeInBytes (
        localImagePathName, inCaseOfLinkHasItToBeRead);

    if (ulFileSize == 0)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed, image file size is 0"
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        ;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    } 
    */ 
    
    return generatedFramesFileNames;
}

void FFMpeg::generateConcatMediaToIngest(
        int64_t ingestionJobKey,
        vector<string>& sourcePhysicalPaths,
        string concatenatedMediaPathName)
{
	_currentApiName = "generateConcatMediaToIngest";

	setStatus(
		ingestionJobKey
		/*
		encodingJobKey
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    string concatenationListPathName =
        _ffmpegTempDir + "/"
        + to_string(ingestionJobKey)
        + ".concatList.txt"
        ;
        
    ofstream concatListFile(concatenationListPathName.c_str(), ofstream::trunc);
    for (string sourcePhysicalPath: sourcePhysicalPaths)
    {
        _logger->info(__FILEREF__ + "ffmpeg: adding physical path"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", sourcePhysicalPath: " + sourcePhysicalPath
        );
        
        concatListFile << "file '" << sourcePhysicalPath << "'" << endl;
    }
    concatListFile.close();

    _outputFfmpegPathFileName =
            _ffmpegTempDir + "/"
            + to_string(ingestionJobKey)
            + ".concat.log"
            ;

    // Then you can stream copy or re-encode your files
    // The -safe 0 above is not required if the paths are relative
    // ffmpeg -f concat -safe 0 -i mylist.txt -c copy output
	// 2019-10-10: added -fflags +genpts -async 1 for lipsync issue!!!
	// 2019-10-11: removed -fflags +genpts -async 1 because does not have inpact on lipsync issue!!!
    string ffmpegExecuteCommand = 
            _ffmpegPath + "/ffmpeg "
            + "-f concat -safe 0 -i " + concatenationListPathName + " "
            + "-c copy " + concatenatedMediaPathName + " "
            + "> " + _outputFfmpegPathFileName + " "
            + "2>&1"
            ;

    #ifdef __APPLE__
        ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif

    try
    {
        _logger->info(__FILEREF__ + "generateConcatMediaToIngest: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );

        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
			// 2020-07-20: added the log of the input file
			string inputBuffer;
			{
				ifstream inputFile(concatenationListPathName);
				stringstream input;
				input << inputFile.rdbuf();

				inputBuffer = input.str();
			}
            string errorMessage = __FILEREF__ + "generateConcatMediaToIngest: ffmpeg command failed"
				+ ", executeCommandStatus: " + to_string(executeCommandStatus)
				+ ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
				+ ", inputBuffer: " + inputBuffer
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "generateConcatMediaToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @"
				+ to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		// 2020-07-20: log of ffmpegExecuteCommand commented because already added into the catched exception
        string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                // + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        bool exceptionInCaseOfError = false;
        _logger->info(__FILEREF__ + "Remove"
            + ", concatenationListPathName: " + concatenationListPathName);
        FileIO::remove(concatenationListPathName, exceptionInCaseOfError);

        _logger->info(__FILEREF__ + "Remove"
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

		throw runtime_error(errorMessage);
    }

    bool exceptionInCaseOfError = false;
    _logger->info(__FILEREF__ + "Remove"
        + ", concatenationListPathName: " + concatenationListPathName);
    FileIO::remove(concatenationListPathName, exceptionInCaseOfError);
    _logger->info(__FILEREF__ + "Remove"
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::generateCutMediaToIngest(
        int64_t ingestionJobKey,
        string sourcePhysicalPath,
		bool keyFrameSeeking,
        double startTimeInSeconds,
        double endTimeInSeconds,
        int framesNumber,
        string cutMediaPathName)
{

	_currentApiName = "generateCutMediaToIngest";

	setStatus(
		ingestionJobKey
		/*
		encodingJobKey
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    _outputFfmpegPathFileName =
            _ffmpegTempDir + "/"
            + to_string(ingestionJobKey)
            + ".cut.log"
            ;

    /*
        -ss: When used as an output option (before an output url), decodes but discards input 
            until the timestamps reach position.
            Format: HH:MM:SS.xxx (xxx are decimals of seconds) or in seconds (sec.decimals)
		2019-09-24: Added -async 1 option because the Escenic transcoder (ffmpeg) was failing
			The generated error was: Too many packets buffered for output stream
			(look https://trac.ffmpeg.org/ticket/6375)
			-async samples_per_second
				Audio sync method. "Stretches/squeezes" the audio stream to match the timestamps, the parameter is
				the maximum samples per second by which the audio is changed.  -async 1 is a special case where only
				the start of the audio stream is corrected without any later correction.
			-af "aresample=async=1:min_hard_comp=0.100000:first_pts=0" helps to keep your audio lined up
				with the beginning of your video. It is common for a container to have the beginning
				of the video and the beginning of the audio start at different points. By using this your container
				should have little to no audio drift or offset as it will pad the audio with silence or trim audio
				with negative PTS timestamps if the audio does not actually start at the beginning of the video.
		2019-09-26: introduced the concept of 'Key-Frame Seeking' vs 'All-Frame Seeking' vs 'Full Re-Encoding'
			(see http://www.markbuckler.com/post/cutting-ffmpeg/)
    */
    string ffmpegExecuteCommand;
	if (keyFrameSeeking)
	{
		ffmpegExecuteCommand = 
            _ffmpegPath + "/ffmpeg "
            + "-ss " + to_string(startTimeInSeconds) + " "
            + (framesNumber != -1 ? ("-vframes " + to_string(framesNumber) + " ") : ("-to " + to_string(endTimeInSeconds) + " "))
            + "-i " + sourcePhysicalPath + " "
			+ "-async 1 "
			// commented because aresample filtering requires encoding and here we are just streamcopy
            // + "-af \"aresample=async=1:min_hard_comp=0.100000:first_pts=0\" "
            + "-c copy " + cutMediaPathName + " "
            + "> " + _outputFfmpegPathFileName + " "
            + "2>&1"
            ;
	}
	else
		ffmpegExecuteCommand = 
            _ffmpegPath + "/ffmpeg "
            + "-i " + sourcePhysicalPath + " "
            + "-ss " + to_string(startTimeInSeconds) + " "
            + (framesNumber != -1 ? ("-vframes " + to_string(framesNumber) + " ") : ("-to " + to_string(endTimeInSeconds) + " "))
			+ "-async 1 "
			// commented because aresample filtering requires encoding and here we are just streamcopy
            // + "-af \"aresample=async=1:min_hard_comp=0.100000:first_pts=0\" "
            + "-c copy " + cutMediaPathName + " "
            + "> " + _outputFfmpegPathFileName + " "
            + "2>&1"
            ;

    #ifdef __APPLE__
        ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif

    try
    {
        _logger->info(__FILEREF__ + "generateCutMediaToIngest: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );

        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
            string errorMessage = __FILEREF__ + "generateCutMediaToIngest: ffmpeg command failed"
                    + ", executeCommandStatus: " + to_string(executeCommandStatus)
                    + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "generateCutMediaToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
        string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

        throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::generateSlideshowMediaToIngest(
        int64_t ingestionJobKey,
        int64_t encodingJobKey,
        vector<string>& sourcePhysicalPaths,
        double durationOfEachSlideInSeconds, 
        int outputFrameRate,
        string slideshowMediaPathName,
		pid_t* pChildPid)
{
	_currentApiName = "generateSlideshowMediaToIngest";

	setStatus(
		ingestionJobKey,
		encodingJobKey
		/*
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

	int iReturnedStatus = 0;

    string slideshowListPathName =
        _ffmpegTempDir + "/"
        + to_string(ingestionJobKey)
        + ".slideshowList.txt"
        ;
        
    ofstream slideshowListFile(slideshowListPathName.c_str(), ofstream::trunc);
    string lastSourcePhysicalPath;
    for (string sourcePhysicalPath: sourcePhysicalPaths)
    {
        slideshowListFile << "file '" << sourcePhysicalPath << "'" << endl;
        slideshowListFile << "duration " << durationOfEachSlideInSeconds << endl;
        
        lastSourcePhysicalPath = sourcePhysicalPath;
    }
    slideshowListFile << "file '" << lastSourcePhysicalPath << "'" << endl;
    slideshowListFile.close();

    _outputFfmpegPathFileName =
            _ffmpegTempDir + "/"
            + to_string(ingestionJobKey)
            + ".slideshow.log"
            ;
    
    // Then you can stream copy or re-encode your files
    // The -safe 0 above is not required if the paths are relative
    // ffmpeg -f concat -safe 0 -i mylist.txt -c copy output

#ifdef __EXECUTE__
    string ffmpegExecuteCommand = 
            _ffmpegPath + "/ffmpeg "
            + "-f concat -safe 0 " 
            // + "-framerate 5/1 "
            + "-i " + slideshowListPathName + " "
            + "-c:v libx264 "
            + "-r " + to_string(outputFrameRate) + " "
            + "-vsync vfr "
            + "-pix_fmt yuv420p " + slideshowMediaPathName + " "
            + "> " + _outputFfmpegPathFileName + " "
            + "2>&1"
            ;

    #ifdef __APPLE__
        ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif
#else
	vector<string> ffmpegArgumentList;
	ostringstream ffmpegArgumentListStream;

	ffmpegArgumentList.push_back("ffmpeg");
	ffmpegArgumentList.push_back("-f");
	ffmpegArgumentList.push_back("concat");
	ffmpegArgumentList.push_back("-safe");
	ffmpegArgumentList.push_back("0");
    // + "-framerate 5/1 "
	ffmpegArgumentList.push_back("-i");
	ffmpegArgumentList.push_back(slideshowListPathName);
	ffmpegArgumentList.push_back("-c:v");
	ffmpegArgumentList.push_back("libx264");
	ffmpegArgumentList.push_back("-r");
	ffmpegArgumentList.push_back(to_string(outputFrameRate));
	ffmpegArgumentList.push_back("-vsync");
	ffmpegArgumentList.push_back("vfr");
	ffmpegArgumentList.push_back("-pix_fmt");
	ffmpegArgumentList.push_back("yuv420p");
	ffmpegArgumentList.push_back(slideshowMediaPathName);
#endif

    try
    {
        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

	#ifdef __EXECUTE__
        _logger->info(__FILEREF__ + "generateSlideshowMediaToIngest: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );

        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
            string errorMessage = __FILEREF__ + "generateSlideshowMediaToIngest: ffmpeg command failed"
                    + ", executeCommandStatus: " + to_string(executeCommandStatus)
                    + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
	#else
		if (!ffmpegArgumentList.empty())
			copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
				ostream_iterator<string>(ffmpegArgumentListStream, " "));

        _logger->info(__FILEREF__ + "generateSlideshowMediaToIngest: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
        );

		bool redirectionStdOutput = true;
		bool redirectionStdError = true;

		ProcessUtility::forkAndExec (
			_ffmpegPath + "/ffmpeg",
			ffmpegArgumentList,
			_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
			pChildPid, &iReturnedStatus);
		if (iReturnedStatus != 0)
        {
			string errorMessage = __FILEREF__ + "generateSlideshowMediaToIngest: ffmpeg command failed"
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", iReturnedStatus: " + to_string(iReturnedStatus)
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
            ;            
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
	#endif
        
        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

	#ifdef __EXECUTE__
        _logger->info(__FILEREF__ + "generateSlideshowMediaToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
	#else
        _logger->info(__FILEREF__ + "generateSlideshowMediaToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
	#endif
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		#ifdef __EXECUTE__
			string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
			;
		#else
			string errorMessage;
			if (iReturnedStatus == 9)	// 9 means: SIGKILL
				errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
					+ ", e.what(): " + e.what()
				;
			else
				errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
					+ ", e.what(): " + e.what()
				;
		#endif
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

        _logger->info(__FILEREF__ + "Remove"
            + ", slideshowListPathName: " + slideshowListPathName);
        FileIO::remove(slideshowListPathName, exceptionInCaseOfError);

		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else
			throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
    
    _logger->info(__FILEREF__ + "Remove"
        + ", slideshowListPathName: " + slideshowListPathName);
    FileIO::remove(slideshowListPathName, exceptionInCaseOfError);
}

void FFMpeg::extractTrackMediaToIngest(
        int64_t ingestionJobKey,
        string sourcePhysicalPath,
        vector<pair<string,int>>& tracksToBeExtracted,
        string extractTrackMediaPathName)
{
	_currentApiName = "extractTrackMediaToIngest";

	setStatus(
		ingestionJobKey
		/*
		encodingJobKey
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    _outputFfmpegPathFileName =
            _ffmpegTempDir + "/"
            + to_string(ingestionJobKey)
            + ".extractTrack.log"
            ;

    string mapParameters;
    bool videoTrackIsPresent = false;
    bool audioTrackIsPresent = false;
    for (pair<string,int>& trackToBeExtracted: tracksToBeExtracted)
    {
        string trackType;
        int trackNumber;
        
        tie(trackType,trackNumber) = trackToBeExtracted;
        
        mapParameters += (string("-map 0:") + (trackType == "video" ? "v" : "a") + ":" + to_string(trackNumber) + " ");
        
        if (trackType == "video")
            videoTrackIsPresent = true;
        else
            audioTrackIsPresent = true;
    }
    /*
        -map option: http://ffmpeg.org/ffmpeg.html#Advanced-options
        -c:a copy:      codec option for audio streams has been set to copy, so no decoding-filtering-encoding operations will occur
        -an:            disables audio stream selection for the output
    */
    // ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
    string globalOptions = "-y ";
    string inputOptions = "";
    string outputOptions =
            mapParameters
            + (videoTrackIsPresent ? (string("-c:v") + " copy ") : "")
            + (audioTrackIsPresent ? (string("-c:a") + " copy ") : "")
            + (videoTrackIsPresent && !audioTrackIsPresent ? "-an " : "")
            + (!videoTrackIsPresent && audioTrackIsPresent ? "-vn " : "")
            ;

    string ffmpegExecuteCommand =
            _ffmpegPath + "/ffmpeg "
            + globalOptions
            + inputOptions
            + "-i " + sourcePhysicalPath + " "
            + outputOptions
            + extractTrackMediaPathName + " "
            + "> " + _outputFfmpegPathFileName 
            + " 2>&1"
    ;

    #ifdef __APPLE__
        ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=") + getenv("DYLD_LIBRARY_PATH") + "; ");
    #endif

    try
    {
        _logger->info(__FILEREF__ + "extractTrackMediaToIngest: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );

        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
            string errorMessage = __FILEREF__ + "extractTrackMediaToIngest: ffmpeg command failed"
                    + ", executeCommandStatus: " + to_string(executeCommandStatus)
                    + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "extractTrackMediaToIngest: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
        string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

        throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::liveRecorder(
        int64_t ingestionJobKey,
        int64_t encodingJobKey,
		string segmentListPathName,
		string recordedFileNamePrefix,
        string liveURL, string userAgent,
        time_t utcRecordingPeriodStart, 
        time_t utcRecordingPeriodEnd, 
        int segmentDurationInSeconds,
        string outputFileFormat,
		pid_t* pChildPid)
{
	_currentApiName = "liveRecorder";

	setStatus(
		ingestionJobKey,
		encodingJobKey
		/*
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

#ifdef __EXECUTE__
	string ffmpegExecuteCommand;
#else
	vector<string> ffmpegArgumentList;
	ostringstream ffmpegArgumentListStream;
	int iReturnedStatus = 0;
#endif
	string segmentListPath;
	chrono::system_clock::time_point startFfmpegCommand;
	chrono::system_clock::time_point endFfmpegCommand;
	time_t utcNow;

    try
    {
		size_t segmentListPathIndex = segmentListPathName.find_last_of("/");
		if (segmentListPathIndex == string::npos)
		{
			string errorMessage = __FILEREF__ + "No segmentListPath find in the segment path name"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
                   + ", segmentListPathName: " + segmentListPathName;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
		segmentListPath = segmentListPathName.substr(0, segmentListPathIndex);

		// directory is created by EncoderVideoAudioProxy using MMSStorage::getStagingAssetPathName
		// I saw just once that the directory was not created and the liveencoder remains in the loop
		// where:
		//	1. the encoder returns an error becaise of the missing directory
		//	2. EncoderVideoAudioProxy calls again the encoder
		// So, for this reason, the below check is done
		if (!FileIO::directoryExisting(segmentListPath))
		{
			_logger->warn(__FILEREF__ + "segmentListPath does not exist!!! It will be created"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", segmentListPath: " + segmentListPath
					);

			_logger->info(__FILEREF__ + "Create directory"
                + ", segmentListPath: " + segmentListPath
            );
			bool noErrorIfExists = true;
			bool recursive = true;
			FileIO::createDirectory(segmentListPath,
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IXGRP |
				S_IROTH | S_IXOTH, noErrorIfExists, recursive);
		}

		{
			chrono::system_clock::time_point now = chrono::system_clock::now();
			utcNow = chrono::system_clock::to_time_t(now);
		}

		if (utcNow < utcRecordingPeriodStart)
		{
			// 2019-12-19: since the first chunk is removed, we will start a bit early
			// than utcRecordingPeriodStart (50% less than segmentDurationInSeconds)
			long secondsToStartEarly =
				segmentDurationInSeconds * 50 / 100;

			while (utcNow + secondsToStartEarly < utcRecordingPeriodStart)
			{
				time_t sleepTime = utcRecordingPeriodStart - (utcNow + secondsToStartEarly);

				_logger->info(__FILEREF__ + "LiveRecorder timing. "
						+ "Too early to start the LiveRecorder, just sleep "
					+ to_string(sleepTime) + " seconds"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", utcNow: " + to_string(utcNow)
                    + ", secondsToStartEarly: " + to_string(secondsToStartEarly)
                    + ", utcRecordingPeriodStart: " + to_string(utcRecordingPeriodStart)
					);

				this_thread::sleep_for(chrono::seconds(sleepTime));

				{
					chrono::system_clock::time_point now = chrono::system_clock::now();
					utcNow = chrono::system_clock::to_time_t(now);
				}
			}
		}
		else if (utcRecordingPeriodEnd <= utcNow)
        {
            string errorMessage = __FILEREF__ + "LiveRecorder timing. "
				+ "Too late to start the LiveRecorder"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", utcRecordingPeriodEnd: " + to_string(utcRecordingPeriodEnd)
                    + ", utcNow: " + to_string(utcNow)
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
		else
		{
            string errorMessage = __FILEREF__ + "LiveRecorder timing. "
				+ "We are a bit late to start the LiveRecorder, let's start it"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", delay (secs): " + to_string(utcNow - utcRecordingPeriodStart)
                    + ", utcRecordingPeriodStart: " + to_string(utcRecordingPeriodStart)
                    + ", utcNow: " + to_string(utcNow)
                    + ", utcRecordingPeriodEnd: " + to_string(utcRecordingPeriodEnd)
            ;

            _logger->warn(errorMessage);
		}

		_outputFfmpegPathFileName =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey) + "_"
			+ to_string(encodingJobKey)
			+ ".liveRecorder.log"
			;
    
		string recordedFileNameTemplate = recordedFileNamePrefix
			+ "_%Y-%m-%d_%H-%M-%S_%s." + outputFileFormat;

	{
		chrono::system_clock::time_point now = chrono::system_clock::now();
		utcNow = chrono::system_clock::to_time_t(now);
	}

	#ifdef __EXECUTE__
		ffmpegExecuteCommand = 
			_ffmpegPath + "/ffmpeg "
			+ (userAgent == "" ? "" : "-user_agent " + userAgent + " ")
			+ "-i " + liveURL + " "
			+ "-t " + to_string(utcRecordingPeriodEnd - utcNow) + " "
			+ "-c:v copy "
			+ "-c:a copy "
			+ "-f segment "
			+ "-segment_list " + segmentListPathName + " "
			+ "-segment_time " + to_string(segmentDurationInSeconds) + " "
			+ "-segment_atclocktime 1 "
			+ "-strftime 1 \"" + segmentListPath + "/" + recordedFileNameTemplate + "\" "
			+ "> " + _outputFfmpegPathFileName + " "
			+ "2>&1"
		;

		#ifdef __APPLE__
			ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=")
					+ getenv("DYLD_LIBRARY_PATH") + "; ");
		#endif

        _logger->info(__FILEREF__ + "liveRecorder: Executing ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );
	#else

		ffmpegArgumentList.push_back("ffmpeg");
		// addToArguments("-loglevel repeat+level+trace", ffmpegArgumentList);
		if (userAgent != "")
		{
			ffmpegArgumentList.push_back("-user_agent");
			ffmpegArgumentList.push_back(userAgent);
		}
		ffmpegArgumentList.push_back("-i");
		ffmpegArgumentList.push_back(liveURL);
		ffmpegArgumentList.push_back("-t");
		ffmpegArgumentList.push_back(to_string(utcRecordingPeriodEnd - utcNow));
		ffmpegArgumentList.push_back("-c:v");
		ffmpegArgumentList.push_back("copy");
		ffmpegArgumentList.push_back("-c:a");
		ffmpegArgumentList.push_back("copy");
		ffmpegArgumentList.push_back("-f");
		ffmpegArgumentList.push_back("segment");
		ffmpegArgumentList.push_back("-segment_list");
		ffmpegArgumentList.push_back(segmentListPathName);
		ffmpegArgumentList.push_back("-segment_time");
		ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));
		ffmpegArgumentList.push_back("-segment_atclocktime");
		ffmpegArgumentList.push_back("1");
		ffmpegArgumentList.push_back("-strftime");
		ffmpegArgumentList.push_back("1");
		ffmpegArgumentList.push_back(segmentListPath + "/" + recordedFileNameTemplate);

		if (!ffmpegArgumentList.empty())
			copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
				ostream_iterator<string>(ffmpegArgumentListStream, " "));

        _logger->info(__FILEREF__ + "liveRecorder: Executing ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
        );
	#endif

        startFfmpegCommand = chrono::system_clock::now();

	#ifdef __EXECUTE__
        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
            string errorMessage = __FILEREF__ + "liveRecorder: ffmpeg command failed"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
                    + ", executeCommandStatus: " + to_string(executeCommandStatus)
                    + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
	#else
		bool redirectionStdOutput = true;
		bool redirectionStdError = true;

		ProcessUtility::forkAndExec (
			_ffmpegPath + "/ffmpeg",
			ffmpegArgumentList,
			_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
			pChildPid, &iReturnedStatus);
		if (iReturnedStatus != 0)
        {
			string errorMessage = __FILEREF__ + "liveRecorder: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
                + ", iReturnedStatus: " + to_string(iReturnedStatus)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
            ;            
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
	#endif
        
        endFfmpegCommand = chrono::system_clock::now();

	#ifdef __EXECUTE__
        _logger->info(__FILEREF__ + "liveRecorder: Executed ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
	#else
        _logger->info(__FILEREF__ + "liveRecorder: Executed ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
	#endif

		if (endFfmpegCommand - startFfmpegCommand < chrono::seconds(utcRecordingPeriodEnd - utcNow - 60))
		{
			char		sEndFfmpegCommand [64];

			time_t	utcEndFfmpegCommand = chrono::system_clock::to_time_t(endFfmpegCommand);
			tm		tmUtcEndFfmpegCommand;
			localtime_r (&utcEndFfmpegCommand, &tmUtcEndFfmpegCommand);
			sprintf (sEndFfmpegCommand, "%04d-%02d-%02d-%02d-%02d-%02d",
				tmUtcEndFfmpegCommand. tm_year + 1900,
				tmUtcEndFfmpegCommand. tm_mon + 1,
				tmUtcEndFfmpegCommand. tm_mday,
				tmUtcEndFfmpegCommand. tm_hour,
				tmUtcEndFfmpegCommand. tm_min,
				tmUtcEndFfmpegCommand. tm_sec);

			string debugOutputFfmpegPathFileName =
				_ffmpegTempDir + "/"
				+ to_string(ingestionJobKey) + "_"
				+ to_string(encodingJobKey) + "_"
				+ sEndFfmpegCommand
				+ ".liveRecorder.log.debug"
				;

			_logger->info(__FILEREF__ + "Coping"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", debugOutputFfmpegPathFileName: " + debugOutputFfmpegPathFileName
				);
			FileIO::copyFile(_outputFfmpegPathFileName, debugOutputFfmpegPathFileName);    

			throw runtime_error("liveRecording exit before unexpectly");
		}
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		#ifdef __EXECUTE__
			string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
                + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
			;
		#else
			string errorMessage;
			if (iReturnedStatus == 9)	// 9 means: SIGKILL
				errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
					+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
					+ ", e.what(): " + e.what()
				;
			else
				errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
					+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
					+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
					+ ", e.what(): " + e.what()
				;
		#endif
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

        _logger->info(__FILEREF__ + "Remove"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", segmentListPathName: " + segmentListPathName);
        FileIO::remove(segmentListPathName, exceptionInCaseOfError);

		if (segmentListPath != "")
    	{
        	// get files from file system
    
        	FileIO::DirectoryEntryType_t detDirectoryEntryType;
        	shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (segmentListPath + "/");

        	bool scanDirectoryFinished = false;
        	while (!scanDirectoryFinished)
        	{
            	string directoryEntry;
            	try
            	{
                	string directoryEntry = FileIO::readDirectory (directory,
                    	&detDirectoryEntryType);
                
                	if (detDirectoryEntryType != FileIO::TOOLS_FILEIO_REGULARFILE)
                    	continue;

                	if (directoryEntry.size() >= recordedFileNamePrefix.size() && 0 == directoryEntry.compare(0, recordedFileNamePrefix.size(), recordedFileNamePrefix))
					{
						string recordedPathNameToBeRemoved = segmentListPath + "/" + directoryEntry;
        				_logger->info(__FILEREF__ + "Remove"
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", encodingJobKey: " + to_string(encodingJobKey)
            				+ ", recordedPathNameToBeRemoved: " + recordedPathNameToBeRemoved);
        				// FileIO::remove(recordedPathNameToBeRemoved, exceptionInCaseOfError);
					}
            	}
            	catch(DirectoryListFinished e)
            	{
                	scanDirectoryFinished = true;
            	}
            	catch(runtime_error e)
            	{
                	string errorMessage = __FILEREF__ + "ffmpeg: listing directory failed"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
                       	+ ", e.what(): " + e.what()
                	;
                	_logger->error(errorMessage);

                	// throw e;
            	}
            	catch(exception e)
            	{
                	string errorMessage = __FILEREF__ + "ffmpeg: listing directory failed"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", e.what(): " + e.what()
                	;
                	_logger->error(errorMessage);

                	// throw e;
            	}
        	}

        	FileIO::closeDirectory (directory);
    	}

		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else if (lastPartOfFfmpegOutputFile.find("403 Forbidden") != string::npos)
			throw FFMpegURLForbidden();
		else if (lastPartOfFfmpegOutputFile.find("404 Not Found") != string::npos)
			throw FFMpegURLNotFound();
		else
			throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
		+ ", ingestionJobKey: " + to_string(ingestionJobKey)
		+ ", encodingJobKey: " + to_string(encodingJobKey)
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::liveProxyByHTTPStreaming(
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
	pid_t* pChildPid)
{
	vector<string> ffmpegArgumentList;
	ostringstream ffmpegArgumentListStream;
	int iReturnedStatus = 0;
	string segmentListPath;
	chrono::system_clock::time_point startFfmpegCommand;
	chrono::system_clock::time_point endFfmpegCommand;
	time_t utcNow;

	_currentApiName = "liveProxyByHTTPStreaming";

	setStatus(
		ingestionJobKey,
		encodingJobKey
		/*
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    try
    {
		string outputTypeLowerCase;
		outputTypeLowerCase.resize(outputType.size());
		transform(outputType.begin(), outputType.end(), outputTypeLowerCase.begin(),
				[](unsigned char c){return tolower(c); } );

		if (outputTypeLowerCase != "hls" && outputTypeLowerCase != "dash")
		{
			string errorMessage = __FILEREF__ + "liveProxyByHTTPStreaming. Wrong output type (it has to be HLS or DASH)"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", outputType: " + outputType;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}

		string manifestFilePathName = manifestDirectoryPath + "/" + manifestFileName;

		// directory is created by EncoderVideoAudioProxy using MMSStorage::getStagingAssetPathName
		// I saw just once that the directory was not created and the liveencoder remains in the loop
		// where:
		//	1. the encoder returns an error becaise of the missing directory
		//	2. EncoderVideoAudioProxy calls again the encoder
		// So, for this reason, the below check is done
		if (!FileIO::directoryExisting(manifestDirectoryPath))
		{
			_logger->warn(__FILEREF__ + "manifestDirectoryPath does not exist!!! It will be created"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", manifestDirectoryPath: " + manifestDirectoryPath
					);

			_logger->info(__FILEREF__ + "Create directory"
                + ", manifestDirectoryPath: " + manifestDirectoryPath
            );
			bool noErrorIfExists = true;
			bool recursive = true;
			FileIO::createDirectory(manifestDirectoryPath,
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IXGRP |
				S_IROTH | S_IXOTH, noErrorIfExists, recursive);
		}

		_outputFfmpegPathFileName =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey) + "_"
			+ to_string(encodingJobKey)
			+ ".liveProxy.log"
		;

		{
			chrono::system_clock::time_point now = chrono::system_clock::now();
			utcNow = chrono::system_clock::to_time_t(now);
		}

		ffmpegArgumentList.push_back("ffmpeg");
		// -re (input) Read input at native frame rate. By default ffmpeg attempts to read the input(s)
		//		as fast as possible. This option will slow down the reading of the input(s)
		//		to the native frame rate of the input(s). It is useful for real-time output
		//		(e.g. live streaming).
		// -hls_flags append_list: Append new segments into the end of old segment list
		//		and remove the #EXT-X-ENDLIST from the old segment list
		// -hls_time seconds: Set the target segment length in seconds. Segment will be cut on the next key frame
		//		after this time has passed.
		// -hls_list_size size: Set the maximum number of playlist entries. If set to 0 the list file
		//		will contain all the segments. Default value is 5.
		if (userAgent != "")
		{
			ffmpegArgumentList.push_back("-user_agent");
			ffmpegArgumentList.push_back(userAgent);
		}
		ffmpegArgumentList.push_back("-re");
		ffmpegArgumentList.push_back("-i");
		ffmpegArgumentList.push_back(liveURL);
		addToArguments(otherOutputOptions, ffmpegArgumentList);
		ffmpegArgumentList.push_back("-c:v");
		ffmpegArgumentList.push_back("copy");
		ffmpegArgumentList.push_back("-c:a");
		ffmpegArgumentList.push_back("copy");
		if (outputTypeLowerCase == "hls")
		{
			ffmpegArgumentList.push_back("-hls_flags");
			ffmpegArgumentList.push_back("append_list");
			ffmpegArgumentList.push_back("-hls_time");
			ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));
			ffmpegArgumentList.push_back("-hls_list_size");
			ffmpegArgumentList.push_back(to_string(playlistEntriesNumber));

			// Set the number of unreferenced segments to keep on disk
			// before 'hls_flags delete_segments' deletes them. Increase this to allow continue clients
			// to download segments which were recently referenced in the playlist.
			// Default value is 1, meaning segments older than hls_list_size+1 will be deleted.
			ffmpegArgumentList.push_back("-hls_delete_threshold");
			ffmpegArgumentList.push_back(to_string(1));

			// Segment files removed from the playlist are deleted after a period of time equal
			// to the duration of the segment plus the duration of the playlist.
			ffmpegArgumentList.push_back("-hls_flags");
			ffmpegArgumentList.push_back("delete_segments");

			// Start the playlist sequence number (#EXT-X-MEDIA-SEQUENCE) based on the current
			// date/time as YYYYmmddHHMMSS. e.g. 20161231235759
			// 2020-07-11: For the Live-Grid task, without -hls_start_number_source we have video-audio out of sync
			// 2020-07-19: commented, if it is needed just test it
			// ffmpegArgumentList.push_back("-hls_start_number_source");
			// ffmpegArgumentList.push_back("datetime");

			// 2020-07-19: commented, if it is needed just test it
			// ffmpegArgumentList.push_back("-start_number");
			// ffmpegArgumentList.push_back(to_string(10));
		}
		else if (outputTypeLowerCase == "dash")
		{
			ffmpegArgumentList.push_back("-seg_duration");
			ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));
			ffmpegArgumentList.push_back("-window_size");
			ffmpegArgumentList.push_back(to_string(playlistEntriesNumber));

			// it is important to specify -init_seg_name because those files
			// will not be removed in EncoderVideoAudioProxy.cpp
			ffmpegArgumentList.push_back("-init_seg_name");
			ffmpegArgumentList.push_back("init-stream$RepresentationID$.$ext$");

			// the only difference with the ffmpeg default is that default is $Number%05d$
			// We had to change it to $Number%01d$ because otherwise the generated file containing
			// 00001 00002 ... but the videojs player generates file name like 1 2 ...
			// and the streaming was not working
			ffmpegArgumentList.push_back("-media_seg_name");
			ffmpegArgumentList.push_back("chunk-stream$RepresentationID$-$Number%01d$.$ext$");
		}
		ffmpegArgumentList.push_back(manifestFilePathName);

		if (!ffmpegArgumentList.empty())
			copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
				ostream_iterator<string>(ffmpegArgumentListStream, " "));

		_logger->info(__FILEREF__ + "liveProxy: Executing ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
		);

		startFfmpegCommand = chrono::system_clock::now();

		bool redirectionStdOutput = true;
		bool redirectionStdError = true;

		ProcessUtility::forkAndExec (
			_ffmpegPath + "/ffmpeg",
			ffmpegArgumentList,
			_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
			pChildPid, &iReturnedStatus);
		if (iReturnedStatus != 0)
		{
			string errorMessage = __FILEREF__ + "liveProxy: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", iReturnedStatus: " + to_string(iReturnedStatus)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
           ;            
           _logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
        
		endFfmpegCommand = chrono::system_clock::now();

		_logger->info(__FILEREF__ + "liveProxy: Executed ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
			+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
		);
    }
    catch(runtime_error e)
    {
		string lastPartOfFfmpegOutputFile = getLastPartOfFile(
			_outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		string errorMessage;
		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
		else
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

		if (manifestDirectoryPath != "")
    	{
			if (FileIO::directoryExisting(manifestDirectoryPath))
			{
				try
				{
					_logger->info(__FILEREF__ + "removeDirectory"
						+ ", manifestDirectoryPath: " + manifestDirectoryPath
					);
					Boolean_t bRemoveRecursively = true;
					FileIO::removeDirectory(manifestDirectoryPath, bRemoveRecursively);
				}
				catch(runtime_error e)
				{
					string errorMessage = __FILEREF__ + "remove directory failed"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", manifestDirectoryPath: " + manifestDirectoryPath
						+ ", e.what(): " + e.what()
					;
					_logger->error(errorMessage);

					// throw e;
				}
				catch(exception e)
				{
					string errorMessage = __FILEREF__ + "remove directory failed"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", encodingJobKey: " + to_string(encodingJobKey)
						+ ", manifestDirectoryPath: " + manifestDirectoryPath
						+ ", e.what(): " + e.what()
					;
					_logger->error(errorMessage);

					// throw e;
				}
			}
    	}

		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else if (lastPartOfFfmpegOutputFile.find("403 Forbidden") != string::npos)
			throw FFMpegURLForbidden();
		else if (lastPartOfFfmpegOutputFile.find("404 Not Found") != string::npos)
			throw FFMpegURLNotFound();
		else
			throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
		+ ", ingestionJobKey: " + to_string(ingestionJobKey)
		+ ", encodingJobKey: " + to_string(encodingJobKey)
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::liveProxyByCDN(
	int64_t ingestionJobKey,
	int64_t encodingJobKey,
	string liveURL, string userAgent,
	double inputTimeOffset,
	string otherOutputOptions,
	string cdnURL,
	pid_t* pChildPid)
{
	vector<string> ffmpegArgumentList;
	ostringstream ffmpegArgumentListStream;
	int iReturnedStatus = 0;
	string segmentListPath;
	chrono::system_clock::time_point startFfmpegCommand;
	chrono::system_clock::time_point endFfmpegCommand;
	time_t utcNow;

	_currentApiName = "liveProxyByCDN";

	setStatus(
		ingestionJobKey,
		encodingJobKey
		/*
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    try
    {
		_outputFfmpegPathFileName =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey) + "_"
			+ to_string(encodingJobKey)
			+ ".liveProxy.log"
		;

		{
			chrono::system_clock::time_point now = chrono::system_clock::now();
			utcNow = chrono::system_clock::to_time_t(now);
		}

		// ffmpeg <global-options> <input-options> -i <input> <output-options> <output>
		// sample: ffmpeg -re -i http://80.211.238.33/restream/fiera.m3u8 -c copy -bsf:a aac_adtstoasc -vcodec copy -f flv rtmp://1.s.cdn77.eu:1936/static/LS-PRG-43330-22?password=hrpiTIFmsK3R

		string sInputTimeOffset;

		if (inputTimeOffset != 0.0)
		{
			char buffer[64];
			sprintf(buffer, "%.1f", inputTimeOffset);

			sInputTimeOffset = buffer;
		}

		ffmpegArgumentList.push_back("ffmpeg");
		// -re (input) Read input at native frame rate. By default ffmpeg attempts to read the input(s)
		//		as fast as possible. This option will slow down the reading of the input(s)
		//		to the native frame rate of the input(s). It is useful for real-time output
		//		(e.g. live streaming).
		ffmpegArgumentList.push_back("-nostdin");
		if (userAgent != "")
		{
			ffmpegArgumentList.push_back("-user_agent");
			ffmpegArgumentList.push_back(userAgent);
		}
		ffmpegArgumentList.push_back("-re");
		if (sInputTimeOffset != "")
		{
			ffmpegArgumentList.push_back("-itsoffset");

			// ffmpegArgumentList.push_back("-0.5");
			// ffmpegArgumentList.push_back("-2.0");
			ffmpegArgumentList.push_back(sInputTimeOffset);
		}
		ffmpegArgumentList.push_back("-i");
		ffmpegArgumentList.push_back(liveURL);
		addToArguments(otherOutputOptions, ffmpegArgumentList);
		ffmpegArgumentList.push_back("-c:v");
		ffmpegArgumentList.push_back("copy");
		ffmpegArgumentList.push_back("-c:a");
		ffmpegArgumentList.push_back("copy");
		ffmpegArgumentList.push_back("-bsf:a");
		ffmpegArgumentList.push_back("aac_adtstoasc");
		ffmpegArgumentList.push_back("-vcodec");
		ffmpegArgumentList.push_back("copy");

		// right now it is fixed flv, it means cdnURL will be like "rtmp://...."
		ffmpegArgumentList.push_back("-f");
		ffmpegArgumentList.push_back("flv");
		ffmpegArgumentList.push_back(cdnURL);

		if (!ffmpegArgumentList.empty())
			copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
				ostream_iterator<string>(ffmpegArgumentListStream, " "));

		_logger->info(__FILEREF__ + "liveProxy: Executing ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
		);

		startFfmpegCommand = chrono::system_clock::now();

		bool redirectionStdOutput = true;
		bool redirectionStdError = true;

		ProcessUtility::forkAndExec (
			_ffmpegPath + "/ffmpeg",
			ffmpegArgumentList,
			_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
			pChildPid, &iReturnedStatus);
		if (iReturnedStatus != 0)
		{
			string errorMessage = __FILEREF__ + "liveProxy: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", iReturnedStatus: " + to_string(iReturnedStatus)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
           ;            
           _logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
        
		endFfmpegCommand = chrono::system_clock::now();

		_logger->info(__FILEREF__ + "liveProxy: Executed ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
			+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
		);
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
			_outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		string errorMessage;
		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
		else
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else if (lastPartOfFfmpegOutputFile.find("403 Forbidden") != string::npos)
			throw FFMpegURLForbidden();
		else if (lastPartOfFfmpegOutputFile.find("404 Not Found") != string::npos)
			throw FFMpegURLNotFound();
		else
			throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
		+ ", ingestionJobKey: " + to_string(ingestionJobKey)
		+ ", encodingJobKey: " + to_string(encodingJobKey)
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::liveGridByHTTPStreaming(
	int64_t ingestionJobKey,
	int64_t encodingJobKey,
	Json::Value encodingProfileDetailsRoot,
	string userAgent,
	Json::Value inputChannelsRoot,	// name,url
	int gridColumns,
	int gridWidth,	// i.e.: 1024
	int gridHeight, // i.e.: 578

	string outputType,	// HLS or DASH

	// next are parameters for the output
	int segmentDurationInSeconds,
	int playlistEntriesNumber,
	string manifestDirectoryPath,
	string manifestFileName,
	pid_t* pChildPid)
{
	vector<string> ffmpegArgumentList;
	ostringstream ffmpegArgumentListStream;
	int iReturnedStatus = 0;
	string segmentListPath;
	chrono::system_clock::time_point startFfmpegCommand;
	chrono::system_clock::time_point endFfmpegCommand;
	time_t utcNow;

	_currentApiName = "liveGridByHTTPStreaming";

	setStatus(
		ingestionJobKey,
		encodingJobKey
		/*
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    try
    {
		_logger->info(__FILEREF__ + "Received " + _currentApiName
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			// + ", videoTracksRoot.size: " + to_string(videoTracksRoot.size())
			// + ", audioTracksRoot.size: " + to_string(audioTracksRoot.size())
		);

		string outputTypeLowerCase;
		outputTypeLowerCase.resize(outputType.size());
		transform(outputType.begin(), outputType.end(), outputTypeLowerCase.begin(),
				[](unsigned char c){return tolower(c); } );

		if (outputTypeLowerCase != "hls") // && outputTypeLowerCase != "dash")
		{
			string errorMessage = __FILEREF__
				+ "liveProxyByHTTPStreaming. Wrong output type (it has to be HLS or DASH)"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", outputType: " + outputType;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}

		// directory is created by EncoderVideoAudioProxy using MMSStorage::getStagingAssetPathName
		// I saw just once that the directory was not created and the liveencoder remains in the loop
		// where:
		//	1. the encoder returns an error becaise of the missing directory
		//	2. EncoderVideoAudioProxy calls again the encoder
		// So, for this reason, the below check is done
		if (!FileIO::directoryExisting(manifestDirectoryPath))
		{
			_logger->warn(__FILEREF__ + "manifestDirectoryPath does not exist!!! It will be created"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", manifestDirectoryPath: " + manifestDirectoryPath
					);

			_logger->info(__FILEREF__ + "Create directory"
                + ", manifestDirectoryPath: " + manifestDirectoryPath
            );
			bool noErrorIfExists = true;
			bool recursive = true;
			FileIO::createDirectory(manifestDirectoryPath,
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IXGRP |
				S_IROTH | S_IXOTH, noErrorIfExists, recursive);
		}

		_outputFfmpegPathFileName =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey) + "_"
			+ to_string(encodingJobKey)
			+ ".liveGrid.log"
		;

		/*
			option 1 (using overlay/pad)
			ffmpeg \
				-i https://1673829767.rsc.cdn77.org/1673829767/index.m3u8 \
				-i https://1696829226.rsc.cdn77.org/1696829226/index.m3u8 \
				-i https://1681769566.rsc.cdn77.org/1681769566/index.m3u8 \
				-i https://1452709105.rsc.cdn77.org/1452709105/index.m3u8 \
				-filter_complex \
				"[0:v]                 pad=width=$X:height=$Y                  [background]; \
				 [0:v]                 scale=width=$X/2:height=$Y/2            [1]; \
				 [1:v]                 scale=width=$X/2:height=$Y/2            [2]; \
				 [2:v]                 scale=width=$X/2:height=$Y/2            [3]; \
				 [3:v]                 scale=width=$X/2:height=$Y/2            [4]; \
				 [background][1]       overlay=shortest=1:x=0:y=0              [background+1];
				 [background+1][2]     overlay=shortest=1:x=$X/2:y=0           [1+2];
				 [1+2][3]              overlay=shortest=1:x=0:y=$Y/2           [1+2+3];
				 [1+2+3][4]            overlay=shortest=1:x=$X/2:y=$Y/2        [1+2+3+4]
				" -map "[1+2+3+4]" -c:v:0 libx264 \
				-map 0:a -c:a aac \
				-map 1:a -c:a aac \
				-map 2:a -c:a aac \
				-map 3:a -c:a aac \
				-t 30 multiple_input_grid.mp4

			option 2: using hstack/vstack (faster than overlay/pad)
			ffmpeg \
				-i https://1673829767.rsc.cdn77.org/1673829767/index.m3u8 \
				-i https://1696829226.rsc.cdn77.org/1696829226/index.m3u8 \
				-i https://1681769566.rsc.cdn77.org/1681769566/index.m3u8 \
				-i https://1452709105.rsc.cdn77.org/1452709105/index.m3u8 \
				-filter_complex \
				"[0:v]                  scale=width=$X/2:height=$Y/2            [0v]; \
				 [1:v]                  scale=width=$X/2:height=$Y/2            [1v]; \
				 [2:v]                  scale=width=$X/2:height=$Y/2            [2v]; \
				 [3:v]                  scale=width=$X/2:height=$Y/2            [3v]; \
				 [0v][1v]               hstack=inputs=2:shortest=1              [0r]; \	#r sta per row
				 [2v][3v]               hstack=inputs=2:shortest=1              [1r]; \
				 [0r][1r]               vstack=inputs=2:shortest=1              [0r+1r]
				 " -map "[0r+1r]" -codec:v libx264 -b:v 800k -preset veryfast -hls_time 10 -hls_list_size 4 -hls_delete_threshold 1 -hls_flags delete_segments -hls_start_number_source datetime -start_number 10 -hls_segment_filename /var/catramms/storage/MMSRepository-free/1/test/low/test_%04d.ts -f hls /var/catramms/storage/MMSRepository-free/1/test/low/test.m3u8 \
				-map 0:a -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 4 -hls_delete_threshold 1 -hls_flags delete_segments -hls_start_number_source datetime -start_number 10 -hls_segment_filename /var/catramms/storage/MMSRepository-free/1/test/tv1/test_%04d.ts -f hls /var/catramms/storage/MMSRepository-free/1/test/tv1/test.m3u8 \
				-map 1:a -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 4 -hls_delete_threshold 1 -hls_flags delete_segments -hls_start_number_source datetime -start_number 10 -hls_segment_filename /var/catramms/storage/MMSRepository-free/1/test/tv2/test_%04d.ts -f hls /var/catramms/storage/MMSRepository-free/1/test/tv2/test.m3u8 \
				-map 2:a -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 4 -hls_delete_threshold 1 -hls_flags delete_segments -hls_start_number_source datetime -start_number 10 -hls_segment_filename /var/catramms/storage/MMSRepository-free/1/test/tv3/test_%04d.ts -f hls /var/catramms/storage/MMSRepository-free/1/test/tv3/test.m3u8 \
				-map 3:a -acodec aac -b:a 92k -ac 2 -hls_time 10 -hls_list_size 4 -hls_delete_threshold 1 -hls_flags delete_segments -hls_start_number_source datetime -start_number 10 -hls_segment_filename /var/catramms/storage/MMSRepository-free/1/test/tv4/test_%04d.ts -f hls /var/catramms/storage/MMSRepository-free/1/test/tv4/test.m3u8
		 */
		{
			chrono::system_clock::time_point now = chrono::system_clock::now();
			utcNow = chrono::system_clock::to_time_t(now);
		}

		int inputChannelsNumber = inputChannelsRoot.size();

		ffmpegArgumentList.push_back("ffmpeg");
		// -re (input) Read input at native frame rate. By default ffmpeg attempts to read the input(s)
		//		as fast as possible. This option will slow down the reading of the input(s)
		//		to the native frame rate of the input(s). It is useful for real-time output
		//		(e.g. live streaming).
		// -hls_flags append_list: Append new segments into the end of old segment list
		//		and remove the #EXT-X-ENDLIST from the old segment list
		// -hls_time seconds: Set the target segment length in seconds. Segment will be cut on the next key frame
		//		after this time has passed.
		// -hls_list_size size: Set the maximum number of playlist entries. If set to 0 the list file
		//		will contain all the segments. Default value is 5.
		if (userAgent != "")
		{
			ffmpegArgumentList.push_back("-user_agent");
			ffmpegArgumentList.push_back(userAgent);
		}
		ffmpegArgumentList.push_back("-re");
		for (int inputChannelIndex = 0; inputChannelIndex < inputChannelsNumber; inputChannelIndex++)
		{
			Json::Value inputChannelRoot = inputChannelsRoot[inputChannelIndex];
			string inputChannelURL = inputChannelRoot.get("inputChannelURL", "").asString();

			ffmpegArgumentList.push_back("-i");
			ffmpegArgumentList.push_back(inputChannelURL);
		}
		int gridRows = inputChannelsNumber / gridColumns;
		if (inputChannelsNumber % gridColumns != 0)
			gridRows += 1;
		{
			string ffmpegFilterComplex;

			// [0:v]                  scale=width=$X/2:height=$Y/2            [0v];
			int scaleWidth = gridWidth / gridColumns;
			int scaleHeight = gridHeight / gridRows;

			// some codecs, like h264, requires even total width/heigth
			bool evenTotalWidth = true;
			if ((scaleWidth * gridColumns) % 2 != 0)
				evenTotalWidth = false;

			bool evenTotalHeight = true;
			if ((scaleHeight * gridRows) % 2 != 0)
				evenTotalHeight = false;

			for (int inputChannelIndex = 0; inputChannelIndex < inputChannelsNumber; inputChannelIndex++)
			{
				bool lastColumn;
				if ((inputChannelIndex + 1) % gridColumns == 0)
					lastColumn = true;
				else
					lastColumn = false;

				bool lastRow;
				{
					int startChannelIndexOfLastRow = inputChannelsNumber / gridColumns;
					if (inputChannelsNumber % gridColumns == 0)
						startChannelIndexOfLastRow--;
					startChannelIndexOfLastRow *= gridColumns;

					if (inputChannelIndex >= startChannelIndexOfLastRow)
						lastRow = true;
					else
						lastRow = false;
				}

				int width;
				if (!evenTotalWidth && lastColumn)
					width = scaleWidth + 1;
				else
					width = scaleWidth;

				int height;
				if (!evenTotalHeight && lastRow)
					height = scaleHeight + 1;
				else
					height = scaleHeight;

				/*
				_logger->info(__FILEREF__ + "Widthhhhhhh"
					+ ", inputChannelIndex: " + to_string(inputChannelIndex)
					+ ", gridWidth: " + to_string(gridWidth)
					+ ", gridColumns: " + to_string(gridColumns)
					+ ", evenTotalWidth: " + to_string(evenTotalWidth)
					+ ", lastColumn: " + to_string(lastColumn)
					+ ", scaleWidth: " + to_string(scaleWidth)
					+ ", width: " + to_string(width)
				);

				_logger->info(__FILEREF__ + "Heightttttttt"
					+ ", inputChannelIndex: " + to_string(inputChannelIndex)
					+ ", gridHeight: " + to_string(gridHeight)
					+ ", gridRows: " + to_string(gridRows)
					+ ", evenTotalHeight: " + to_string(evenTotalHeight)
					+ ", lastRow: " + to_string(lastRow)
					+ ", scaleHeight: " + to_string(scaleHeight)
					+ ", height: " + to_string(height)
				);
				*/

				ffmpegFilterComplex += (
					"[" + to_string(inputChannelIndex) + ":v]"
					+ "scale=width=" + to_string(width) + ":height=" + to_string(height)
					+ "[" + to_string(inputChannelIndex) + "v];"
					);
			}
			// [0v][1v]               hstack=inputs=2:shortest=1              [0r]; #r sta per row
			for (int gridRowIndex = 0, inputChannelIndex = 0; gridRowIndex < gridRows; gridRowIndex++)
			{
				int columnsIntoTheRow;
				if (gridRowIndex + 1 < gridRows)
				{
					// it is not the last row --> we have all the columns
					columnsIntoTheRow = gridColumns;
				}
				else
				{
					if (inputChannelsNumber % gridColumns != 0)
						columnsIntoTheRow = inputChannelsNumber % gridColumns;
					else
						columnsIntoTheRow = gridColumns;
				}
				for(int gridColumnIndex = 0; gridColumnIndex < columnsIntoTheRow; gridColumnIndex++)
					ffmpegFilterComplex += ("[" + to_string(inputChannelIndex++) + "v]");

				ffmpegFilterComplex += (
					"hstack=inputs=" + to_string(columnsIntoTheRow) + ":shortest=1"
					);

				if (gridRows == 1 && gridRowIndex == 0)
				{
					// in case there is just one row, vstack has NOT to be added 
					ffmpegFilterComplex += (
						"[outVideo]"
					);
				}
				else
				{
					ffmpegFilterComplex += (
						"[" + to_string(gridRowIndex) + "r];"
					);
				}
			}

			if (gridRows > 1)
			{
				// [0r][1r]               vstack=inputs=2:shortest=1              [outVideo]
				for (int gridRowIndex = 0, inputChannelIndex = 0; gridRowIndex < gridRows; gridRowIndex++)
					ffmpegFilterComplex += ("[" + to_string(gridRowIndex) + "r]");
				ffmpegFilterComplex += (
					"vstack=inputs=" + to_string(gridRows) + ":shortest=1[outVideo]"
				);
			}

			ffmpegArgumentList.push_back("-filter_complex");
			ffmpegArgumentList.push_back(ffmpegFilterComplex);
		}

		/*
		string manifestFileName = to_string(ingestionJobKey)
			+ "_" + to_string(encodingJobKey)
			+ ".m3u8";
		*/

		int videoBitRateInKbps = -1;
		{
			string httpStreamingFileFormat;    
			string ffmpegHttpStreamingParameter = "";

			string ffmpegFileFormatParameter = "";

			string ffmpegVideoCodecParameter = "";
			string ffmpegVideoProfileParameter = "";
			string ffmpegVideoResolutionParameter = "";
			string ffmpegVideoBitRateParameter = "";
			string ffmpegVideoOtherParameters = "";
			string ffmpegVideoMaxRateParameter = "";
			string ffmpegVideoBufSizeParameter = "";
			string ffmpegVideoFrameRateParameter = "";
			string ffmpegVideoKeyFramesRateParameter = "";

			string ffmpegAudioCodecParameter = "";
			string ffmpegAudioBitRateParameter = "";
			string ffmpegAudioOtherParameters = "";
			string ffmpegAudioChannelsParameter = "";
			string ffmpegAudioSampleRateParameter = "";


			_currentlyAtSecondPass = false;

			// we will set by default _twoPasses to false otherwise, since the ffmpeg class is reused
			// it could remain set to true from a previous call
			_twoPasses = false;

			settingFfmpegParameters(
				encodingProfileDetailsRoot,
				true,	// isVideo,

				httpStreamingFileFormat,
				ffmpegHttpStreamingParameter,

				ffmpegFileFormatParameter,

				ffmpegVideoCodecParameter,
				ffmpegVideoProfileParameter,
				ffmpegVideoResolutionParameter,
				videoBitRateInKbps,
				ffmpegVideoBitRateParameter,
				ffmpegVideoOtherParameters,
				_twoPasses,
				ffmpegVideoMaxRateParameter,
				ffmpegVideoBufSizeParameter,
				ffmpegVideoFrameRateParameter,
				ffmpegVideoKeyFramesRateParameter,

				ffmpegAudioCodecParameter,
				ffmpegAudioBitRateParameter,
				ffmpegAudioOtherParameters,
				ffmpegAudioChannelsParameter,
				ffmpegAudioSampleRateParameter
			);

			// -map for video and audio
			{
				ffmpegArgumentList.push_back("-map");
				ffmpegArgumentList.push_back("[outVideo]");

				addToArguments(ffmpegVideoCodecParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoProfileParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBitRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoOtherParameters, ffmpegArgumentList);
				addToArguments(ffmpegVideoMaxRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoBufSizeParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoFrameRateParameter, ffmpegArgumentList);
				addToArguments(ffmpegVideoKeyFramesRateParameter, ffmpegArgumentList);
				// addToArguments(ffmpegVideoResolutionParameter, ffmpegArgumentList);
				ffmpegArgumentList.push_back("-threads");
				ffmpegArgumentList.push_back("0");

				if (outputTypeLowerCase == "hls")
				{
					ffmpegArgumentList.push_back("-hls_time");
					ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));

					ffmpegArgumentList.push_back("-hls_list_size");
					ffmpegArgumentList.push_back(to_string(playlistEntriesNumber));

					ffmpegArgumentList.push_back("-hls_delete_threshold");
					ffmpegArgumentList.push_back(to_string(1));

					ffmpegArgumentList.push_back("-hls_flags");
					ffmpegArgumentList.push_back("delete_segments");

					// 2020-07-11: without -hls_start_number_source we have video-audio out of sync
					ffmpegArgumentList.push_back("-hls_start_number_source");
					ffmpegArgumentList.push_back("datetime");

					ffmpegArgumentList.push_back("-start_number");
					ffmpegArgumentList.push_back(to_string(10));

					{
						string videoTrackDirectoryName = "0_video";

						string segmentPathFileName =
							manifestDirectoryPath 
							+ "/"
							+ videoTrackDirectoryName
							+ "/"
							+ to_string(ingestionJobKey)
							+ "_"
							+ to_string(encodingJobKey)
							+ "_%04d.ts"
						;
						ffmpegArgumentList.push_back("-hls_segment_filename");
						ffmpegArgumentList.push_back(segmentPathFileName);

						ffmpegArgumentList.push_back("-f");
						ffmpegArgumentList.push_back("hls");

						string manifestFilePathName =
							manifestDirectoryPath 
							+ "/"
							+ videoTrackDirectoryName
							+ "/"
							+ manifestFileName
						;
						ffmpegArgumentList.push_back(manifestFilePathName);
					}
				}
				else if (outputTypeLowerCase == "dash")
				{
					/*
					 * non so come si deve gestire nel caso di multi audio con DASH
					*/
				}

				for (int inputChannelIndex = 0; inputChannelIndex < inputChannelsNumber; inputChannelIndex++)
				{
					ffmpegArgumentList.push_back("-map");
					ffmpegArgumentList.push_back(
						to_string(inputChannelIndex) + ":a");

					addToArguments(ffmpegAudioCodecParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioBitRateParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioOtherParameters, ffmpegArgumentList);
					addToArguments(ffmpegAudioChannelsParameter, ffmpegArgumentList);
					addToArguments(ffmpegAudioSampleRateParameter, ffmpegArgumentList);

					if (outputTypeLowerCase == "hls")
					{
						{
							ffmpegArgumentList.push_back("-hls_time");
							ffmpegArgumentList.push_back(to_string(segmentDurationInSeconds));

							ffmpegArgumentList.push_back("-hls_list_size");
							ffmpegArgumentList.push_back(to_string(playlistEntriesNumber));

							ffmpegArgumentList.push_back("-hls_delete_threshold");
							ffmpegArgumentList.push_back(to_string(1));

							ffmpegArgumentList.push_back("-hls_flags");
							ffmpegArgumentList.push_back("delete_segments");

							// 2020-07-11: without -hls_start_number_source we have video-audio out of sync
							ffmpegArgumentList.push_back("-hls_start_number_source");
							ffmpegArgumentList.push_back("datetime");

							ffmpegArgumentList.push_back("-start_number");
							ffmpegArgumentList.push_back(to_string(10));
						}

						string audioTrackDirectoryName = to_string(inputChannelIndex) + "_audio";

						{
							string segmentPathFileName =
								manifestDirectoryPath 
								+ "/"
								+ audioTrackDirectoryName
								+ "/"
								+ to_string(ingestionJobKey)
								+ "_"
								+ to_string(encodingJobKey)
								+ "_%04d.ts"
							;
							ffmpegArgumentList.push_back("-hls_segment_filename");
							ffmpegArgumentList.push_back(segmentPathFileName);

							ffmpegArgumentList.push_back("-f");
							ffmpegArgumentList.push_back("hls");

							string manifestFilePathName =
								manifestDirectoryPath
								+ "/"
								+ audioTrackDirectoryName
								+ "/"
								+ manifestFileName
							;
							ffmpegArgumentList.push_back(manifestFilePathName);
						}
					}
					else if (outputTypeLowerCase == "dash")
					{
						/*
						 * non so come si deve gestire nel caso di multi audio con DASH
						 */
					}
				}
			}
        }

		// We will create:
		//  - one m3u8 for each track (video and audio)
		//  - one main m3u8 having a group for AUDIO
		{
			/*
			Manifest will be like:
			#EXTM3U
			#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="ita",NAME="ita",AUTOSELECT=YES, DEFAULT=YES,URI="ita/8896718_1509416.m3u8"
			#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",LANGUAGE="eng",NAME="eng",AUTOSELECT=YES, DEFAULT=YES,URI="eng/8896718_1509416.m3u8"
			#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=195023,AUDIO="audio"
			0/8896718_1509416.m3u8

			https://developer.apple.com/documentation/http_live_streaming/example_playlists_for_http_live_streaming/adding_alternate_media_to_a_playlist#overview
			https://github.com/videojs/http-streaming/blob/master/docs/multiple-alternative-audio-tracks.md

			*/

			{
				for (int inputChannelIndex = 0; inputChannelIndex < inputChannelsNumber; inputChannelIndex++)
				{
					string audioTrackDirectoryName = to_string(inputChannelIndex) + "_audio";

					string audioPathName = manifestDirectoryPath + "/"
						+ audioTrackDirectoryName;

					bool noErrorIfExists = true;
					bool recursive = true;
					_logger->info(__FILEREF__ + "Creating directory (if needed)"
						+ ", audioPathName: " + audioPathName
					);
					FileIO::createDirectory(audioPathName,
						S_IRUSR | S_IWUSR | S_IXUSR |
						S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, noErrorIfExists, recursive);
				}

				{
					string videoTrackDirectoryName = "0_video";
					string videoPathName = manifestDirectoryPath + "/" + videoTrackDirectoryName;

					bool noErrorIfExists = true;
					bool recursive = true;
					_logger->info(__FILEREF__ + "Creating directory (if needed)"
						+ ", videoPathName: " + videoPathName
					);
					FileIO::createDirectory(videoPathName,
						S_IRUSR | S_IWUSR | S_IXUSR |
						S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, noErrorIfExists, recursive);
				}
			}

			// create main manifest file
			{
				string mainManifestPathName = manifestDirectoryPath + "/"
					+ manifestFileName;

				string mainManifest;

				mainManifest = string("#EXTM3U") + "\n";

				for (int inputChannelIndex = 0; inputChannelIndex < inputChannelsNumber; inputChannelIndex++)
				{
					string audioTrackDirectoryName = to_string(inputChannelIndex) + "_audio";

					Json::Value inputChannelRoot = inputChannelsRoot[inputChannelIndex];
					string inputChannelName = inputChannelRoot.get("inputConfigurationLabel", "").asString();

					string audioManifestLine = "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",LANGUAGE=\""
						+ inputChannelName + "\",NAME=\"" + inputChannelName + "\",AUTOSELECT=YES, DEFAULT=YES,URI=\""
						+ audioTrackDirectoryName + "/" + manifestFileName + "\"";

					mainManifest += (audioManifestLine + "\n");
				}

				string videoManifestLine = "#EXT-X-STREAM-INF:PROGRAM-ID=1";
				if (videoBitRateInKbps != -1)
					videoManifestLine += (",BANDWIDTH=" + to_string(videoBitRateInKbps * 1000));
				videoManifestLine += ",AUDIO=\"audio\"";

				mainManifest += (videoManifestLine + "\n");

				string videoTrackDirectoryName = "0_video";
				mainManifest += (videoTrackDirectoryName + "/" + manifestFileName + "\n");

				ofstream manifestFile(mainManifestPathName);
				manifestFile << mainManifest;
			}
		}


		if (!ffmpegArgumentList.empty())
			copy(ffmpegArgumentList.begin(), ffmpegArgumentList.end(),
				ostream_iterator<string>(ffmpegArgumentListStream, " "));

		_logger->info(__FILEREF__ + "liveGrid: Executing ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
		);

		startFfmpegCommand = chrono::system_clock::now();

		bool redirectionStdOutput = true;
		bool redirectionStdError = true;

		ProcessUtility::forkAndExec (
			_ffmpegPath + "/ffmpeg",
			ffmpegArgumentList,
			_outputFfmpegPathFileName, redirectionStdOutput, redirectionStdError,
			pChildPid, &iReturnedStatus);
		if (iReturnedStatus != 0)
		{
			string errorMessage = __FILEREF__ + "liveGrid: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", iReturnedStatus: " + to_string(iReturnedStatus)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
           ;            
           _logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
        
		endFfmpegCommand = chrono::system_clock::now();

		_logger->info(__FILEREF__ + "liveGrid: Executed ffmpeg command"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
			+ ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
		);
    }
    catch(runtime_error e)
    {
		string lastPartOfFfmpegOutputFile = getLastPartOfFile(
			_outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
		string errorMessage;
		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed because killed by the user"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
		else
			errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
				+ ", ffmpegArgumentList: " + ffmpegArgumentListStream.str()
				+ ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
				+ ", e.what(): " + e.what()
			;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

		if (manifestDirectoryPath != "")
    	{
			try
			{
				_logger->info(__FILEREF__ + "Remove directory"
					+ ", manifestDirectoryPath: " + manifestDirectoryPath);
				Boolean_t bRemoveRecursively = true;
				FileIO::removeDirectory(manifestDirectoryPath, bRemoveRecursively);
			}
			catch(runtime_error e)
			{
				string errorMessage = __FILEREF__ + "ffmpeg: remove directory failed"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", e.what(): " + e.what()
				;
				_logger->error(errorMessage);

				// throw e;
			}
			catch(exception e)
			{
				string errorMessage = __FILEREF__ + "ffmpeg: remove directory failed"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", e.what(): " + e.what()
				;
				_logger->error(errorMessage);

				// throw e;
			}
		}

		if (iReturnedStatus == 9)	// 9 means: SIGKILL
			throw FFMpegEncodingKilledByUser();
		else if (lastPartOfFfmpegOutputFile.find("403 Forbidden") != string::npos)
			throw FFMpegURLForbidden();
		else if (lastPartOfFfmpegOutputFile.find("404 Not Found") != string::npos)
			throw FFMpegURLNotFound();
		else
			throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
		+ ", ingestionJobKey: " + to_string(ingestionJobKey)
		+ ", encodingJobKey: " + to_string(encodingJobKey)
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

// destinationPathName will end with the new file format
void FFMpeg::changeFileFormat(
	int64_t ingestionJobKey,
	int64_t sourceKey,
	string sourcePhysicalPath,
	string destinationPathName)
{
	string ffmpegExecuteCommand;

	_currentApiName = "changeFileFormat";

	setStatus(
		ingestionJobKey
		/*
		encodingJobKey
		videoDurationInMilliSeconds,
		mmsAssetPathName
		stagingEncodedAssetPathName
		*/
	);

    try
    {
		_outputFfmpegPathFileName =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey)
			+ "_" + to_string(sourceKey)
			+ ".changeFileFormat.log"
			;
    
		ffmpegExecuteCommand = 
			_ffmpegPath + "/ffmpeg "
			+ "-i " + sourcePhysicalPath + " "
			+ "-c:v copy "
			+ "-c:a copy "
			+ destinationPathName + " "
			+ "> " + _outputFfmpegPathFileName + " "
			+ "2>&1"
		;

		#ifdef __APPLE__
			ffmpegExecuteCommand.insert(0, string("export DYLD_LIBRARY_PATH=")
					+ getenv("DYLD_LIBRARY_PATH") + "; ");
		#endif

        _logger->info(__FILEREF__ + "changeFileFormat: Executing ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", sourceKey: " + to_string(sourceKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
        );

        chrono::system_clock::time_point startFfmpegCommand = chrono::system_clock::now();

        int executeCommandStatus = ProcessUtility::execute(ffmpegExecuteCommand);
        if (executeCommandStatus != 0)
        {
            string errorMessage = __FILEREF__ + "changeFileFormat: ffmpeg command failed"
                    + ", executeCommandStatus: " + to_string(executeCommandStatus)
                    + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        chrono::system_clock::time_point endFfmpegCommand = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "changeContainer: Executed ffmpeg command"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", sourceKey: " + to_string(sourceKey)
            + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
            + ", @FFMPEG statistics@ - ffmpegCommandDuration (secs): @" + to_string(chrono::duration_cast<chrono::seconds>(endFfmpegCommand - startFfmpegCommand).count()) + "@"
        );
    }
    catch(runtime_error e)
    {
        string lastPartOfFfmpegOutputFile = getLastPartOfFile(
                _outputFfmpegPathFileName, _charsToBeReadFromFfmpegErrorOutput);
        string errorMessage = __FILEREF__ + "ffmpeg: ffmpeg command failed"
                + ", ffmpegExecuteCommand: " + ffmpegExecuteCommand
                + ", lastPartOfFfmpegOutputFile: " + lastPartOfFfmpegOutputFile
                + ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

        _logger->info(__FILEREF__ + "Remove"
            + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
        bool exceptionInCaseOfError = false;
        FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);

        _logger->info(__FILEREF__ + "Remove"
            + ", destinationPathName: " + destinationPathName);
        FileIO::remove(destinationPathName, exceptionInCaseOfError);

        throw e;
    }

    _logger->info(__FILEREF__ + "Remove"
        + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName);
    bool exceptionInCaseOfError = false;
    FileIO::remove(_outputFfmpegPathFileName, exceptionInCaseOfError);    
}

void FFMpeg::removeHavingPrefixFileName(string directoryName, string prefixFileName)
{
    try
    {
        FileIO::DirectoryEntryType_t detDirectoryEntryType;
        shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (directoryName + "/");

        bool scanDirectoryFinished = false;
        while (!scanDirectoryFinished)
        {
            string directoryEntry;
            try
            {
                string directoryEntry = FileIO::readDirectory (directory,
                    &detDirectoryEntryType);

                if (detDirectoryEntryType != FileIO::TOOLS_FILEIO_REGULARFILE)
                    continue;

                if (directoryEntry.size() >= prefixFileName.size() && directoryEntry.compare(0, prefixFileName.size(), prefixFileName) == 0) 
                {
                    bool exceptionInCaseOfError = false;
                    string pathFileName = directoryName + "/" + directoryEntry;
                    _logger->info(__FILEREF__ + "Remove"
                        + ", pathFileName: " + pathFileName);
                    FileIO::remove(pathFileName, exceptionInCaseOfError);
                }
            }
            catch(DirectoryListFinished e)
            {
                scanDirectoryFinished = true;
            }
            catch(runtime_error e)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: listing directory failed"
                       + ", e.what(): " + e.what()
                ;
                _logger->error(errorMessage);

                throw e;
            }
            catch(exception e)
            {
                string errorMessage = __FILEREF__ + "ffmpeg: listing directory failed"
                       + ", e.what(): " + e.what()
                ;
                _logger->error(errorMessage);

                throw e;
            }
        }

        FileIO::closeDirectory (directory);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "removeHavingPrefixFileName failed"
            + ", e.what(): " + e.what()
        );
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "removeHavingPrefixFileName failed");
    }
}

int FFMpeg::getEncodingProgress()
{
    int encodingPercentage = 0;

    try
    {
		if (
				_currentApiName == "liveProxyByHTTPStreaming"
				|| _currentApiName == "liveProxyByCDN"
				|| _currentApiName == "liveGridByHTTPStreaming"
				|| _currentApiName == "liveGridByCDN"
				)
		{
			// it's a live

			return -1;
		}

        if (!FileIO::isFileExisting(_outputFfmpegPathFileName.c_str()))
        {
            _logger->info(__FILEREF__ + "ffmpeg: Encoding status not available"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

        string ffmpegEncodingStatus;
        try
        {
			int lastCharsToBeReadToGetInfo = 10000;
            
            ffmpegEncodingStatus = getLastPartOfFile(_outputFfmpegPathFileName, lastCharsToBeReadToGetInfo);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "ffmpeg: Failure reading the encoding status file"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

        {
            // frame= 2315 fps= 98 q=27.0 q=28.0 size=    6144kB time=00:01:32.35 bitrate= 545.0kbits/s speed=3.93x    
            
            smatch m;   // typedef std:match_result<string>

            regex e("time=([^ ]+)");

            bool match = regex_search(ffmpegEncodingStatus, m, e);

            // m is where the result is saved
            // we will have three results: the entire match, the first submatch, the second submatch
            // giving the following input: <email>user@gmail.com<end>
            // m.prefix(): everything is in front of the matched string (<email> in the previous example)
            // m.suffix(): everything is after the matched string (<end> in the previous example)

            /*
            _logger->info(string("m.size(): ") + to_string(m.size()) + ", ffmpegEncodingStatus: " + ffmpegEncodingStatus);
            for (int n = 0; n < m.size(); n++)
            {
                _logger->info(string("m[") + to_string(n) + "]: str()=" + m[n].str());
            }
            cout << "m.prefix().str(): " << m.prefix().str() << endl;
            cout << "m.suffix().str(): " << m.suffix().str() << endl;
             */

            if (m.size() >= 2)
            {
                string duration = m[1].str();   // 00:01:47.87

                stringstream ss(duration);
                string hours;
                string minutes;
                string seconds;
                string roughMicroSeconds;    // microseconds???
                char delim = ':';

                getline(ss, hours, delim); 
                getline(ss, minutes, delim); 

                delim = '.';
                getline(ss, seconds, delim); 
                getline(ss, roughMicroSeconds, delim); 

                int iHours = atoi(hours.c_str());
                int iMinutes = atoi(minutes.c_str());
                int iSeconds = atoi(seconds.c_str());
                int iRoughMicroSeconds = atoi(roughMicroSeconds.c_str());

                double encodingSeconds = (iHours * 3600) + (iMinutes * 60) + (iSeconds) + (iRoughMicroSeconds / 100);
                double currentTimeInMilliSeconds = (encodingSeconds * 1000) + (_currentlyAtSecondPass ? _currentDurationInMilliSeconds : 0);
                //  encodingSeconds : _encodingItem->videoOrAudioDurationInMilliSeconds = x : 100
                
                encodingPercentage = 100 * currentTimeInMilliSeconds / (_currentDurationInMilliSeconds * (_twoPasses ? 2 : 1));

				if (encodingPercentage > 100 || encodingPercentage < 0)
				{
					_logger->error(__FILEREF__ + "Encoding status too big"
						+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
						+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
						+ ", duration: " + duration
						+ ", encodingSeconds: " + to_string(encodingSeconds)
						+ ", _twoPasses: " + to_string(_twoPasses)
						+ ", _currentlyAtSecondPass: " + to_string(_currentlyAtSecondPass)
						+ ", currentTimeInMilliSeconds: " + to_string(currentTimeInMilliSeconds)
						+ ", _currentDurationInMilliSeconds: " + to_string(_currentDurationInMilliSeconds)
						+ ", encodingPercentage: " + to_string(encodingPercentage)
						+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
						+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
					);

					encodingPercentage		= 0;
				}
				else
				{
					_logger->info(__FILEREF__ + "Encoding status"
						+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
						+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
						+ ", duration: " + duration
						+ ", encodingSeconds: " + to_string(encodingSeconds)
						+ ", _twoPasses: " + to_string(_twoPasses)
						+ ", _currentlyAtSecondPass: " + to_string(_currentlyAtSecondPass)
						+ ", currentTimeInMilliSeconds: " + to_string(currentTimeInMilliSeconds)
						+ ", _currentDurationInMilliSeconds: " + to_string(_currentDurationInMilliSeconds)
						+ ", encodingPercentage: " + to_string(encodingPercentage)
						+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
						+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
					);
				}
            }
        }
    }
    catch(FFMpegEncodingStatusNotAvailable e)
    {
        _logger->info(__FILEREF__ + "ffmpeg: getEncodingProgress failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw FFMpegEncodingStatusNotAvailable();
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: getEncodingProgress failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
        );

        throw e;
    }
    
    return encodingPercentage;
}

bool FFMpeg::nonMonotonousDTSInOutputLog()
{
    try
    {
		if (_currentApiName != "liveProxyByCDN")
		{
			// actually we need this check just for liveProxyByCDN

			return false;
		}

        if (!FileIO::isFileExisting(_outputFfmpegPathFileName.c_str()))
        {
            _logger->warn(__FILEREF__ + "ffmpeg: Encoding status not available"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

        string ffmpegEncodingStatus;
        try
        {
			int lastCharsToBeReadToGetInfo = 10000;
            
            ffmpegEncodingStatus = getLastPartOfFile(_outputFfmpegPathFileName, lastCharsToBeReadToGetInfo);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "ffmpeg: Failure reading the encoding status file"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

		string lowerCaseFfmpegEncodingStatus;
		lowerCaseFfmpegEncodingStatus.resize(ffmpegEncodingStatus.size());
		transform(ffmpegEncodingStatus.begin(), ffmpegEncodingStatus.end(), lowerCaseFfmpegEncodingStatus.begin(), [](unsigned char c){return tolower(c); } );

		// [flv @ 0x562afdc507c0] Non-monotonous DTS in output stream 0:1; previous: 95383372, current: 1163825; changing to 95383372. This may result in incorrect timestamps in the output file.
		if (lowerCaseFfmpegEncodingStatus.find("non-monotonous dts in output stream") != string::npos
				&& lowerCaseFfmpegEncodingStatus.find("incorrect timestamps") != string::npos)
			return true;
		else
			return false;
    }
    catch(FFMpegEncodingStatusNotAvailable e)
    {
        _logger->info(__FILEREF__ + "ffmpeg: nonMonotonousDTSInOutputLog failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw FFMpegEncodingStatusNotAvailable();
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: nonMonotonousDTSInOutputLog failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
        );

        throw e;
    }
}

bool FFMpeg::forbiddenErrorInOutputLog()
{
    try
    {
		if (_currentApiName != "liveProxyByCDN")
		{
			// actually we need this check just for liveProxyByCDN

			return false;
		}

        if (!FileIO::isFileExisting(_outputFfmpegPathFileName.c_str()))
        {
            _logger->warn(__FILEREF__ + "ffmpeg: Encoding status not available"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

        string ffmpegEncodingStatus;
        try
        {
			int lastCharsToBeReadToGetInfo = 10000;
            
            ffmpegEncodingStatus = getLastPartOfFile(_outputFfmpegPathFileName, lastCharsToBeReadToGetInfo);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "ffmpeg: Failure reading the encoding status file"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

		string lowerCaseFfmpegEncodingStatus;
		lowerCaseFfmpegEncodingStatus.resize(ffmpegEncodingStatus.size());
		transform(ffmpegEncodingStatus.begin(), ffmpegEncodingStatus.end(),
			lowerCaseFfmpegEncodingStatus.begin(),
			[](unsigned char c)
			{
				return tolower(c);
			}
		);

		// [https @ 0x555a8e428a00] HTTP error 403 Forbidden
		if (lowerCaseFfmpegEncodingStatus.find("http error 403 forbidden") != string::npos)
			return true;
		else
			return false;
    }
    catch(FFMpegEncodingStatusNotAvailable e)
    {
        _logger->info(__FILEREF__ + "ffmpeg: forbiddenErrorInOutputLog failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw FFMpegEncodingStatusNotAvailable();
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: forbiddenErrorInOutputLog failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
        );

        throw e;
    }
}

bool FFMpeg::isFrameIncreasing(int secondsToWaitBetweenSamples)
{

	bool frameIncreasing = true;

    try
    {
		chrono::system_clock::time_point now = chrono::system_clock::now();
		long minutesSinceBeginningPassed =
			chrono::duration_cast<chrono::minutes>(now - _startFFMpegMethod).count();
		if (minutesSinceBeginningPassed <= _startCheckingFrameInfoInMinutes)
			return frameIncreasing;

        if (!FileIO::isFileExisting(_outputFfmpegPathFileName.c_str()))
        {
            _logger->info(__FILEREF__ + "ffmpeg: Encoding status not available"
                + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
                + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
                + ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
                + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
                + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
				+ ", minutesSinceBeginningPassed: " + to_string(minutesSinceBeginningPassed)
            );

            throw FFMpegEncodingStatusNotAvailable();
        }

		int lastCharsToBeReadToGetInfo = 10000;

		long firstFramesValue;
		{
			string ffmpegEncodingStatus;
			try
			{
				ffmpegEncodingStatus = getLastPartOfFile(_outputFfmpegPathFileName, lastCharsToBeReadToGetInfo);
			}
			catch(exception e)
			{
				_logger->error(__FILEREF__ + "ffmpeg: Failure reading the encoding status file"
					+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
					+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
					+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
					+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
					+ ", minutesSinceBeginningPassed: " + to_string(minutesSinceBeginningPassed)
				);

				throw FFMpegEncodingStatusNotAvailable();
			}

			try
			{
				firstFramesValue = getFrameByOutputLog(ffmpegEncodingStatus);
			}
			catch(FFMpegFrameInfoNotAvailable e)
			{
				frameIncreasing = false;

				_logger->error(__FILEREF__ + "ffmpeg: frame monitoring. Frame info not available (1)"
					+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
					+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
					+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
					+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
					+ ", minutesSinceBeginningPassed: " + to_string(minutesSinceBeginningPassed)
					+ ", frameIncreasing: " + to_string(frameIncreasing)
				);

				return frameIncreasing;
				// throw FFMpegEncodingStatusNotAvailable();
			}
		}

		this_thread::sleep_for(chrono::seconds(secondsToWaitBetweenSamples));

		long secondFramesValue;
		{
			string ffmpegEncodingStatus;
			try
			{
				ffmpegEncodingStatus = getLastPartOfFile(_outputFfmpegPathFileName, lastCharsToBeReadToGetInfo);
			}
			catch(exception e)
			{
				_logger->error(__FILEREF__ + "ffmpeg: Failure reading the encoding status file"
					+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
					+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
					+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
					+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
					+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
					+ ", minutesSinceBeginningPassed: " + to_string(minutesSinceBeginningPassed)
				);

				throw FFMpegEncodingStatusNotAvailable();
			}

			try
			{
				secondFramesValue = getFrameByOutputLog(ffmpegEncodingStatus);
			}
			catch(FFMpegFrameInfoNotAvailable e)
			{
				frameIncreasing = false;

				_logger->error(__FILEREF__ + "ffmpeg: frame monitoring. Frame info not available (2)"
					+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
					+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
					+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
					+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
					+ ", minutesSinceBeginningPassed: " + to_string(minutesSinceBeginningPassed)
					+ ", frameIncreasing: " + to_string(frameIncreasing)
				);

				return frameIncreasing;
				// throw FFMpegEncodingStatusNotAvailable();
			}
		}

		frameIncreasing = (firstFramesValue == secondFramesValue ? false : true);

        _logger->info(__FILEREF__ + "ffmpeg: frame monitoring"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            + ", firstFramesValue: " + to_string(firstFramesValue)
            + ", secondFramesValue: " + to_string(secondFramesValue)
			+ ", minutesSinceBeginningPassed: " + to_string(minutesSinceBeginningPassed)
            + ", frameIncreasing: " + to_string(frameIncreasing)
        );
    }
    catch(FFMpegEncodingStatusNotAvailable e)
    {
        _logger->info(__FILEREF__ + "ffmpeg: isFrameIncreasing failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ffmpeg: isFrameIncreasing failed"
            + ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
            + ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
            + ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
            + ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
        );

        throw e;
    }
    
    return frameIncreasing;
}

long FFMpeg::getFrameByOutputLog(string ffmpegEncodingStatus)
{
	// frame= 2315 fps= 98 q=27.0 q=28.0 size=    6144kB time=00:01:32.35 bitrate= 545.0kbits/s speed=3.93x    

	string frameToSearch = "frame=";
	size_t startFrameIndex = ffmpegEncodingStatus.rfind(frameToSearch);
	if (startFrameIndex == string::npos)
	{
		_logger->warn(__FILEREF__ + "ffmpeg: frame info was not found"
			+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
			+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
			+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
			+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
			+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
			+ ", ffmpegEncodingStatus: " + ffmpegEncodingStatus
		);

		throw FFMpegFrameInfoNotAvailable();
	}
	ffmpegEncodingStatus = ffmpegEncodingStatus.substr(startFrameIndex + frameToSearch.size());
	size_t endFrameIndex = ffmpegEncodingStatus.find(" fps=");
	if (endFrameIndex == string::npos)
	{
		_logger->error(__FILEREF__ + "ffmpeg: fps info was not found"
			+ ", _currentIngestionJobKey: " + to_string(_currentIngestionJobKey)
			+ ", _currentEncodingJobKey: " + to_string(_currentEncodingJobKey)
			+ ", _outputFfmpegPathFileName: " + _outputFfmpegPathFileName
			+ ", _currentMMSSourceAssetPathName: " + _currentMMSSourceAssetPathName
			+ ", _currentStagingEncodedAssetPathName: " + _currentStagingEncodedAssetPathName
			+ ", ffmpegEncodingStatus: " + ffmpegEncodingStatus
		);

		throw FFMpegEncodingStatusNotAvailable();
	}
	ffmpegEncodingStatus = ffmpegEncodingStatus.substr(0, endFrameIndex);
	ffmpegEncodingStatus = StringUtils::trim(ffmpegEncodingStatus);

	return stol(ffmpegEncodingStatus);
}

void FFMpeg::settingFfmpegParameters(
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
)
{
    string field;
	/*
    Json::Value encodingProfileRoot;
    try
    {
        Json::CharReaderBuilder builder;
        Json::CharReader* reader = builder.newCharReader();
        string errors;

        bool parsingSuccessful = reader->parse(encodingProfileDetails.c_str(),
                encodingProfileDetails.c_str() + encodingProfileDetails.size(), 
                &encodingProfileRoot, &errors);
        delete reader;

        if (!parsingSuccessful)
        {
            string errorMessage = __FILEREF__ + "ffmpeg: failed to parse the encoder details"
                    + ", errors: " + errors
                    + ", encodingProfileDetails: " + encodingProfileDetails
                    ;
            _logger->error(errorMessage);
            
            throw runtime_error(errorMessage);
        }
    }
    catch(...)
    {
        throw runtime_error(string("ffmpeg: wrong encoding profile json format")
                + ", encodingProfileDetails: " + encodingProfileDetails
                );
    }
	*/

    // fileFormat
    string fileFormat;
	string fileFormatLowerCase;
    {
		field = "FileFormat";
		if (!isMetadataPresent(encodingProfileDetailsRoot, field))
		{
			string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
				+ ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        fileFormat = encodingProfileDetailsRoot.get(field, "XXX").asString();
		fileFormatLowerCase.resize(fileFormat.size());
		transform(fileFormat.begin(), fileFormat.end(), fileFormatLowerCase.begin(),
			[](unsigned char c){return tolower(c); } );

        FFMpeg::encodingFileFormatValidation(fileFormat, _logger);

        if (fileFormatLowerCase == "hls")
        {
			httpStreamingFileFormat = "hls";

            ffmpegFileFormatParameter = 
				+ "-f hls "
			;

			long segmentDurationInSeconds = 10;

			field = "HLS";
			if (isMetadataPresent(encodingProfileDetailsRoot, field))
			{
				Json::Value hlsRoot = encodingProfileDetailsRoot[field]; 

				field = "SegmentDuration";
				if (isMetadataPresent(hlsRoot, field))
					segmentDurationInSeconds = asInt(hlsRoot, field, 10);
			}

            ffmpegHttpStreamingParameter = 
				"-hls_time " + to_string(segmentDurationInSeconds) + " ";

			// hls_list_size: set the maximum number of playlist entries. If set to 0 the list file
			//	will contain all the segments. Default value is 5.
            ffmpegHttpStreamingParameter += "-hls_list_size 0 ";
		}
		else if (fileFormatLowerCase == "dash")
        {
			httpStreamingFileFormat = "dash";

            ffmpegFileFormatParameter = 
				+ "-f dash "
			;

			long segmentDurationInSeconds = 10;

			field = "DASH";
			if (isMetadataPresent(encodingProfileDetailsRoot, field))
			{
				Json::Value dashRoot = encodingProfileDetailsRoot[field]; 

				field = "SegmentDuration";
				if (isMetadataPresent(dashRoot, field))
					segmentDurationInSeconds = asInt(dashRoot, field, 10);
			}

            ffmpegHttpStreamingParameter =
				"-seg_duration " + to_string(segmentDurationInSeconds) + " ";

			// hls_list_size: set the maximum number of playlist entries. If set to 0 the list file
			//	will contain all the segments. Default value is 5.
            // ffmpegHttpStreamingParameter += "-hls_list_size 0 ";

			// it is important to specify -init_seg_name because those files
			// will not be removed in EncoderVideoAudioProxy.cpp
            ffmpegHttpStreamingParameter +=
				"-init_seg_name init-stream$RepresentationID$.$ext$ ";

			// the only difference with the ffmpeg default is that default is $Number%05d$
			// We had to change it to $Number%01d$ because otherwise the generated file containing
			// 00001 00002 ... but the videojs player generates file name like 1 2 ...
			// and the streaming was not working
            ffmpegHttpStreamingParameter +=
				"-media_seg_name chunk-stream$RepresentationID$-$Number%01d$.$ext$ ";
		}
        else
        {
            httpStreamingFileFormat = "";

			if (fileFormatLowerCase == "ts")
			{
				// if "-f ts filename.ts" is added the following error happens:
				//		...Requested output format 'ts' is not a suitable output format
				// Without "-f ts", just filename.ts works fine
				ffmpegFileFormatParameter = "";
			}
			else
			{
				ffmpegFileFormatParameter =
					" -f " + fileFormatLowerCase + " "
				;
			}
        }
    }

    if (isVideo)
    {
        field = "Video";
        if (!isMetadataPresent(encodingProfileDetailsRoot, field))
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        Json::Value videoRoot = encodingProfileDetailsRoot[field]; 

        // codec
        string codec;
        {
            field = "Codec";
            if (!isMetadataPresent(videoRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }

            codec = videoRoot.get(field, "XXX").asString();

			// 2020-03-27: commented just to avoid to add the check every time a new codec is added
			//		In case the codec is wrong, ffmpeg will generate the error later
            // FFMpeg::encodingVideoCodecValidation(codec, _logger);

            ffmpegVideoCodecParameter   =
                    "-codec:v " + codec + " "
            ;
        }

        // profile
        {
            field = "Profile";
            if (isMetadataPresent(videoRoot, field))
            {
                string profile = videoRoot.get(field, "").asString();

                if (codec == "libx264" || codec == "libvpx")
				{
					FFMpeg::encodingVideoProfileValidation(codec, profile, _logger);
					if (codec == "libx264")
					{
						ffmpegVideoProfileParameter =
                            "-profile:v " + profile + " "
						;
					}
					else if (codec == "libvpx")
					{
						ffmpegVideoProfileParameter =
                            "-quality " + profile + " "
						;
					}
				}
                else if (profile != "")
                {
					ffmpegVideoProfileParameter =
						"-profile:v " + profile + " "
					;
					/*
                    string errorMessage = __FILEREF__ + "ffmpeg: codec is wrong"
                            + ", codec: " + codec;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
					*/
                }
            }
        }

        // resolution
        {
            field = "Width";
            if (!isMetadataPresent(videoRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            int width = asInt(videoRoot, field, 0);
            if (width == -1 && codec == "libx264")
                width   = -2;     // h264 requires always a even width/height
        
            field = "Height";
            if (!isMetadataPresent(videoRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            int height = asInt(videoRoot, field, 0);
            if (height == -1 && codec == "libx264")
                height   = -2;     // h264 requires always a even width/height

            ffmpegVideoResolutionParameter =
                    "-vf scale=" + to_string(width) + ":" + to_string(height) + " "
            ;
        }

        // bitRate
		videoBitRateInKbps = -1;
        {
            field = "KBitRate";
            if (isMetadataPresent(videoRoot, field))
            {
                videoBitRateInKbps = asInt(videoRoot, field, 0);

                ffmpegVideoBitRateParameter =
                        "-b:v " + to_string(videoBitRateInKbps) + "k "
                ;
            }
        }

        // OtherOutputParameters
        {
            field = "OtherOutputParameters";
            if (isMetadataPresent(videoRoot, field))
            {
                string otherOutputParameters = videoRoot.get(field, "XXX").asString();

                ffmpegVideoOtherParameters =
                        otherOutputParameters + " "
                ;
            }
        }

        // twoPasses
        {
            field = "TwoPasses";
            if (!isMetadataPresent(videoRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
			twoPasses = asBool(videoRoot, field, false);
        }

        // maxRate
        {
            field = "MaxRate";
            if (isMetadataPresent(videoRoot, field))
            {
                int maxRate = asInt(videoRoot, field, 0);

                ffmpegVideoMaxRateParameter =
                        "-maxrate " + to_string(maxRate) + "k "
                ;
            }
        }

        // bufSize
        {
            field = "BufSize";
            if (isMetadataPresent(videoRoot, field))
            {
                int bufSize = asInt(videoRoot, field, 0);

                ffmpegVideoBufSizeParameter =
                        "-bufsize " + to_string(bufSize) + "k "
                ;
            }
        }

        // frameRate
        {
            field = "FrameRate";
            if (isMetadataPresent(videoRoot, field))
            {
                int frameRate = asInt(videoRoot, field, 0);

				if (frameRate != 0)
				{
					ffmpegVideoFrameRateParameter =
                        "-r " + to_string(frameRate) + " "
					;

					// keyFrameIntervalInSeconds
					{
						field = "KeyFrameIntervalInSeconds";
						if (isMetadataPresent(videoRoot, field))
						{
							int keyFrameIntervalInSeconds = asInt(videoRoot, field, 0);

							// -g specifies the number of frames in a GOP
							ffmpegVideoKeyFramesRateParameter =
                                "-g " + to_string(frameRate * keyFrameIntervalInSeconds) + " "
							;
						}
					}
				}
            }
        }
    }
    
    // if (contentType == "video" || contentType == "audio")
    {
        field = "Audio";
        if (!isMetadataPresent(encodingProfileDetailsRoot, field))
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        Json::Value audioRoot = encodingProfileDetailsRoot[field]; 

        // codec
        {
            field = "Codec";
            if (!isMetadataPresent(audioRoot, field))
            {
                string errorMessage = __FILEREF__ + "ffmpeg: Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string codec = audioRoot.get(field, "XXX").asString();

            FFMpeg::encodingAudioCodecValidation(codec, _logger);

            ffmpegAudioCodecParameter   =
                    "-acodec " + codec + " "
            ;
        }

        // kBitRate
        {
            field = "KBitRate";
            if (isMetadataPresent(audioRoot, field))
            {
                int bitRate = asInt(audioRoot, field, 0);

                ffmpegAudioBitRateParameter =
                        "-b:a " + to_string(bitRate) + "k "
                ;
            }
        }
        
        // OtherOutputParameters
        {
            field = "OtherOutputParameters";
            if (isMetadataPresent(audioRoot, field))
            {
                string otherOutputParameters = audioRoot.get(field, "XXX").asString();

                ffmpegAudioOtherParameters =
                        otherOutputParameters + " "
                ;
            }
        }

        // channelsNumber
        {
            field = "ChannelsNumber";
            if (isMetadataPresent(audioRoot, field))
            {
                int channelsNumber = asInt(audioRoot, field, 0);

                ffmpegAudioChannelsParameter =
                        "-ac " + to_string(channelsNumber) + " "
                ;
            }
        }

        // sample rate
        {
            field = "SampleRate";
            if (isMetadataPresent(audioRoot, field))
            {
                int sampleRate = asInt(audioRoot, field, 0);

                ffmpegAudioSampleRateParameter =
                        "-ar " + to_string(sampleRate) + " "
                ;
            }
        }
    }
}

void FFMpeg::addToArguments(string parameter, vector<string>& argumentList)
{
	_logger->info(__FILEREF__ + "addToArguments"
			+ ", parameter: " + parameter
	);

	if (parameter != "")
	{
		string item;
		stringstream parameterStream(parameter);

		while(getline(parameterStream, item, ' '))
		{
			if (item != "")
				argumentList.push_back(item);
		}
	}
}

string FFMpeg::getLastPartOfFile(
    string pathFileName, int lastCharsToBeRead)
{
    string lastPartOfFile = "";
    char* buffer = nullptr;

    auto logger = spdlog::get("mmsEngineService");

    try
    {
        ifstream ifPathFileName(pathFileName);
        if (ifPathFileName) 
        {
            int         charsToBeRead;
            
            // get length of file:
            ifPathFileName.seekg (0, ifPathFileName.end);
            int fileSize = ifPathFileName.tellg();
            if (fileSize >= lastCharsToBeRead)
            {
                ifPathFileName.seekg (fileSize - lastCharsToBeRead, ifPathFileName.beg);
                charsToBeRead = lastCharsToBeRead;
            }
            else
            {
                ifPathFileName.seekg (0, ifPathFileName.beg);
                charsToBeRead = fileSize;
            }

            buffer = new char [charsToBeRead];
            ifPathFileName.read (buffer, charsToBeRead);
            if (ifPathFileName)
            {
                // all characters read successfully
                lastPartOfFile.assign(buffer, charsToBeRead);                
            }
            else
            {
                // error: only is.gcount() could be read";
                lastPartOfFile.assign(buffer, ifPathFileName.gcount());                
            }
            ifPathFileName.close();

            delete[] buffer;
        }
    }
    catch(exception e)
    {
        if (buffer != nullptr)
            delete [] buffer;

        logger->error("getLastPartOfFile failed");        
    }

    return lastPartOfFile;
}

pair<string, string> FFMpeg::retrieveStreamingYouTubeURL(
	int64_t ingestionJobKey, int64_t encodingJobKey,
	string youTubeURL)
{
	_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL"
		+ ", youTubeURL: " + youTubeURL
		+ ", ingestionJobKey: " + to_string(ingestionJobKey)
		+ ", encodingJobKey: " + to_string(encodingJobKey)
	);

	string detailsYouTubeProfilesPath;
	{
		detailsYouTubeProfilesPath =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey)
			+ "_"
			+ to_string(encodingJobKey)
			+ "-youTubeProfiles.txt";
    
		string youTubeExecuteCommand =
			_youTubeDlPath + "/youtube-dl "
			+ "--list-formats "
			+ youTubeURL + " "
			+ " > " + detailsYouTubeProfilesPath
			+ " 2>&1"
		;

		try
		{
			_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL: Executing youtube command"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
			);

			chrono::system_clock::time_point startYouTubeCommand = chrono::system_clock::now();

			int executeCommandStatus = ProcessUtility::execute(youTubeExecuteCommand);
			if (executeCommandStatus != 0)
			{
				// it could be also that the live is not available
				// ERROR: f2vW_XyTW4o: YouTube said: This live stream recording is not available.

				string errorMessage = __FILEREF__
					+ "retrieveStreamingYouTubeURL: youTube command failed"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", executeCommandStatus: " + to_string(executeCommandStatus)
					+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			else if (!FileIO::fileExisting(detailsYouTubeProfilesPath))
			{
				string errorMessage = __FILEREF__
					+ "retrieveStreamingYouTubeURL: youTube command failed. no profiles file created"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", executeCommandStatus: " + to_string(executeCommandStatus)
					+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			chrono::system_clock::time_point endYouTubeCommand = chrono::system_clock::now();

			_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL: Executed youTube command"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
				+ ", @FFMPEG statistics@ - duration (secs): @"
					+ to_string(chrono::duration_cast<chrono::seconds>(endYouTubeCommand - startYouTubeCommand).count()) + "@"
			);
		}
		catch(runtime_error e)
		{
			string errorMessage = __FILEREF__ + "retrieveStreamingYouTubeURL, youTube command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", e.what(): " + e.what()
			;
			_logger->error(errorMessage);

			if (FileIO::fileExisting(detailsYouTubeProfilesPath))
			{
				_logger->info(__FILEREF__ + "Remove"
					+ ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath);
				bool exceptionInCaseOfError = false;
				FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
			}

			throw e;
		}
		catch(exception e)
		{
			string errorMessage = __FILEREF__ + "retrieveStreamingYouTubeURL, youTube command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", e.what(): " + e.what()
			;
			_logger->error(errorMessage);

			if (FileIO::fileExisting(detailsYouTubeProfilesPath))
			{
				_logger->info(__FILEREF__ + "Remove"
				+ ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath);
				bool exceptionInCaseOfError = false;
				FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
			}

			throw e;
		}
	}

	int selectedFormatCode = -1;
	string extension;
    try
    {
        // txt output will be like:
        /*
[youtube] f2vW_XyTW4o: Downloading webpage
[youtube] f2vW_XyTW4o: Downloading m3u8 information
[youtube] f2vW_XyTW4o: Downloading MPD manifest
[info] Available formats for f2vW_XyTW4o:
format code  extension  resolution note
91           mp4        256x144    HLS  197k , avc1.42c00b, 30.0fps, mp4a.40.5@ 48k
92           mp4        426x240    HLS  338k , avc1.4d4015, 30.0fps, mp4a.40.5@ 48k
93           mp4        640x360    HLS  829k , avc1.4d401e, 30.0fps, mp4a.40.2@128k
94           mp4        854x480    HLS 1380k , avc1.4d401f, 30.0fps, mp4a.40.2@128k
95           mp4        1280x720   HLS 2593k , avc1.4d401f, 30.0fps, mp4a.40.2@256k (best)
        */

        ifstream detailsFile(detailsYouTubeProfilesPath);
		string line;
		bool formatCodeLabelFound = false;
		int lastFormatCode = -1;
		int bestFormatCode = -1;
		while(getline(detailsFile, line))
		{
			_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL, Details youTube profiles"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath
				+ ", formatCodeLabelFound: " + to_string(formatCodeLabelFound)
				+ ", lastFormatCode: " + to_string(lastFormatCode)
				+ ", bestFormatCode: " + to_string(bestFormatCode)
				+ ", line: " + line
			);

			if (formatCodeLabelFound)
			{
				int lastDigit = 0;
				while(lastDigit < line.length() && isdigit(line[lastDigit]))
					lastDigit++;
				if (lastDigit > 0)
				{
					string formatCode = line.substr(0, lastDigit);
					lastFormatCode = stoi(formatCode);

					if (line.find("(best)") != string::npos)
						bestFormatCode = lastFormatCode;

					int startExtensionIndex = lastDigit;
					while(startExtensionIndex < line.length()
						&& isspace(line[startExtensionIndex]))
						startExtensionIndex++;
					int endExtensionIndex = startExtensionIndex;
					while(endExtensionIndex < line.length()
						&& !isspace(line[endExtensionIndex]))
						endExtensionIndex++;

					extension = line.substr(startExtensionIndex,
						endExtensionIndex - startExtensionIndex);
				}
			}
			else if (line.find("format code") != string::npos)
				formatCodeLabelFound = true;
		}

		_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL, Details youTube profiles, final info"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath
			+ ", formatCodeLabelFound: " + to_string(formatCodeLabelFound)
			+ ", lastFormatCode: " + to_string(lastFormatCode)
			+ ", bestFormatCode: " + to_string(bestFormatCode)
			+ ", line: " + line
		);

		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath);
			bool exceptionInCaseOfError = false;
			FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
		}

		if (bestFormatCode != -1)
			selectedFormatCode = bestFormatCode;
		else if (lastFormatCode != -1)
			selectedFormatCode = lastFormatCode;
		else
		{
			string errorMessage = __FILEREF__
				+ "retrieveStreamingYouTubeURL: no format code found"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
			;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
	}
    catch(runtime_error e)
    {
        string errorMessage = __FILEREF__ + "retrieveStreamingYouTubeURL: profile error processing or format code not found"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

		if (FileIO::fileExisting(detailsYouTubeProfilesPath))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath);
			bool exceptionInCaseOfError = false;
			FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
		}

        throw e;
    }
    catch(exception e)
    {
        string errorMessage = __FILEREF__ + "retrieveStreamingYouTubeURL: profiles error processing"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", encodingJobKey: " + to_string(encodingJobKey)
			+ ", e.what(): " + e.what()
        ;
        _logger->error(errorMessage);

		if (FileIO::fileExisting(detailsYouTubeProfilesPath))
		{
			_logger->info(__FILEREF__ + "Remove"
            + ", detailsYouTubeProfilesPath: " + detailsYouTubeProfilesPath);
			bool exceptionInCaseOfError = false;
			FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
		}

        throw e;
	}

	string streamingYouTubeURL;
	{
		string detailsYouTubeURLPath =
			_ffmpegTempDir + "/"
			+ to_string(ingestionJobKey)
			+ "_"
			+ to_string(encodingJobKey)
			+ "-youTubeUrl.txt";

		string youTubeExecuteCommand =
			_youTubeDlPath + "/youtube-dl "
			+ "-f " + to_string(selectedFormatCode) + " "
			+ "-g " + youTubeURL + " "
			+ " > " + detailsYouTubeURLPath
			+ " 2>&1"
		;

		try
		{
			_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL: Executing youtube command"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
			);

			chrono::system_clock::time_point startYouTubeCommand = chrono::system_clock::now();

			int executeCommandStatus = ProcessUtility::execute(youTubeExecuteCommand);
			if (executeCommandStatus != 0)
			{
				string errorMessage = __FILEREF__
					+ "retrieveStreamingYouTubeURL: youTube command failed"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", executeCommandStatus: " + to_string(executeCommandStatus)
					+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			else if (!FileIO::fileExisting(detailsYouTubeURLPath))
			{
				string errorMessage = __FILEREF__
					+ "retrieveStreamingYouTubeURL: youTube command failed. no URL file created"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", executeCommandStatus: " + to_string(executeCommandStatus)
					+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			chrono::system_clock::time_point endYouTubeCommand = chrono::system_clock::now();

			_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL: Executed youTube command"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
				+ ", @FFMPEG statistics@ - duration (secs): @"
					+ to_string(chrono::duration_cast<chrono::seconds>(endYouTubeCommand - startYouTubeCommand).count()) + "@"
			);

			{
				ifstream urlFile(detailsYouTubeURLPath);
				std::stringstream buffer;
				buffer << urlFile.rdbuf();

				streamingYouTubeURL = buffer.str();
				streamingYouTubeURL = StringUtils::trimNewLineToo(streamingYouTubeURL);

				_logger->info(__FILEREF__ + "retrieveStreamingYouTubeURL: Executed youTube command"
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", encodingJobKey: " + to_string(encodingJobKey)
					+ ", youTubeExecuteCommand: " + youTubeExecuteCommand
					+ ", streamingYouTubeURL: " + streamingYouTubeURL
				);
			}

			{
				_logger->info(__FILEREF__ + "Remove"
				+ ", detailsYouTubeURLPath: " + detailsYouTubeURLPath);
				bool exceptionInCaseOfError = false;
				FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
			}
		}
		catch(runtime_error e)
		{
			string errorMessage = __FILEREF__ + "retrieveStreamingYouTubeURL, youTube command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", e.what(): " + e.what()
			;
			_logger->error(errorMessage);

			if (FileIO::fileExisting(detailsYouTubeURLPath))
			{
				_logger->info(__FILEREF__ + "Remove"
					+ ", detailsYouTubeURLPath: " + detailsYouTubeURLPath);
				bool exceptionInCaseOfError = false;
				FileIO::remove(detailsYouTubeURLPath, exceptionInCaseOfError);
			}

			throw e;
		}
		catch(exception e)
		{
			string errorMessage = __FILEREF__ + "retrieveStreamingYouTubeURL, youTube command failed"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingJobKey: " + to_string(encodingJobKey)
				+ ", e.what(): " + e.what()
			;
			_logger->error(errorMessage);

			if (FileIO::fileExisting(detailsYouTubeURLPath))
			{
				_logger->info(__FILEREF__ + "Remove"
					+ ", detailsYouTubeURLPath: " + detailsYouTubeURLPath);
				bool exceptionInCaseOfError = false;
				FileIO::remove(detailsYouTubeProfilesPath, exceptionInCaseOfError);
			}

			throw e;
		}
	}

	return make_pair(streamingYouTubeURL, extension);
}

void FFMpeg::encodingFileFormatValidation(string fileFormat,
        shared_ptr<spdlog::logger> logger)
{
	string fileFormatLowerCase;
	fileFormatLowerCase.resize(fileFormat.size());
	transform(fileFormat.begin(), fileFormat.end(), fileFormatLowerCase.begin(),
		[](unsigned char c){return tolower(c); } );

    if (fileFormatLowerCase != "3gp" 
		&& fileFormatLowerCase != "mp4" 
		&& fileFormatLowerCase != "mov"
		&& fileFormatLowerCase != "webm" 
		&& fileFormatLowerCase != "hls"
		&& fileFormatLowerCase != "dash"
		&& fileFormatLowerCase != "ts"
		&& fileFormatLowerCase != "mkv"
	)
    {
        string errorMessage = __FILEREF__ + "ffmpeg: fileFormat is wrong"
                + ", fileFormatLowerCase: " + fileFormatLowerCase;

        logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
}

void FFMpeg::encodingVideoCodecValidation(string codec,
        shared_ptr<spdlog::logger> logger)
{    
    if (codec != "libx264" 
            && codec != "libvpx")
    {
        string errorMessage = __FILEREF__ + "ffmpeg: Video codec is wrong"
                + ", codec: " + codec;

        logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
}

void FFMpeg::encodingVideoProfileValidation(
        string codec, string profile,
        shared_ptr<spdlog::logger> logger)
{
    if (codec == "libx264")
    {
        if (profile != "high" && profile != "baseline" && profile != "main"
				&& profile != "high422"	// used in case of mxf
			)
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Profile is wrong"
                    + ", codec: " + codec
                    + ", profile: " + profile;

            logger->error(errorMessage);
        
            throw runtime_error(errorMessage);
        }
    }
    else if (codec == "libvpx")
    {
        if (profile != "best" && profile != "good")
        {
            string errorMessage = __FILEREF__ + "ffmpeg: Profile is wrong"
                    + ", codec: " + codec
                    + ", profile: " + profile;

            logger->error(errorMessage);
        
            throw runtime_error(errorMessage);
        }
    }
    else
    {
        string errorMessage = __FILEREF__ + "ffmpeg: codec is wrong"
                + ", codec: " + codec;

        logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
}

void FFMpeg::encodingAudioCodecValidation(string codec,
        shared_ptr<spdlog::logger> logger)
{    
    if (codec != "aac" 
            && codec != "libfdk_aac" 
            && codec != "libvo_aacenc" 
            && codec != "libvorbis"
    )
    {
        string errorMessage = __FILEREF__ + "ffmpeg: Audio codec is wrong"
                + ", codec: " + codec;

        logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
}

bool FFMpeg::isMetadataPresent(Json::Value root, string field)
{
    if (root.isObject() && root.isMember(field) && !root[field].isNull()
)
        return true;
    else
        return false;
}

int FFMpeg::asInt(Json::Value root, string field, int defaultValue)
{
	if (field == "")
	{
		if (root.type() == Json::stringValue)
			return strtol(root.asString().c_str(), nullptr, 10);
		else
			return root.asInt();
	}
	else
	{
		if (root.get(field, defaultValue).type() == Json::stringValue)
			return strtol(root.get(field, defaultValue).asString().c_str(), nullptr, 10);
		else
			return root.get(field, defaultValue).asInt();
	}
}

int64_t FFMpeg::asInt64(Json::Value root, string field, int64_t defaultValue)
{
	if (field == "")
	{
		if (root.type() == Json::stringValue)
			return strtoll(root.asString().c_str(), nullptr, 10);
		else
			return root.asInt64();
	}
	else
	{
		if (root.get(field, defaultValue).type() == Json::stringValue)
			return strtoll(root.get(field, defaultValue).asString().c_str(), nullptr, 10);
		else
			return root.get(field, defaultValue).asInt64();
	}
}

double FFMpeg::asDouble(Json::Value root, string field, double defaultValue)
{
	if (field == "")
	{
		if (root.type() == Json::stringValue)
			return stod(root.asString(), nullptr);
		else
			return root.asDouble();
	}
	else
	{
		if (root.get(field, defaultValue).type() == Json::stringValue)
			return stod(root.get(field, defaultValue).asString(), nullptr);
		else
			return root.get(field, defaultValue).asDouble();
	}
}

bool FFMpeg::asBool(Json::Value root, string field, bool defaultValue)
{
	if (field == "")
	{
		if (root.type() == Json::stringValue)
		{
			string sTrue = "true";

			bool isEqual = root.asString().length() != sTrue.length() ? false :
				equal(root.asString().begin(), root.asString().end(), sTrue.begin(),
						[](int c1, int c2){ return toupper(c1) == toupper(c2); });

			return isEqual ? true : false;
		}
		else
			return root.asBool();
	}
	else
	{
		if (root.get(field, defaultValue).type() == Json::stringValue)
		{
			string sTrue = "true";

			bool isEqual = root.asString().length() != sTrue.length() ? false :
				equal(root.asString().begin(), root.asString().end(), sTrue.begin(),
						[](int c1, int c2){ return toupper(c1) == toupper(c2); });

			return isEqual ? true : false;
		}
		else
			return root.get(field, defaultValue).asBool();
	}
}

void FFMpeg::setStatus(
	int64_t ingestionJobKey,
	int64_t encodingJobKey,
	int64_t durationInMilliSeconds,
	string mmsSourceAssetPathName,
	string stagingEncodedAssetPathName
)
{

    _currentIngestionJobKey             = ingestionJobKey;	// just for log
    _currentEncodingJobKey              = encodingJobKey;	// just for log
    _currentDurationInMilliSeconds      = durationInMilliSeconds;	// in case of some functionalities, it is important for getEncodingProgress
    _currentMMSSourceAssetPathName      = mmsSourceAssetPathName;	// just for log
    _currentStagingEncodedAssetPathName = stagingEncodedAssetPathName;	// just for log

	_startFFMpegMethod = chrono::system_clock::now();
}

