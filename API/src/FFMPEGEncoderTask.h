
#ifndef FFMPEGEncoderTask_h
#define FFMPEGEncoderTask_h

#include "spdlog/spdlog.h"
#include "FFMpeg.h"
#include <string>
#include <chrono>

#ifndef __FILEREF__
    #ifdef __APPLE__
        #define __FILEREF__ string("[") + string(__FILE__).substr(string(__FILE__).find_last_of("/") + 1) + ":" + to_string(__LINE__) + "] "
    #else
        #define __FILEREF__ string("[") + basename((char *) __FILE__) + ":" + to_string(__LINE__) + "] "
    #endif
#endif


class FFMPEGEncoderTask {

	public:
		struct Encoding
		{
			bool					_available;
			pid_t					_childPid;
			int64_t					_encodingJobKey;
			shared_ptr<FFMpeg>		_ffmpeg;
			string					_errorMessage;
		};

		struct LiveProxyAndGrid: public Encoding
		{
			string					_method;	// liveProxy, liveGrid or awaitingTheBeginning
			bool					_killedBecauseOfNotWorking;	// by monitorThread

			string					_liveGridOutputType;	// only for LiveGrid
			Json::Value				_outputsRoot;

			int64_t					_ingestionJobKey;
			Json::Value				_ingestedParametersRoot;

			Json::Value				_inputsRoot;
			mutex					_inputsRootMutex;

			chrono::system_clock::time_point	_proxyStart;

			shared_ptr<LiveProxyAndGrid> cloneForMonitor()
			{
				shared_ptr<LiveProxyAndGrid> liveProxyAndGrid =
					make_shared<LiveProxyAndGrid>();

				liveProxyAndGrid->_available = _available;
				liveProxyAndGrid->_childPid = _childPid;
				liveProxyAndGrid->_encodingJobKey = _encodingJobKey;
				liveProxyAndGrid->_method = _method;
				liveProxyAndGrid->_ffmpeg = _ffmpeg;
				liveProxyAndGrid->_killedBecauseOfNotWorking = _killedBecauseOfNotWorking;
				liveProxyAndGrid->_errorMessage = _errorMessage;
				liveProxyAndGrid->_liveGridOutputType = _liveGridOutputType;

				liveProxyAndGrid->_ingestionJobKey = _ingestionJobKey;

				liveProxyAndGrid->_outputsRoot = _outputsRoot;
				liveProxyAndGrid->_ingestedParametersRoot = _ingestedParametersRoot;
				liveProxyAndGrid->_inputsRoot = _inputsRoot;

				liveProxyAndGrid->_proxyStart = _proxyStart;

				return liveProxyAndGrid;
			}
		};

		struct LiveRecording: public Encoding
		{
			bool					_killedBecauseOfNotWorking;	// by monitorThread

			bool					_monitoringEnabled;
			bool					_monitoringFrameIncreasingEnabled;

			int64_t					_ingestionJobKey;
			bool					_externalEncoder;
			Json::Value				_encodingParametersRoot;
			Json::Value				_ingestedParametersRoot;
			string					_streamSourceType;
			string					_chunksTranscoderStagingContentsPath;
			string					_chunksNFSStagingContentsPath;
			string					_segmentListFileName;
			string					_recordedFileNamePrefix;
			string					_lastRecordedAssetFileName;
			double					_lastRecordedAssetDurationInSeconds;
			string					_channelLabel;
			string					_segmenterType;
			chrono::system_clock::time_point	_recordingStart;

			bool					_virtualVOD;
			string					_monitorVirtualVODManifestDirectoryPath;	// used to build virtualVOD
			string					_monitorVirtualVODManifestFileName;			// used to build virtualVOD
			string					_virtualVODStagingContentsPath;
			int64_t					_liveRecorderVirtualVODImageMediaItemKey;

			shared_ptr<LiveRecording> cloneForMonitorAndVirtualVOD()
			{
				shared_ptr<LiveRecording> liveRecording =
					make_shared<LiveRecording>();

				liveRecording->_available = _available;
				liveRecording->_childPid = _childPid;
				liveRecording->_monitoringEnabled = _monitoringEnabled;
				liveRecording->_monitoringFrameIncreasingEnabled = _monitoringFrameIncreasingEnabled;
				liveRecording->_encodingJobKey = _encodingJobKey;
				liveRecording->_externalEncoder = _externalEncoder;
				liveRecording->_ffmpeg = _ffmpeg;
				liveRecording->_killedBecauseOfNotWorking = _killedBecauseOfNotWorking;
				liveRecording->_errorMessage = _errorMessage;
				liveRecording->_ingestionJobKey = _ingestionJobKey;
				liveRecording->_encodingParametersRoot = _encodingParametersRoot;
				liveRecording->_ingestedParametersRoot = _ingestedParametersRoot;
				liveRecording->_streamSourceType = _streamSourceType;
				liveRecording->_chunksTranscoderStagingContentsPath = _chunksTranscoderStagingContentsPath;
				liveRecording->_chunksNFSStagingContentsPath = _chunksNFSStagingContentsPath;
				liveRecording->_segmentListFileName = _segmentListFileName;
				liveRecording->_recordedFileNamePrefix = _recordedFileNamePrefix;
				liveRecording->_lastRecordedAssetFileName = _lastRecordedAssetFileName;
				liveRecording->_lastRecordedAssetDurationInSeconds = _lastRecordedAssetDurationInSeconds;
				liveRecording->_channelLabel = _channelLabel;
				liveRecording->_segmenterType = _segmenterType;
				liveRecording->_recordingStart = _recordingStart;
				liveRecording->_virtualVOD = _virtualVOD;
				liveRecording->_monitorVirtualVODManifestDirectoryPath = _monitorVirtualVODManifestDirectoryPath;
				liveRecording->_monitorVirtualVODManifestFileName = _monitorVirtualVODManifestFileName;
				liveRecording->_virtualVODStagingContentsPath = _virtualVODStagingContentsPath;
				liveRecording->_liveRecorderVirtualVODImageMediaItemKey = _liveRecorderVirtualVODImageMediaItemKey;

				return liveRecording;
			}
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


