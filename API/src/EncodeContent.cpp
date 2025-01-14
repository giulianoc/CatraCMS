
#include "EncodeContent.h"

#include "JSONUtils.h"
#include "MMSEngineDBFacade.h"
#include "MMSStorage.h"
#include "catralibraries/DateTime.h"
#include "spdlog/spdlog.h"

void EncodeContent::encodeContent(json metadataRoot)
{
	string api = "encodeContent";

	_logger->info(
		__FILEREF__ + "Received " + api + ", _ingestionJobKey: " + to_string(_ingestionJobKey) + ", _encodingJobKey: " + to_string(_encodingJobKey) +
		", requestBody: " + JSONUtils::toString(metadataRoot)
	);

	bool externalEncoder = false;
	string sourceAssetPathName;
	string encodedStagingAssetPathName;
	// int64_t ingestionJobKey = 1;
	try
	{
		// json metadataRoot = JSONUtils::toJson(
		// 	-1, _encodingJobKey, requestBody);

		// ingestionJobKey = JSONUtils::asInt64(metadataRoot, "ingestionJobKey", -1);

		externalEncoder = JSONUtils::asBool(metadataRoot, "externalEncoder", false);

		json ingestedParametersRoot = metadataRoot["ingestedParametersRoot"];
		json encodingParametersRoot = metadataRoot["encodingParametersRoot"];

		int videoTrackIndexToBeUsed = JSONUtils::asInt(ingestedParametersRoot, "VideoTrackIndex", -1);
		int audioTrackIndexToBeUsed = JSONUtils::asInt(ingestedParametersRoot, "AudioTrackIndex", -1);

		json filtersRoot = nullptr;
		if (JSONUtils::isMetadataPresent(ingestedParametersRoot, "filters"))
			filtersRoot = ingestedParametersRoot["filters"];

		json sourcesToBeEncodedRoot = encodingParametersRoot["sourcesToBeEncoded"];
		json sourceToBeEncodedRoot = sourcesToBeEncodedRoot[0];
		json encodingProfileDetailsRoot = encodingParametersRoot["encodingProfileDetails"];

		int64_t durationInMilliSeconds = JSONUtils::asInt64(sourceToBeEncodedRoot, "sourceDurationInMilliSecs", -1);
		MMSEngineDBFacade::ContentType contentType = MMSEngineDBFacade::toContentType(JSONUtils::asString(encodingParametersRoot, "contentType", ""));
		int64_t physicalPathKey = JSONUtils::asInt64(sourceToBeEncodedRoot, "sourcePhysicalPathKey", -1);

		json videoTracksRoot;
		string field = "videoTracks";
		if (JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			videoTracksRoot = sourceToBeEncodedRoot[field];
		json audioTracksRoot;
		field = "audioTracks";
		if (JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			audioTracksRoot = sourceToBeEncodedRoot[field];

		field = "sourceFileExtension";
		if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
		{
			string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
								  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field +
								  ", sourceToBeEncodedRoot: " + JSONUtils::toString(sourceToBeEncodedRoot);
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
		string sourceFileExtension = JSONUtils::asString(sourceToBeEncodedRoot, field, "");

		bool useOfLocalStorageForProcessingOutput = true;

		if (externalEncoder)
		{
			field = "sourceTranscoderStagingAssetPathName";
			if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			sourceAssetPathName = JSONUtils::asString(sourceToBeEncodedRoot, field, "");

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

			field = "sourcePhysicalDeliveryURL";
			if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			string sourcePhysicalDeliveryURL = JSONUtils::asString(sourceToBeEncodedRoot, field, "");

			field = "encodedTranscoderStagingAssetPathName";
			if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			encodedStagingAssetPathName = JSONUtils::asString(sourceToBeEncodedRoot, field, "");

			sourceAssetPathName = downloadMediaFromMMS(
				_ingestionJobKey, _encodingJobKey, _encoding->_ffmpeg, sourceFileExtension, sourcePhysicalDeliveryURL, sourceAssetPathName
			);
		}
		else
		{
			field = "mmsSourceAssetPathName";
			if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			sourceAssetPathName = JSONUtils::asString(sourceToBeEncodedRoot, field, "");

			if (useOfLocalStorageForProcessingOutput)
				field = "encodedTranscoderStagingAssetPathName";
			else
				field = "encodedNFSStagingAssetPathName";
			if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			encodedStagingAssetPathName = JSONUtils::asString(sourceToBeEncodedRoot, field, "");
		}

		_logger->info(
			__FILEREF__ + "encoding content..." + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
			", _encodingJobKey: " + to_string(_encodingJobKey) + ", sourceAssetPathName: " + sourceAssetPathName +
			", encodedStagingAssetPathName: " + encodedStagingAssetPathName
		);

		_encoding->_ffmpeg->encodeContent(
			sourceAssetPathName, durationInMilliSeconds, encodedStagingAssetPathName, encodingProfileDetailsRoot,
			contentType == MMSEngineDBFacade::ContentType::Video, videoTracksRoot, audioTracksRoot, videoTrackIndexToBeUsed, audioTrackIndexToBeUsed,
			filtersRoot, physicalPathKey, _encodingJobKey, _ingestionJobKey, &(_encoding->_childPid)
		);

		_encoding->_ffmpegTerminatedSuccessful = true;

		_logger->info(
			__FILEREF__ + "encoded content" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
			", _encodingJobKey: " + to_string(_encodingJobKey) + ", sourceAssetPathName: " + sourceAssetPathName +
			", encodedStagingAssetPathName: " + encodedStagingAssetPathName
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

			field = "sourceMediaItemKey";
			if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			int64_t sourceMediaItemKey = JSONUtils::asInt64(sourceToBeEncodedRoot, field, -1);

			field = "encodingProfileKey";
			if (!JSONUtils::isMetadataPresent(encodingParametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
									  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			int64_t encodingProfileKey = JSONUtils::asInt64(encodingParametersRoot, field, -1);

			string workflowLabel =
				"Add Variant " + to_string(sourceMediaItemKey) + " - " + to_string(encodingProfileKey) + " (encoding from external transcoder)";
			uploadLocalMediaToMMS(
				_ingestionJobKey, _encodingJobKey, ingestedParametersRoot, encodingProfileDetailsRoot, encodingParametersRoot, sourceFileExtension,
				encodedStagingAssetPathName, workflowLabel,
				"External Transcoder", // ingester
				encodingProfileKey, sourceMediaItemKey
			);
		}
		else
		{
			if (useOfLocalStorageForProcessingOutput)
			{
				field = "encodedNFSStagingAssetPathName";
				if (!JSONUtils::isMetadataPresent(sourceToBeEncodedRoot, field))
				{
					string errorMessage = __FILEREF__ + "Field is not present or it is null" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
										  ", _encodingJobKey: " + to_string(_encodingJobKey) + ", Field: " + field;
					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}
				string encodedNFSStagingAssetPathName = JSONUtils::asString(sourceToBeEncodedRoot, field, "");

				// move encodedStagingAssetPathName (encodedTranscoderStagingAssetPathName) in encodedNFSStagingAssetPathName
				_logger->info(
					__FILEREF__ + "moving file" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
					", _encodingJobKey: " + to_string(_encodingJobKey) + ", encodedStagingAssetPathName: " + encodedStagingAssetPathName +
					", encodedNFSStagingAssetPathName: " + encodedNFSStagingAssetPathName
				);
				int64_t moveElapsedInSeconds =
					MMSStorage::move(_ingestionJobKey, encodedStagingAssetPathName, encodedNFSStagingAssetPathName, _logger);
				_logger->info(
					__FILEREF__ + "moved file" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
					", _encodingJobKey: " + to_string(_encodingJobKey) + ", encodedStagingAssetPathName: " + encodedStagingAssetPathName +
					", encodedNFSStagingAssetPathName: " + encodedNFSStagingAssetPathName +
					", moveElapsedInSeconds: " + to_string(moveElapsedInSeconds)
				);
			}
		}
	}
	catch (FFMpegEncodingKilledByUser &e)
	{
		if (externalEncoder)
		{
			if (sourceAssetPathName != "" && fs::exists(sourceAssetPathName))
			{
				_logger->info(
					__FILEREF__ + "Remove file" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
					", _encodingJobKey: " + to_string(_encodingJobKey) + ", sourceAssetPathName: " + sourceAssetPathName
				);

				fs::remove_all(sourceAssetPathName);
			}

			if (encodedStagingAssetPathName != "")
			{
				size_t endOfDirectoryIndex = encodedStagingAssetPathName.find_last_of("/");
				if (endOfDirectoryIndex != string::npos)
				{
					string directoryPathName = encodedStagingAssetPathName.substr(0, endOfDirectoryIndex);

					_logger->info(__FILEREF__ + "removeDirectory" + ", directoryPathName: " + directoryPathName);
					fs::remove_all(directoryPathName);
				}
			}
		}

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
		if (externalEncoder)
		{
			if (sourceAssetPathName != "" && fs::exists(sourceAssetPathName))
			{
				_logger->info(
					__FILEREF__ + "Remove file" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
					", _encodingJobKey: " + to_string(_encodingJobKey) + ", sourceAssetPathName: " + sourceAssetPathName
				);

				fs::remove_all(sourceAssetPathName);
			}

			if (encodedStagingAssetPathName != "")
			{
				size_t endOfDirectoryIndex = encodedStagingAssetPathName.find_last_of("/");
				if (endOfDirectoryIndex != string::npos)
				{
					string directoryPathName = encodedStagingAssetPathName.substr(0, endOfDirectoryIndex);

					_logger->info(__FILEREF__ + "removeDirectory" + ", directoryPathName: " + directoryPathName);
					fs::remove_all(directoryPathName);
				}
			}
		}

		string eWhat = e.what();
		SPDLOG_ERROR(
			"{} API failed (runtime_error)"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", API: {}"
			", requestBody: {}"
			", e.what(): {}",
			DateTime::utcToLocalString(chrono::system_clock::to_time_t(chrono::system_clock::now())), _ingestionJobKey, _encodingJobKey, api,
			JSONUtils::toString(metadataRoot), (eWhat.size() > 130 ? eWhat.substr(0, 130) : eWhat)
		);

		// used by FFMPEGEncoderTask
		_encoding->pushErrorMessage(e.what());
		_completedWithError = true;

		throw e;
	}
	catch (exception &e)
	{
		if (externalEncoder)
		{
			if (sourceAssetPathName != "" && fs::exists(sourceAssetPathName))
			{
				_logger->info(
					__FILEREF__ + "Remove file" + ", _ingestionJobKey: " + to_string(_ingestionJobKey) +
					", _encodingJobKey: " + to_string(_encodingJobKey) + ", sourceAssetPathName: " + sourceAssetPathName
				);

				fs::remove_all(sourceAssetPathName);
			}

			if (encodedStagingAssetPathName != "")
			{
				size_t endOfDirectoryIndex = encodedStagingAssetPathName.find_last_of("/");
				if (endOfDirectoryIndex != string::npos)
				{
					string directoryPathName = encodedStagingAssetPathName.substr(0, endOfDirectoryIndex);

					_logger->info(__FILEREF__ + "removeDirectory" + ", directoryPathName: " + directoryPathName);
					fs::remove_all(directoryPathName);
				}
			}
		}

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
