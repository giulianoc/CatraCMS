
#include "FFMPEGEncoderTask.h"


class GenerateFrames: public FFMPEGEncoderTask {

	public:
		GenerateFrames(
			shared_ptr<Encoding> encoding,
			int64_t ingestionJobKey,
			int64_t encodingJobKey,
			Json::Value configuration,
			mutex* encodingCompletedMutex,                                                                        
			map<int64_t, shared_ptr<EncodingCompleted>>* encodingCompletedMap,                                    
			shared_ptr<spdlog::logger> logger):
		FFMPEGEncoderTask(encoding, ingestionJobKey, encodingJobKey, configuration, encodingCompletedMutex,
			encodingCompletedMap, logger)
		{ };

		void encodeContent(Json::Value metadataRoot);

	private:
		int64_t generateFrames_ingestFrame(
			int64_t ingestionJobKey,
			bool externalEncoder,
			string imagesDirectory, string generatedFrameFileName,
			string addContentTitle,
			Json::Value userDataRoot,
			string outputFileFormat,
			Json::Value ingestedParametersRoot,
			Json::Value encodingParametersRoot);
};