	public:
		FFMPEGEncoderTask(
			shared_ptr<Encoding> encoding,
			int64_t encodingJobKey,
			Json::Value configuration,
			mutex* encodingCompletedMutex,                                                                        
			map<int64_t, shared_ptr<EncodingCompleted>>* encodingCompletedMap,                                    
			shared_ptr<spdlog::logger> logger);
		~FFMPEGEncoderTask();

	private:
		mutex*				_encodingCompletedMutex;
		map<int64_t, shared_ptr<EncodingCompleted>>*	_encodingCompletedMap;

		string				_tvChannelConfigurationDirectory;


		void addEncodingCompleted();
		void removeEncodingCompletedIfPresent();

		int64_t ingestContentByPushingBinary(
			int64_t ingestionJobKey,
			string workflowMetadata,
			string fileFormat,
			string binaryPathFileName,
			int64_t binaryFileSizeInBytes,
			int64_t userKey,
			string apiKey,
			string mmsWorkflowIngestionURL,
			string mmsBinaryIngestionURL);

	protected:
		shared_ptr<Encoding>	_encoding;
		int64_t				_encodingJobKey;

		bool				_completedWithError;
		bool				_killedByUser;
		bool				_urlForbidden;
		bool				_urlNotFound;

		int64_t				_mmsAPITimeoutInSeconds;
		int64_t				_mmsBinaryTimeoutInSeconds;
		long				_tvChannelPort_Start;
		long				_tvChannelPort_MaxNumberOfOffsets;

		shared_ptr<spdlog::logger>		_logger;

		string buildAddContentIngestionWorkflow(
			int64_t ingestionJobKey,
			string label,
			string fileFormat,
			string ingester,

			// in case of a new content
			string sourceURL,	// if empty it means binary is ingested later (PUSH)
			string title,
			Json::Value userDataRoot,
			Json::Value ingestedParametersRoot,	// it could be also nullValue

			// in case of a Variant
			int64_t variantOfMediaItemKey = -1,		// in case of a variant, otherwise -1
			int64_t variantEncodingProfileKey = -1);	// in case of a variant, otherwise -1

		long getAddContentIngestionJobKey(
			int64_t ingestionJobKey,
			string ingestionResponse);

		void uploadLocalMediaToMMS(
			int64_t ingestionJobKey,
			int64_t encodingJobKey,
			Json::Value ingestedParametersRoot,
			Json::Value encodingProfileDetailsRoot,
			Json::Value encodingParametersRoot,
			string sourceFileExtension,
			string encodedStagingAssetPathName,
			string workflowLabel,
			string ingester,
			int64_t variantOfMediaItemKey = -1,	// in case Media is a variant of a MediaItem already present
			int64_t variantEncodingProfileKey = -1);	// in case Media is a variant of a MediaItem already present

		string downloadMediaFromMMS(
			int64_t ingestionJobKey,
			int64_t encodingJobKey,
			shared_ptr<FFMpeg> ffmpeg,
			string sourceFileExtension,
			string sourcePhysicalDeliveryURL,
			string destAssetPathName);

		void createOrUpdateTVDvbLastConfigurationFile(
			int64_t ingestionJobKey,
			int64_t encodingJobKey,
			string multicastIP,
			string multicastPort,
			string tvType,
			int64_t tvServiceId,
			int64_t tvFrequency,
			int64_t tvSymbolRate,
			int64_t tvBandwidthInMhz,
			string tvModulation,
			int tvVideoPid,
			int tvAudioItalianPid,
			bool toBeAdded
		);

		pair<string, string> getTVMulticastFromDvblastConfigurationFile(
			int64_t ingestionJobKey,
			int64_t encodingJobKey,
			string tvType,
			int64_t tvServiceId,
			int64_t tvFrequency,
			int64_t tvSymbolRate,
			int64_t tvBandwidthInMhz,
			string tvModulation
		);

};

#endif
