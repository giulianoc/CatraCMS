
#include "VideoSpeed.h"

#include "JSONUtils.h"
#include "MMSEngineDBFacade.h"
#include "catralibraries/DateTime.h"
#include "spdlog/spdlog.h"

void VideoSpeed::encodeContent(json metadataRoot)
{
	string api = "videoSpeed";

	_logger->info(
		__FILEREF__ + "Received " + api + ", _ingestionJobKey: " + to_string(_ingestionJobKey) + ", _encodingJobKey: " + to_string(_encodingJobKey) +
		", requestBody: " + JSONUtils::toString(metadataRoot)
	);

	try
	{
		// json metadataRoot = JSONUtils::toJson(
		// 	-1, _encodingJobKey, requestBody);

		// int64_t ingestionJobKey = JSONUtils::asInt64(metadataRoot, "ingestionJobKey", -1);
		bool externalEncoder = JSONUtils::asBool(metadataRoot, "externalEncoder", false);
		json ingestedParametersRoot = metadataRoot["ingestedParametersRoot"];
		json encodingParametersRoot = metadataRoot["encodingParametersRoot"];

		json encodingProfileDetailsRoot = encodingParametersRoot["encodingProfileDetails"];

		int64_t videoDurationInMilliSeconds = JSONUtils::asInt64(encodingParametersRoot, "sourceDurationInMilliSeconds", -1);

		string videoSpeedType;
		videoSpeedType =
			JSONUtils::asString(ingestedParametersRoot, "speedType", MMSEngineDBFacade::toString(MMSEngineDBFacade::VideoSpeedType::SlowDown));

		int videoSpeedSize = JSONUtils::asInt(ingestedParametersRoot, "videoSpeedSize", 3);

		string sourceFileExtension;
		{
			string field = "sourceFileExtension";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			sourceFileExtension = JSONUtils::asString(encodingParametersRoot, field, "");
		}

		string sourceAssetPathName;
		string encodedStagingAssetPathName;

		if (externalEncoder)
		{
			string field = "sourceTranscoderStagingAssetPathName";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			sourceAssetPathName = JSONUtils::asString(encodingParametersRoot, field, "");

			{
				size_t endOfDirectoryIndex = sourceAssetPathName.find_last_of("/");
				if (endOfDirectoryIndex != string::npos)
				{
					string directoryPathName = sourceAssetPathName.substr(0, endOfDirectoryIndex);

					_logger->info(
						__FILEREF__ + "Creating directory" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
						", _encodingJobKey: " + to_string(_encodingJobKey) + ", directoryPathName: " + directoryPathName
					);
					fs::create_directories(directoryPathName);
					fs::permissions(
						directoryPathName,
						fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec |
							fs::perms::others_read | fs::perms::others_exec,
						fs::perm_options::replace
					);
				}
			}

			field = "encodedTranscoderStagingAssetPathName";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			encodedStagingAssetPathName = JSONUtils::asString(encodingParametersRoot, field, "");

			{
				size_t endOfDirectoryIndex = encodedStagingAssetPathName.find_last_of("/");
				if (endOfDirectoryIndex != string::npos)
				{
					string directoryPathName = encodedStagingAssetPathName.substr(0, endOfDirectoryIndex);

					_logger->info(
						__FILEREF__ + "Creating directory" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
						", _encodingJobKey: " + to_string(_encodingJobKey) + ", directoryPathName: " + directoryPathName
					);
					fs::create_directories(directoryPathName);
					fs::permissions(
						directoryPathName,
						fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec |
							fs::perms::others_read | fs::perms::others_exec,
						fs::perm_options::replace
					);
				}
			}

			field = "sourcePhysicalDeliveryURL";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			string sourcePhysicalDeliveryURL = JSONUtils::asString(encodingParametersRoot, field, "");

			sourceAssetPathName = downloadMediaFromMMS(
				_ingestionJobKey, _encodingJobKey, _encoding->_ffmpeg, sourceFileExtension, sourcePhysicalDeliveryURL, sourceAssetPathName
			);
		}
		else
		{
			string field = "sourceAssetPathName";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			sourceAssetPathName = JSONUtils::asString(encodingParametersRoot, field, "");

			field = "encodedNFSStagingAssetPathName";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			encodedStagingAssetPathName = JSONUtils::asString(encodingParametersRoot, field, "");
		}

		_encoding->_ffmpeg->videoSpeed(
			sourceAssetPathName, videoDurationInMilliSeconds,

			videoSpeedType, videoSpeedSize,

			encodingProfileDetailsRoot,

			encodedStagingAssetPathName, _encodingJobKey, _ingestionJobKey, &(_encoding->_childPid)
		);

		_encoding->_ffmpegTerminatedSuccessful = true;

		_logger->info(
			__FILEREF__ + "Encode content finished" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
			", _encodingJobKey: " + to_string(_encodingJobKey) + ", encodedStagingAssetPathName: " + encodedStagingAssetPathName
		);

		if (externalEncoder)
		{
			{
				_logger->info(
					__FILEREF__ + "Remove file" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
					", _encodingJobKey: " + to_string(_encodingJobKey) + ", sourceAssetPathName: " + sourceAssetPathName
				);

				fs::remove_all(sourceAssetPathName);
			}

			string workflowLabel = JSONUtils::asString(ingestedParametersRoot, "title", "") + " (add videoSpeed from external transcoder)";

			int64_t encodingProfileKey = JSONUtils::asInt64(encodingParametersRoot, "encodingProfileKey", -1);

			uploadLocalMediaToMMS(
				_ingestionJobKey, _encodingJobKey, ingestedParametersRoot, encodingProfileDetailsRoot, encodingParametersRoot, sourceFileExtension,
				encodedStagingAssetPathName, workflowLabel,
				"External Transcoder", // ingester
				encodingProfileKey
			);
		}
	}
	catch (FFMpegEncodingKilledByUser &e)
	{
		string eWhat = e.what();
		SPDLOG_ERROR(
			"{} API failed (EncodingKilledByUser)"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", API: {}"
			", requestBody: {}"
			", e.what(): {}",
			DateTime::utcToLocalString(chrono::system_clock::to_time_t(chrono::system_clock::now())), _ingestionJobKey, _encodingJobKey, api,
			JSONUtils::toString(metadataRoot), (eWhat.size() > 130 ? eWhat.substr(0, 130) : eWhat)
		);

		// used by FFMPEGEncoderTask
		_killedByUser = true;

		throw e;
	}
	catch (runtime_error &e)
	{
		string eWhat = e.what();
		string errorMessage = std::format(
			"{} API failed (runtime_error)"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", API: {}"
			", requestBody: {}"
			", e.what(): {}",
			DateTime::utcToLocalString(chrono::system_clock::to_time_t(chrono::system_clock::now())), _ingestionJobKey, _encodingJobKey, api,
			JSONUtils::toString(metadataRoot), (eWhat.size() > 130 ? eWhat.substr(0, 130) : eWhat)
		);
		SPDLOG_ERROR(errorMessage);

		// used by FFMPEGEncoderTask
		_encoding->pushErrorMessage(errorMessage);
		_completedWithError = true;

		throw e;
	}
	catch (exception &e)
	{
		string eWhat = e.what();
		string errorMessage = std::format(
			"{} API failed (exception)"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", API: {}"
			", requestBody: {}"
			", e.what(): {}",
			DateTime::utcToLocalString(chrono::system_clock::to_time_t(chrono::system_clock::now())), _ingestionJobKey, _encodingJobKey, api,
			JSONUtils::toString(metadataRoot), (eWhat.size() > 130 ? eWhat.substr(0, 130) : eWhat)
		);
		SPDLOG_ERROR(errorMessage);

		// used by FFMPEGEncoderTask
		_encoding->pushErrorMessage(errorMessage);
		_completedWithError = true;

		throw e;
	}
}
