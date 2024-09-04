
#include "JSONUtils.h"
#include "MMSEngineDBFacade.h"
#include "MMSEngineProcessor.h"

void MMSEngineProcessor::manageEncodeTask(
	int64_t ingestionJobKey, shared_ptr<Workspace> workspace, json parametersRoot,
	vector<tuple<int64_t, MMSEngineDBFacade::ContentType, Validator::DependencyType, bool>> &dependencies
)
{
	try
	{
		if (dependencies.size() == 0)
		{
			string errorMessage = string() + "No media received to be encoded" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
								  ", ingestionJobKey: " + to_string(ingestionJobKey) + ", dependencies.size: " + to_string(dependencies.size());
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		MMSEngineDBFacade::ContentType contentType;
		{
			tuple<int64_t, MMSEngineDBFacade::ContentType, Validator::DependencyType, bool> &keyAndDependencyType = dependencies[0];

			tie(ignore, contentType, ignore, ignore) = keyAndDependencyType;
		}

		MMSEngineDBFacade::EncodingPriority encodingPriority;
		{
			string field = "encodingPriority";
			if (!JSONUtils::isMetadataPresent(parametersRoot, field))
			{
				encodingPriority = static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
			}
			else
			{
				encodingPriority = MMSEngineDBFacade::toEncodingPriority(JSONUtils::asString(parametersRoot, field, ""));
			}
		}

		int64_t encodingProfileKey = -1;
		json encodingProfileDetailsRoot;
		{
			// This task shall contain encodingProfileKey or
			// encodingProfileLabel. We cannot have encodingProfilesSetKey
			// because we replaced it with a GroupOfTasks
			//  having just encodingProfileKey

			string keyField = "encodingProfileKey";
			string labelField = "encodingProfileLabel";
			if (JSONUtils::isMetadataPresent(parametersRoot, keyField))
			{
				encodingProfileKey = JSONUtils::asInt64(parametersRoot, keyField, 0);
			}
			else if (JSONUtils::isMetadataPresent(parametersRoot, labelField))
			{
				string encodingProfileLabel = JSONUtils::asString(parametersRoot, labelField, "");

				encodingProfileKey = _mmsEngineDBFacade->getEncodingProfileKeyByLabel(workspace->_workspaceKey, contentType, encodingProfileLabel);
			}
			else
			{
				string errorMessage = string() + "Both fields are not present or it is null" + ", ingestionJobKey: " + to_string(ingestionJobKey) +
									  ", _processorIdentifier: " + to_string(_processorIdentifier) + ", Field: " + keyField +
									  ", Field: " + labelField;
				SPDLOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}

			{
				string jsonEncodingProfile;

				tuple<string, MMSEngineDBFacade::ContentType, MMSEngineDBFacade::DeliveryTechnology, string> encodingProfileDetails =
					_mmsEngineDBFacade->getEncodingProfileDetailsByKey(workspace->_workspaceKey, encodingProfileKey);
				tie(ignore, ignore, ignore, jsonEncodingProfile) = encodingProfileDetails;

				encodingProfileDetailsRoot = JSONUtils::toJson(jsonEncodingProfile);
			}
		}

		// it is not possible to manage more than one encode because:
		// 1. inside _mmsEngineDBFacade->addEncodingJob, the ingestionJob is
		// updated to encodingQueue
		//		and the second call will fail (because the update of the
		// ingestion was already done
		//	2. The ingestionJob mantains the status of the encoding, how would
		// be managed 		the status in case of more than one encoding?
		//	3. EncoderVideoAudioProxy::encodeContent_VideoAudio_through_ffmpeg
		// saves 		encoder URL and staging directory into database to
		// recover the scenario 		where the engine reboot.
		// 2021-06-07: we have the need to manage more than one encoding.
		//	For example, we have the I-Frame task and, on success, we want to
		// encode 	all the images generated by the I-Frames task. 	In this
		// scenario the Encode task receives a lot of images as input. 	Solution
		// no. 1: 		we manage all the inputs sequentially (as it is doing
		// the RemoveContent task). 		This is not a good solution because,
		// in case of the Encode task and in case of videos, 		every
		// encoding would take a lot of time. Manage all these encodings
		// sequentially 		is not what the User expect to see. 	Solution
		// no.
		// 2: 		we can create one EncodingJob for each encoding. This is
		// exactly
		// what the User expects 		because the encodings will run in
		// parallel.
		//
		//		Issue 1: How to manage the ingestionJob status in case of
		// multiple encodings? 		Issue 2: GUI and API are planned to manage
		// one EncodingJob for each IngestionJob
		//
		//	2021-08-25: In case the Encode Task is received by the MMS with
		// multiple References 	as input during the ingestion, it will be
		// automatically converted with a 	GroupOfTasks with all the Encode
		// Tasks as children (just done). 	The problem is when the input
		// references are generated dinamically as output 	of the parent task.
		// We will manage this issue ONLY in case of images doing 	the encoding
		// sequentially. 	For video/audio we cannot manage it sequentially
		// (like images) mainly because 	the encoder URL and the staging
		// directory are saved into the database 	to manage the recovering in
		// case of reboot of the Engine. 	This recovery is very important and
		// I
		// do not know how to manage it 	in case the task has in his queue a
		// list of encodings to do!!! 	We should save into DB also the specific
		// encoding it is doing?!?!??! 	Also che encoding progress would not
		// have sense in the "sequential/queue" scenario
		json sourcesToBeEncodedRoot = json::array();
		{
			// 2022-12-10: next for and the sourcesToBeEncodedRoot structure
			// sarebbe inutile per
			//	l'encoding di video/audio ma serve invece per l'encoding di
			// picture
			for (tuple<int64_t, MMSEngineDBFacade::ContentType, Validator::DependencyType, bool> &keyAndDependencyType : dependencies)
			{
				bool stopIfReferenceProcessingError;
				int64_t sourceMediaItemKey;
				int64_t sourcePhysicalPathKey;
				string mmsSourceAssetPathName;
				string sourcePhysicalDeliveryURL;
				string sourceFileName;
				int64_t sourceDurationInMilliSecs;
				string sourceRelativePath;
				string sourceFileExtension;
				json videoTracksRoot = json::array();
				json audioTracksRoot = json::array();
				string sourceTranscoderStagingAssetPathName;
				string encodedTranscoderStagingAssetPathName; // used in case of
															  // external encoder
				string encodedNFSStagingAssetPathName;
				MMSEngineDBFacade::ContentType referenceContentType;
				try
				{
					tuple<int64_t, int64_t, MMSEngineDBFacade::ContentType, string, string, string, string, int64_t, string, string, bool>
						dependencyInfo = processDependencyInfo(workspace, ingestionJobKey, keyAndDependencyType);
					tie(sourceMediaItemKey, sourcePhysicalPathKey, referenceContentType, mmsSourceAssetPathName, sourceRelativePath, sourceFileName,
						sourceFileExtension, sourceDurationInMilliSecs, sourcePhysicalDeliveryURL, sourceTranscoderStagingAssetPathName,
						stopIfReferenceProcessingError) = dependencyInfo;

					if (contentType != referenceContentType)
					{
						string errorMessage = string() + "Wrong content type" + ", ingestionJobKey: " + to_string(ingestionJobKey) +
											  ", contentType: " + MMSEngineDBFacade::toString(contentType) +
											  ", referenceContentType: " + MMSEngineDBFacade::toString(referenceContentType);
						SPDLOG_ERROR(errorMessage);

						throw runtime_error(errorMessage);
					}

					// check if the profile is already present for the source
					// content
					{
						try
						{
							bool warningIfMissing = true;
							int64_t localPhysicalPathKey = _mmsEngineDBFacade->getPhysicalPathDetails(
								sourceMediaItemKey, encodingProfileKey, warningIfMissing,
								// 2022-12-18: MIK potrebbe essere stato
								// appena aggiunto
								true
							);

							string errorMessage =
								string() + "Content profile is already present" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
								", ingestionJobKey: " + to_string(ingestionJobKey) + ", sourceMediaItemKey: " + to_string(sourceMediaItemKey) +
								", encodingProfileKey: " + to_string(encodingProfileKey);
							SPDLOG_ERROR(errorMessage);

							throw runtime_error(errorMessage);
						}
						catch (MediaItemKeyNotFound &e)
						{
						}
					}

					string encodedFileName;
					string fileFormat;
					{
						fileFormat = JSONUtils::asString(encodingProfileDetailsRoot, "fileFormat", "");

						encodedFileName = to_string(ingestionJobKey) + "_" + to_string(encodingProfileKey) +
										  getEncodedFileExtensionByEncodingProfile(encodingProfileDetailsRoot);

						/*
						if (fileFormat == "hls" || fileFormat == "dash")
						{
							;
						}
						else
						{
							encodedFileName.append(".");
							encodedFileName.append(fileFormat);
						}
						*/
					}

					{
						bool removeLinuxPathIfExist = false;
						bool neededForTranscoder = true;
						encodedTranscoderStagingAssetPathName = _mmsStorage->getStagingAssetPathName(
							neededForTranscoder,
							workspace->_directoryName,	// workspaceDirectoryName
							to_string(ingestionJobKey), // directoryNamePrefix
							"/",						// relativePath,
							// as specified by doc
							// (TASK_01_Add_Content_JSON_Format.txt), in
							// case of hls the directory inside the tar.gz
							// has to be 'content'
							(fileFormat == "hls" || fileFormat == "dash") ? "content" : encodedFileName,
							-1, // _encodingItem->_mediaItemKey, not used
								// because encodedFileName is not ""
							-1, // _encodingItem->_physicalPathKey, not used
								// because encodedFileName is not ""
							removeLinuxPathIfExist
						);

						neededForTranscoder = false;
						encodedNFSStagingAssetPathName = _mmsStorage->getStagingAssetPathName(
							neededForTranscoder,
							workspace->_directoryName,	// workspaceDirectoryName
							to_string(ingestionJobKey), // directoryNamePrefix
							"/",						// relativePath,
							encodedFileName,			// fileName
							-1,							// _encodingItem->_mediaItemKey, not used
														// because encodedFileName is not ""
							-1,							// _encodingItem->_physicalPathKey, not used
														// because encodedFileName is not ""
							removeLinuxPathIfExist
						);
					}

					{
						if (contentType == MMSEngineDBFacade::ContentType::Video)
						{
							vector<tuple<int64_t, int, int64_t, int, int, string, string, long, string>> videoTracks;
							vector<tuple<int64_t, int, int64_t, long, string, long, int, string>> audioTracks;

							// int64_t sourceMediaItemKey = -1;
							_mmsEngineDBFacade->getVideoDetails(
								-1, sourcePhysicalPathKey,
								// 2022-12-18: MIK potrebbe essere stato appena
								// aggiunto
								true, videoTracks, audioTracks
							);

							for (tuple<int64_t, int, int64_t, int, int, string, string, long, string> videoTrack : videoTracks)
							{
								int trackIndex;
								tie(ignore, trackIndex, ignore, ignore, ignore, ignore, ignore, ignore, ignore) = videoTrack;

								if (trackIndex != -1)
								{
									json videoTrackRoot;

									string field = "trackIndex";
									videoTrackRoot[field] = trackIndex;

									videoTracksRoot.push_back(videoTrackRoot);
								}
							}

							for (tuple<int64_t, int, int64_t, long, string, long, int, string> audioTrack : audioTracks)
							{
								int trackIndex;
								string language;
								tie(ignore, trackIndex, ignore, ignore, ignore, ignore, ignore, language) = audioTrack;

								if (trackIndex != -1 && language != "")
								{
									json audioTrackRoot;

									string field = "trackIndex";
									audioTrackRoot[field] = trackIndex;

									field = "language";
									audioTrackRoot[field] = language;

									audioTracksRoot.push_back(audioTrackRoot);
								}
							}
						}
						else if (contentType == MMSEngineDBFacade::ContentType::Audio)
						{
							vector<tuple<int64_t, int, int64_t, long, string, long, int, string>> audioTracks;

							// int64_t sourceMediaItemKey = -1;
							_mmsEngineDBFacade->getAudioDetails(
								-1, sourcePhysicalPathKey,
								// 2022-12-18: MIK potrebbe essere stato appena
								// aggiunto
								true, audioTracks
							);

							for (tuple<int64_t, int, int64_t, long, string, long, int, string> audioTrack : audioTracks)
							{
								int trackIndex;
								string language;
								tie(ignore, trackIndex, ignore, ignore, ignore, ignore, ignore, language) = audioTrack;

								if (trackIndex != -1 && language != "")
								{
									json audioTrackRoot;

									string field = "trackIndex";
									audioTrackRoot[field] = trackIndex;

									field = "language";
									audioTrackRoot[field] = language;

									audioTracksRoot.push_back(audioTrackRoot);
								}
							}
						}
					}

					json sourceRoot;

					string field = "stopIfReferenceProcessingError";
					sourceRoot[field] = stopIfReferenceProcessingError;

					field = "sourceMediaItemKey";
					sourceRoot[field] = sourceMediaItemKey;

					field = "sourcePhysicalPathKey";
					sourceRoot[field] = sourcePhysicalPathKey;

					field = "mmsSourceAssetPathName";
					sourceRoot[field] = mmsSourceAssetPathName;

					field = "sourcePhysicalDeliveryURL";
					sourceRoot[field] = sourcePhysicalDeliveryURL;

					field = "sourceDurationInMilliSecs";
					sourceRoot[field] = sourceDurationInMilliSecs;

					field = "sourceFileName";
					sourceRoot[field] = sourceFileName;

					field = "sourceRelativePath";
					sourceRoot[field] = sourceRelativePath;

					field = "sourceFileExtension";
					sourceRoot[field] = sourceFileExtension;

					field = "videoTracks";
					sourceRoot[field] = videoTracksRoot;

					field = "audioTracks";
					sourceRoot[field] = audioTracksRoot;

					field = "sourceTranscoderStagingAssetPathName";
					sourceRoot[field] = sourceTranscoderStagingAssetPathName;

					field = "encodedTranscoderStagingAssetPathName";
					sourceRoot[field] = encodedTranscoderStagingAssetPathName;

					field = "encodedNFSStagingAssetPathName";
					sourceRoot[field] = encodedNFSStagingAssetPathName;

					sourcesToBeEncodedRoot.push_back(sourceRoot);
				}
				catch (runtime_error &e)
				{
					SPDLOG_ERROR(
						string() + "processing media input failed" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
						", ingestionJobKey: " + to_string(ingestionJobKey) + ", referenceContentType: " +
						MMSEngineDBFacade::toString(referenceContentType) + ", sourceMediaItemKey: " + to_string(sourceMediaItemKey)
					);

					if (stopIfReferenceProcessingError)
						throw e;
				}
			}
		}

		if (sourcesToBeEncodedRoot.size() == 0)
		{
			// dependecies.size() > 0 perchè è stato già verificato inizialmente
			// Se sourcesToBeEncodedRoot.size() == 0 vuol dire che
			// l'encodingProfileKey era già presente
			//	per il MediaItem

			string errorMessage = string() + "Content profile is already present" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
								  ", ingestionJobKey: " + to_string(ingestionJobKey);
			SPDLOG_ERROR(errorMessage);

			throw runtime_error(errorMessage);
		}

		_mmsEngineDBFacade->addEncodingJob(
			workspace, ingestionJobKey, contentType, encodingPriority, encodingProfileKey, encodingProfileDetailsRoot,

			sourcesToBeEncodedRoot,

			_mmsWorkflowIngestionURL, _mmsBinaryIngestionURL, _mmsIngestionURL
		);
	}
	catch (DBRecordNotFound &e)
	{
		SPDLOG_ERROR(
			string() + "manageEncodeTask failed" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
			", ingestionJobKey: " + to_string(ingestionJobKey) + ", e.what(): " + e.what()
		);

		// Update IngestionJob done in the calling method

		throw e;
	}
	catch (runtime_error &e)
	{
		SPDLOG_ERROR(
			string() + "manageEncodeTask failed" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
			", ingestionJobKey: " + to_string(ingestionJobKey) + ", e.what(): " + e.what()
		);

		// Update IngestionJob done in the calling method

		throw e;
	}
	catch (exception &e)
	{
		SPDLOG_ERROR(
			string() + "manageEncodeTask failed" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
			", ingestionJobKey: " + to_string(ingestionJobKey)
		);

		// Update IngestionJob done in the calling method

		throw e;
	}
}

void MMSEngineProcessor::handleCheckEncodingEvent()
{
	try
	{
		if (isMaintenanceMode())
		{
			SPDLOG_INFO(
				string() +
				"Received handleCheckEncodingEvent, not managed it because of "
				"MaintenanceMode" +
				", _processorIdentifier: " + to_string(_processorIdentifier)
			);

			return;
		}

		SPDLOG_INFO(string() + "Received handleCheckEncodingEvent" + ", _processorIdentifier: " + to_string(_processorIdentifier));

		vector<shared_ptr<MMSEngineDBFacade::EncodingItem>> encodingItems;

		_mmsEngineDBFacade->getToBeProcessedEncodingJobs(
			_processorMMS, encodingItems, _timeBeforeToPrepareResourcesInMinutes, _maxEncodingJobsPerEvent
		);

		SPDLOG_INFO(
			string() + "_pActiveEncodingsManager->addEncodingItems" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
			", encodingItems.size: " + to_string(encodingItems.size())
		);

		_pActiveEncodingsManager->addEncodingItems(encodingItems);

		SPDLOG_INFO(
			string() + "getEncodingJobs result" + ", _processorIdentifier: " + to_string(_processorIdentifier) +
			", encodingItems.size: " + to_string(encodingItems.size())
		);
	}
	catch (AlreadyLocked &e)
	{
		_logger->warn(
			string() + "getEncodingJobs was not done" + ", _processorIdentifier: " + to_string(_processorIdentifier) + ", exception: " + e.what()
		);

		return;
		// throw e;
	}
	catch (runtime_error &e)
	{
		SPDLOG_ERROR(string() + "getEncodingJobs failed" + ", _processorIdentifier: " + to_string(_processorIdentifier) + ", exception: " + e.what());

		throw e;
	}
	catch (exception &e)
	{
		SPDLOG_ERROR(string() + "getEncodingJobs failed" + ", _processorIdentifier: " + to_string(_processorIdentifier) + ", exception: " + e.what());

		throw e;
	}
}
