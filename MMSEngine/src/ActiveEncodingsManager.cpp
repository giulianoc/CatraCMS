/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   EnodingsManager.cpp
 * Author: giuliano
 * 
 * Created on February 4, 2018, 7:18 PM
 */

#include "JSONUtils.h"
#include "ActiveEncodingsManager.h"
#include "catralibraries/System.h"
#include "FFMpeg.h"

ActiveEncodingsManager::ActiveEncodingsManager(
    Json::Value configuration,
    shared_ptr<MultiEventsSet> multiEventsSet,
    shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade,
    shared_ptr<MMSStorage> mmsStorage,
    shared_ptr<spdlog::logger> logger) 
{
    _logger = logger;
    _configuration = configuration;
    _mmsEngineDBFacade = mmsEngineDBFacade;
    _mmsStorage = mmsStorage; 
    
    _encodersLoadBalancer = make_shared<EncodersLoadBalancer>(_configuration, _logger);

	_hostName				= System::getHostName();

	_maxSecondsToWaitUpdateEncodingJobLock  = JSONUtils::asInt(_configuration["mms"]["locks"], "maxSecondsToWaitUpdateEncodingJobLock", 0);
	_logger->info(__FILEREF__ + "Configuration item"
		+ ", mms->locks->maxSecondsToWaitUpdateEncodingJobLock: " + to_string(_maxSecondsToWaitUpdateEncodingJobLock)
	);

    #ifdef __LOCALENCODER__
        _runningEncodingsNumber = 0;
    #endif

    {
		shared_ptr<long> faceRecognitionNumber = make_shared<long>(0);
		int maxFaceRecognitionNumber = JSONUtils::asInt(_configuration["mms"], "maxFaceRecognitionNumber", 0);
		_logger->info(__FILEREF__ + "Configuration item"
			+ ", mms->maxFaceRecognitionNumber: " + to_string(maxFaceRecognitionNumber)
		);
		maxFaceRecognitionNumber +=
			(MAXHIGHENCODINGSTOBEMANAGED + MAXMEDIUMENCODINGSTOBEMANAGED + MAXLOWENCODINGSTOBEMANAGED);
        
        int lastProxyIdentifier = 0;

        for (EncodingJob& encodingJob: _lowPriorityEncodingJobs)
        {
            encodingJob._encoderVideoAudioProxy.init(
                lastProxyIdentifier++, &_mtEncodingJobs,
                    _configuration, multiEventsSet, _mmsEngineDBFacade,
                    _mmsStorage, _encodersLoadBalancer,
					faceRecognitionNumber, maxFaceRecognitionNumber,
                    #ifdef __LOCALENCODER__
                        &_runningEncodingsNumber,
                    #endif
                    _logger);
        }
        
        for (EncodingJob& encodingJob: _mediumPriorityEncodingJobs)
        {
            encodingJob._encoderVideoAudioProxy.init(
                lastProxyIdentifier++, &_mtEncodingJobs,
                    _configuration, multiEventsSet, _mmsEngineDBFacade,
                    _mmsStorage, _encodersLoadBalancer,
					faceRecognitionNumber, maxFaceRecognitionNumber,
                    #ifdef __LOCALENCODER__
                        &_runningEncodingsNumber,
                    #endif
                    _logger);
        }

        for (EncodingJob& encodingJob: _highPriorityEncodingJobs)
        {
            encodingJob._encoderVideoAudioProxy.init(
                lastProxyIdentifier++, &_mtEncodingJobs,
                    _configuration, multiEventsSet, _mmsEngineDBFacade,
                    _mmsStorage, _encodersLoadBalancer,
					faceRecognitionNumber, maxFaceRecognitionNumber,
                    #ifdef __LOCALENCODER__
                        &_runningEncodingsNumber,
                    #endif
                    _logger);
        }
    }                        
}

ActiveEncodingsManager::~ActiveEncodingsManager()
{
}

void ActiveEncodingsManager::operator()()
{
    bool shutdown = false;
    
    chrono::seconds secondsToBlock(5);
    
    vector<MMSEngineDBFacade::EncodingPriority> sortedEncodingPriorities = { 
        MMSEngineDBFacade::EncodingPriority::High,
        MMSEngineDBFacade::EncodingPriority::Medium,
        MMSEngineDBFacade::EncodingPriority::Low
    };
        
    while (!shutdown)
    {
        try
        {
            unique_lock<mutex>  locker(_mtEncodingJobs);

            // _logger->info("Reviewing current Encoding Jobs...");

            _cvAddedEncodingJob.wait_for(locker, secondsToBlock);
            /*
            if (_cvAddedEncodingJob.wait_for(locker, secondsToBlock) == cv_status::timeout)
            {
                // time expired

                continue;
            }
             */

			chrono::system_clock::time_point startEvent = chrono::system_clock::now();

			_logger->info(__FILEREF__ + "Begin checking encodingJobs");

            for (MMSEngineDBFacade::EncodingPriority encodingPriority: sortedEncodingPriorities)
            {
                EncodingJob*    encodingJobs;
                int             maxEncodingsToBeManaged;

                if (encodingPriority == MMSEngineDBFacade::EncodingPriority::High)
                {
                    // _logger->info(__FILEREF__ + "Processing the high encodings...");

                    encodingJobs            = _highPriorityEncodingJobs;
                    maxEncodingsToBeManaged = MAXHIGHENCODINGSTOBEMANAGED;
                }
                else if (encodingPriority == MMSEngineDBFacade::EncodingPriority::Medium)
                {
                    // _logger->info(__FILEREF__ + "Processing the default encodings...");

                    encodingJobs = _mediumPriorityEncodingJobs;
                    maxEncodingsToBeManaged = MAXMEDIUMENCODINGSTOBEMANAGED;
                }
                else // if (encodingPriority == MMSEngineDBFacade::EncodingPriority::Low)
                {
                    // _logger->info(__FILEREF__ + "Processing the low encodings...");

                    encodingJobs = _lowPriorityEncodingJobs;
                    maxEncodingsToBeManaged = MAXLOWENCODINGSTOBEMANAGED;
                }

                for (int encodingJobIndex = 0; encodingJobIndex < maxEncodingsToBeManaged; encodingJobIndex++)
                {
                    EncodingJob* encodingJob = &(encodingJobs[encodingJobIndex]);

                    if (encodingJob->_status == EncoderVideoAudioProxy::EncodingJobStatus::Free)
                        continue;
                    else if (encodingJob->_status == EncoderVideoAudioProxy::EncodingJobStatus::Running
						|| encodingJob->_status == EncoderVideoAudioProxy::EncodingJobStatus::GoingToRun)
                    {
						/*
						// We will start to check the encodingProgress after at least XXX seconds.
						// This is because the status is set to EncodingJobStatus::Running as soon as it is created
						// the encoderVideoAudioProxyThread thread. Many times the thread returns soon because
						// of 'No encoding available' and in this case getEncodingProgress will return 'No encoding job key found'

                        if (chrono::system_clock::now() - encodingJob->_encodingJobStart >= chrono::seconds(secondsToBlock))

						2019-03-31: Above commented because it was introduced the GoingToRun status
						*/
						if (encodingJob->_status == EncoderVideoAudioProxy::EncodingJobStatus::Running)
                        {
                            try
                            {
                                int encodingPercentage =
									encodingJob->_encoderVideoAudioProxy
									.getEncodingProgress(/* encodingJob->_encodingItem */);

								_logger->info(__FILEREF__ + "updateEncodingJobProgress"
										+ ", encodingJobKey: "
											+ to_string(encodingJob->_encodingItem->_encodingJobKey)
										+ ", encodingPercentage: " + to_string(encodingPercentage)
										);
                                _mmsEngineDBFacade->updateEncodingJobProgress (encodingJob->_encodingItem->_encodingJobKey, 
                                    encodingPercentage);
                            }
                            catch(FFMpegEncodingStatusNotAvailable e)
                            {

                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "getEncodingProgress failed"
                                    + ", runtime_error: " + e.what()
                                );
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "getEncodingProgress failed");
                            }
                        }

						if (encodingJob->_encodingItem->_encodingType !=
							MMSEngineDBFacade::EncodingType::LiveRecorder
							&& chrono::duration_cast<chrono::hours>(
								chrono::system_clock::now() - encodingJob->_encodingJobStart) >
								chrono::hours(24))
						{
							_logger->error(__FILEREF__ + "EncodingJob is not finishing"
								+ ", @MMS statistics@ - elapsed (hours): @" + to_string(chrono::duration_cast<chrono::hours>(
									chrono::system_clock::now() - encodingJob->_encodingJobStart).count()) + "@"
								+ ", workspace: " + encodingJob->_encodingItem->_workspace->_name
								+ ", _ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
								+ ", _encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
								+ ", _encodingPriority: " + to_string(static_cast<int>(encodingJob->_encodingItem->_encodingPriority))
								+ ", _encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
								+ ", _encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
								+ ", encodingJob->_status: " + EncoderVideoAudioProxy::toString(encodingJob->_status)
							);
						}
						else
						{
							_logger->info(__FILEREF__ + "EncodingJob still running"
								+ "elapsed (minutes): " + 
									to_string(chrono::duration_cast<chrono::minutes>(chrono::system_clock::now() - encodingJob->_encodingJobStart).count())
								+ ", workspace: " + encodingJob->_encodingItem->_workspace->_name
								+ ", _ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
								+ ", _encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
								+ ", _encodingPriority: " + to_string(static_cast<int>(encodingJob->_encodingItem->_encodingPriority))
								+ ", _encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
								+ ", _encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
								+ ", encodingJob->_status: " + EncoderVideoAudioProxy::toString(encodingJob->_status)
							);
						}
                    }
                    else // if (encodingJob._status == EncoderVideoAudioProxy::EncodingJobStatus::ToBeRun)
                    {
                        chrono::system_clock::time_point        processingItemStart = chrono::system_clock::now();

                        _logger->info(__FILEREF__ + "processEncodingJob begin"
                                + ", workspace: " + encodingJob->_encodingItem->_workspace->_name
                                + ", _ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
                                + ", _encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
                                + ", _encodingPriority: " + to_string(static_cast<int>(encodingJob->_encodingItem->_encodingPriority))
                                + ", _encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
                                + ", _encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
                        );

                        try
                        {
                            processEncodingJob(encodingJob);
                            
                            _logger->info(__FILEREF__ + "processEncodingJob done"
                                + ", @MMS statistics@ - elapsed (seconds): @" + 
                                    to_string(chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - processingItemStart).count()) + "@"
                                + ", workspace: " + encodingJob->_encodingItem->_workspace->_name
                                + ", _ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
                                + ", _encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
                                + ", _encodingPriority: " + to_string(static_cast<int>(encodingJob->_encodingItem->_encodingPriority))
                                + ", _encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
                                + ", _encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
                            );
                        }
                        catch(runtime_error e)
                        {
                            _logger->error(__FILEREF__ + "processEncodingJob failed"
                                + ", @MMS statistics@ - elapsed (seconds): @" + 
                                    to_string(chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - processingItemStart).count()) + "@"
                                + ", workspace: " + encodingJob->_encodingItem->_workspace->_name
                                + ", _ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
                                + ", _encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
                                + ", _encodingPriority: " + to_string(static_cast<int>(encodingJob->_encodingItem->_encodingPriority))
                                + ", _encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
                                + ", _encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
                                + ", runtime_error: " + e.what()
                            );
                        }
                        catch(exception e)
                        {
                            _logger->error(__FILEREF__ + "processEncodingJob failed"
                                + ", @MMS statistics@ - elapsed (seconds): @" + 
                                    to_string(chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - processingItemStart).count()) + "@"
                                + ", workspace: " + encodingJob->_encodingItem->_workspace->_name
                                + ", _ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
                                + ", _encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
                                + ", _encodingPriority: " + to_string(static_cast<int>(encodingJob->_encodingItem->_encodingPriority))
                                + ", _encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
                                + ", _encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
							);
                        }
                    }
                }
            }

			chrono::system_clock::time_point endEvent = chrono::system_clock::now();
			long elapsedInSeconds = chrono::duration_cast<chrono::seconds>(endEvent - startEvent).count();
			_logger->info(__FILEREF__ + "End checking encodingJobs"
				+ ", @MMS statistics@ - elapsed in seconds: @" + to_string(elapsedInSeconds) + "@"
			);
        }
        catch(exception e)
        {
            _logger->info(__FILEREF__ + "ActiveEncodingsManager loop failed");
        }
    }
}

void ActiveEncodingsManager::processEncodingJob(EncodingJob* encodingJob)
{
	chrono::system_clock::time_point startEvent = chrono::system_clock::now();

    if (encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::EncodeImage
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::EncodeVideoAudio
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::OverlayImageOnVideo
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::OverlayTextOnVideo
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::GenerateFrames
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::SlideShow
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::FaceRecognition
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::FaceIdentification
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::LiveRecorder
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::VideoSpeed
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::PictureInPicture
		|| encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::LiveProxy
	)
    {
        encodingJob->_encoderVideoAudioProxy.setEncodingData(
            &(encodingJob->_status),
            encodingJob->_encodingItem
        );
        
        _logger->info(__FILEREF__ + "Creating encoderVideoAudioProxy thread"
            + ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
            + ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
            + ", encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
        );
        thread encoderVideoAudioProxyThread(ref(encodingJob->_encoderVideoAudioProxy));
        encoderVideoAudioProxyThread.detach();
        
        // the lock guarantees us that the _ejsStatus is not updated
        // before the below _ejsStatus setting
        encodingJob->_encodingJobStart		= chrono::system_clock::now();
        encodingJob->_status			= EncoderVideoAudioProxy::EncodingJobStatus::GoingToRun;
    }
	/*
    else if (encodingJob->_encodingItem->_encodingType == MMSEngineDBFacade::EncodingType::EncodeImage)
    {
        string stagingEncodedAssetPathName;
        try
        {
            stagingEncodedAssetPathName = encodeContentImage(encodingJob->_encodingItem);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "encodeContentImage: " + e.what());

			try
			{
				_logger->info(__FILEREF__ + "updateEncodingJob PunctualError"
					+ ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
					+ ", encodingJob->_encodingItem->_ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
					+ ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
					+ ", encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
				);

				int64_t encodedPhysicalPathKey = -1;
            
				// PunctualError is used because, in case it always happens, the encoding will never reach a final state
				int encodingFailureNumber = _mmsEngineDBFacade->updateEncodingJob (
                    encodingJob->_encodingItem->_encodingJobKey,
                    MMSEngineDBFacade::EncodingError::PunctualError,    // ErrorBeforeEncoding, 
                    encodingJob->_encodingItem->_encodeData->_mediaItemKey, encodedPhysicalPathKey,
                    encodingJob->_encodingItem->_ingestionJobKey);
			}
			catch(...)
			{
				_logger->error(__FILEREF__ + "updateEncodingJob PunctualError FAILED"
					+ ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
					+ ", encodingJob->_encodingItem->_ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
					+ ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
					+ ", encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
				);
			}

            encodingJob->_status = EncoderVideoAudioProxy::EncodingJobStatus::Free;

            // throw e;
            return;
        }

        int64_t encodedPhysicalPathKey;
        try
        {
            encodedPhysicalPathKey = processEncodedImage(encodingJob->_encodingItem, 
                    stagingEncodedAssetPathName);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "processEncodedImage: " + e.what()
					+ ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
					+ ", encodingJob->_encodingItem->_ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
					+ ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
				);

			bool exceptionInCaseOfError = false;
            _logger->error(__FILEREF__ + "Remove"
                + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            );
            FileIO::remove(stagingEncodedAssetPathName, exceptionInCaseOfError);

			try
			{
				_logger->info(__FILEREF__ + "updateEncodingJob PunctualError"
					+ ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
					+ ", encodingJob->_encodingItem->_ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
					+ ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
					+ ", encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
				);

				encodedPhysicalPathKey = -1;
            
				// PunctualError is used because, in case it always happens, the encoding will never reach a final state
				int encodingFailureNumber = _mmsEngineDBFacade->updateEncodingJob (
                    encodingJob->_encodingItem->_encodingJobKey, 
                    MMSEngineDBFacade::EncodingError::PunctualError,    // ErrorBeforeEncoding, 
                    encodingJob->_encodingItem->_encodeData->_mediaItemKey, encodedPhysicalPathKey,
                    encodingJob->_encodingItem->_ingestionJobKey);
			}
			catch(...)
			{
				_logger->error(__FILEREF__ + "updateEncodingJob PunctualError FAILED"
					+ ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
					+ ", encodingJob->_encodingItem->_ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
					+ ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
					+ ", encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
				);
			}

            encodingJob->_status = EncoderVideoAudioProxy::EncodingJobStatus::Free;

            // throw e;
            return;
        }

        try
        {
            _logger->info(__FILEREF__ + "updateEncodingJob NoError"
                + ", encodingJob->_encodingItem->_encodingJobKey: " + to_string(encodingJob->_encodingItem->_encodingJobKey)
                + ", encodingJob->_encodingItem->_ingestionJobKey: " + to_string(encodingJob->_encodingItem->_ingestionJobKey)
                + ", encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
                + ", encodingParameters: " + encodingJob->_encodingItem->_encodingParameters
            );

            _mmsEngineDBFacade->updateEncodingJob (
                encodingJob->_encodingItem->_encodingJobKey, 
                MMSEngineDBFacade::EncodingError::NoError, 
                encodingJob->_encodingItem->_encodeData->_mediaItemKey, encodedPhysicalPathKey,
                encodingJob->_encodingItem->_ingestionJobKey);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "updateEncodingJob failed: " + e.what());

            encodingJob->_status = EncoderVideoAudioProxy::EncodingJobStatus::Free;

            // throw e;
            return;
        }

        encodingJob->_status = EncoderVideoAudioProxy::EncodingJobStatus::Free;
    }
	*/
    else
    {
        string errorMessage = __FILEREF__ + "Encoding not managed for the EncodingType"
                + ", encodingJob->_encodingItem->_encodingType: " + MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
                ;
        _logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }

	chrono::system_clock::time_point endEvent = chrono::system_clock::now();
	long elapsedInSeconds = chrono::duration_cast<chrono::seconds>(endEvent - startEvent).count();
	_logger->warn(__FILEREF__ + "processEncodingJob"
		+ ", encodingJob->_encodingItem->_encodingType: "
			+ MMSEngineDBFacade::toString(encodingJob->_encodingItem->_encodingType)
		+ ", @MMS statistics@ - elapsed in seconds: @" + to_string(elapsedInSeconds) + "@"
	);
}

/*
string ActiveEncodingsManager::encodeContentImage(
        shared_ptr<MMSEngineDBFacade::EncodingItem> encodingItem)
{
    int64_t sourcePhysicalPathKey;
    int64_t encodingProfileKey;    

    {
        string field = "sourcePhysicalPathKey";
        sourcePhysicalPathKey = encodingItem->_parametersRoot.get(field, 0).asInt64();

        field = "encodingProfileKey";
        encodingProfileKey = encodingItem->_parametersRoot.get(field, 0).asInt64();
    }

    size_t extensionIndex = encodingItem->_encodeData->_fileName.find_last_of(".");
    if (extensionIndex == string::npos)
    {
        string errorMessage = __FILEREF__ + "No extension find in the asset file name"
                + ", encodingItem->_encodeData->_fileName: " + encodingItem->_encodeData->_fileName;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }
    string encodedFileName =
            encodingItem->_encodeData->_fileName.substr(0, extensionIndex)
            + "_" 
            + to_string(encodingProfileKey);
    
    string mmsSourceAssetPathName;
	tuple<string, string, string, int64_t, string> physicalPathFileNameSizeInBytesAndDeliveryFileName =
		_mmsStorage->getPhysicalPath(sourcePhysicalPathKey);
	tie(mmsSourceAssetPathName, ignore, ignore, ignore, ignore)
		= physicalPathFileNameSizeInBytesAndDeliveryFileName;

    string          stagingEncodedAssetPathName;
    
    // added the check of the file size is zero because in this case the
    // magick library cause the crash of the xmms engine
    {
        bool inCaseOfLinkHasItToBeRead = false;
        unsigned long ulFileSize = FileIO::getFileSizeInBytes (
            mmsSourceAssetPathName, inCaseOfLinkHasItToBeRead);
        if (ulFileSize == 0)
        {
            string errorMessage = __FILEREF__ + "source image file size is zero"
                + ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
            ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
    }
        
    string                      newImageFormat;
    int                         newWidth;
    int                         newHeight;
    bool                        newAspectRatio;
    string                      sNewInterlaceType;
    Magick::InterlaceType       newInterlaceType;

    readingImageProfile(encodingItem->_encodeData->_jsonProfile,
            newImageFormat, newWidth, newHeight, newAspectRatio, sNewInterlaceType, newInterlaceType);

    try
    {
        Magick:: Image      imageToEncode;
        
        imageToEncode. read (mmsSourceAssetPathName.c_str());

        string currentImageFormat = imageToEncode.magick ();
        
        if (currentImageFormat == "jpeg")
            currentImageFormat = "JPG";

        int currentWidth	= imageToEncode. columns ();
        int currentHeight	= imageToEncode. rows ();

        _logger->info(__FILEREF__ + "Image processing"
            + ", encodingProfileKey: " + to_string(encodingProfileKey)
            + ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
            + ", currentImageFormat: " + currentImageFormat
            + ", currentWidth: " + to_string(currentWidth)
            + ", currentHeight: " + to_string(currentHeight)
            + ", newImageFormat: " + newImageFormat
            + ", newWidth: " + to_string(newWidth)
            + ", newHeight: " + to_string(newHeight)
            + ", newAspectRatio: " + to_string(newAspectRatio)
            + ", sNewInterlace: " + sNewInterlaceType
        );
        
        if (currentImageFormat == newImageFormat
            && currentWidth == newWidth
            && currentHeight == newHeight)
        {
            // same as the ingested content. Just copy the content
            
            encodedFileName.append(encodingItem->_encodeData->_fileName.substr(extensionIndex));

            bool removeLinuxPathIfExist = true;
			bool neededForTranscoder = false;
            stagingEncodedAssetPathName = _mmsStorage->getStagingAssetPathName(
					neededForTranscoder,
                encodingItem->_workspace->_directoryName,
                to_string(encodingItem->_encodingJobKey),
                encodingItem->_encodeData->_relativePath,
                encodedFileName,
                -1, // _encodingItem->_mediaItemKey, not used because encodedFileName is not ""
                -1, // _encodingItem->_physicalPathKey, not used because encodedFileName is not ""
                removeLinuxPathIfExist);

            FileIO::copyFile (mmsSourceAssetPathName, stagingEncodedAssetPathName);
        }
        else
        {
            if (newImageFormat == "JPG")
            {
                imageToEncode. magick ("JPEG");                
                encodedFileName.append(".jpg");
            }
            else if (newImageFormat == "GIF")
            {
                imageToEncode. magick ("GIF");                
                encodedFileName.append(".gif");
            }
            else if (newImageFormat == "PNG")
            {
                imageToEncode. magick ("PNG");
                imageToEncode. depth (8);
                encodedFileName.append(".png");
            }

            bool removeLinuxPathIfExist = true;
			bool neededForTranscoder = false;
            stagingEncodedAssetPathName = _mmsStorage->getStagingAssetPathName(
				neededForTranscoder,
                encodingItem->_workspace->_directoryName,
                to_string(encodingItem->_encodingJobKey),
                "/",    // encodingItem->_encodeData->_relativePath,
                encodedFileName,
                -1, // _encodingItem->_mediaItemKey, not used because encodedFileName is not ""
                -1, // _encodingItem->_physicalPathKey, not used because encodedFileName is not ""
                removeLinuxPathIfExist);
            
            Magick:: Geometry	newGeometry (newWidth, newHeight);

            // if Aspect is true the proportion are not mantained
            // if Aspect is false the proportion are mantained
            newGeometry. aspect (newAspectRatio);

            // if ulAspect is false, it means the aspect is preserved,
            // the width is fixed and the height will be calculated

            // also 'scale' could be used
            imageToEncode.scale (newGeometry);
            imageToEncode.interlaceType (newInterlaceType);
            imageToEncode.write(stagingEncodedAssetPathName);
        }
    }
    catch (Magick::Error &e)
    {
        _logger->info(__FILEREF__ + "ImageMagick exception"
            + ", e.what(): " + e.what()
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
        );
        
        throw e;
    }
    catch (exception e)
    {
        _logger->info(__FILEREF__ + "ImageMagick exception"
            + ", e.what(): " + e.what()
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
        );
        
        throw e;
    }
        
    return stagingEncodedAssetPathName;
}

int64_t ActiveEncodingsManager::processEncodedImage(
        shared_ptr<MMSEngineDBFacade::EncodingItem> encodingItem,
        string stagingEncodedAssetPathName)
{
    int64_t sourcePhysicalPathKey;
    int64_t encodingProfileKey;    

    {
        string field = "sourcePhysicalPathKey";
        sourcePhysicalPathKey = encodingItem->_parametersRoot.get(field, 0).asInt64();

        field = "encodingProfileKey";
        encodingProfileKey = encodingItem->_parametersRoot.get(field, 0).asInt64();
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

    int imageWidth = -1;
    int imageHeight = -1;
    string imageFormat;
    int imageQuality = -1;
    try
    {
        _logger->info(__FILEREF__ + "Processing through Magick"
            + ", ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );
        Magick::Image      imageToEncode;

        imageToEncode.read (stagingEncodedAssetPathName.c_str());

        imageWidth	= imageToEncode.columns();
        imageHeight	= imageToEncode.rows();
        imageFormat = imageToEncode.magick();
        imageQuality = imageToEncode.quality();
    }
    catch( Magick::WarningCoder &e )
    {
        // Process coder warning while loading file (e.g. TIFF warning)
        // Maybe the user will be interested in these warnings (or not).
        // If a warning is produced while loading an image, the image
        // can normally still be used (but not if the warning was about
        // something important!)
        _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw e;
    }
    catch( Magick::Warning &e )
    {
        _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw e;
    }
    catch( Magick::ErrorFileOpen &e ) 
    { 
        _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw e;
    }
    catch (Magick::Error &e)
    { 
        _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
            + ", e.what(): " + e.what()
        );

        throw e;
    }    
    
    int64_t encodedPhysicalPathKey;
    string encodedFileName;
    string mmsAssetPathName;
    unsigned long mmsPartitionIndexUsed;
    try
    {
        size_t fileNameIndex = stagingEncodedAssetPathName.find_last_of("/");
        if (fileNameIndex == string::npos)
        {
            string errorMessage = __FILEREF__ + "No fileName find in the asset path name"
                    + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        encodedFileName = stagingEncodedAssetPathName.substr(fileNameIndex + 1);

        bool partitionIndexToBeCalculated = true;
        bool deliveryRepositoriesToo = true;

        mmsAssetPathName = _mmsStorage->moveAssetInMMSRepository(
            stagingEncodedAssetPathName,
            encodingItem->_workspace->_directoryName,
            encodedFileName,
            encodingItem->_encodeData->_relativePath,

            partitionIndexToBeCalculated,
            &mmsPartitionIndexUsed, // OUT if bIsPartitionIndexToBeCalculated is true, IN is bIsPartitionIndexToBeCalculated is false

            deliveryRepositoriesToo,
            encodingItem->_workspace->_territories
        );
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

        throw e;
    }

	// remove staging directory
	{
		string directoryPathName;
		try
		{
			size_t endOfDirectoryIndex = stagingEncodedAssetPathName.find_last_of("/");
			if (endOfDirectoryIndex != string::npos)
			{
				directoryPathName = stagingEncodedAssetPathName.substr(0,
						endOfDirectoryIndex);

				_logger->info(__FILEREF__ + "removeDirectory"
					+ ", directoryPathName: " + directoryPathName
				);
				Boolean_t bRemoveRecursively = true;
				FileIO::removeDirectory(directoryPathName, bRemoveRecursively);
			}
		}
		catch(runtime_error e)
		{
			_logger->error(__FILEREF__ + "removeDirectory failed"
				+ ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
				+ ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
				+ ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
				+ ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
				+ ", directoryPathName: " + directoryPathName
				+ ", exception: " + e.what()
			);
		}
	}

    try
    {
        unsigned long long mmsAssetSizeInBytes;
        {
            bool inCaseOfLinkHasItToBeRead = false;
            mmsAssetSizeInBytes = FileIO::getFileSizeInBytes(mmsAssetPathName,
                    inCaseOfLinkHasItToBeRead);   
        }

		bool externalReadOnlyStorage = false;
		string externalDeliveryTechnology;
		string externalDeliveryURL;
		int64_t liveRecordingIngestionJobKey = -1;
        encodedPhysicalPathKey = _mmsEngineDBFacade->saveVariantContentMetadata(
            encodingItem->_workspace->_workspaceKey,
			encodingItem->_ingestionJobKey,
			liveRecordingIngestionJobKey,
            encodingItem->_encodeData->_mediaItemKey,
			externalReadOnlyStorage,
			externalDeliveryTechnology,
			externalDeliveryURL,
            encodedFileName,
            encodingItem->_encodeData->_relativePath,
            mmsPartitionIndexUsed,
            mmsAssetSizeInBytes,
            encodingProfileKey,
                
            durationInMilliSeconds,
            bitRate,
            videoCodecName,
            videoProfile,
            videoWidth,
            videoHeight,
            videoAvgFrameRate,
            videoBitRate,
            audioCodecName,
            audioSampleRate,
            audioChannels,
            audioBitRate,

            imageWidth,
            imageHeight,
            imageFormat,
            imageQuality
                );
        
        _logger->info(__FILEREF__ + "Saved the Encoded content"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", encodedPhysicalPathKey: " + to_string(encodedPhysicalPathKey)
        );
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->saveVariantContentMetadata failed"
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            + ", stagingEncodedAssetPathName: " + stagingEncodedAssetPathName
        );

		bool exceptionInCaseOfErr = false;
        _logger->info(__FILEREF__ + "Remove"
            + ", mmsAssetPathName: " + mmsAssetPathName
        );
        FileIO::remove(mmsAssetPathName, exceptionInCaseOfErr);

        throw e;
    }
    
    return encodedPhysicalPathKey;
}
*/

void ActiveEncodingsManager::addEncodingItem(shared_ptr<MMSEngineDBFacade::EncodingItem> encodingItem)
{
    
    EncodingJob*    encodingJobs;
    int             maxEncodingsToBeManaged;

    if (encodingItem->_encodingPriority == MMSEngineDBFacade::EncodingPriority::High)
    {
        encodingJobs            = _highPriorityEncodingJobs;
        maxEncodingsToBeManaged = MAXHIGHENCODINGSTOBEMANAGED;
    }
    else if (encodingItem->_encodingPriority == MMSEngineDBFacade::EncodingPriority::Medium)
    {
        encodingJobs = _mediumPriorityEncodingJobs;
        maxEncodingsToBeManaged = MAXMEDIUMENCODINGSTOBEMANAGED;
    }
    else // if (encodingItem->_encodingPriority == MMSEngineDBFacade::EncodingPriority::Low)
    {
        encodingJobs = _lowPriorityEncodingJobs;
        maxEncodingsToBeManaged = MAXLOWENCODINGSTOBEMANAGED;
    }
    
    int encodingJobIndex;
    for (encodingJobIndex = 0; encodingJobIndex < maxEncodingsToBeManaged; encodingJobIndex++)
    {
        if ((encodingJobs [encodingJobIndex])._status == EncoderVideoAudioProxy::EncodingJobStatus::Free)
        {
            (encodingJobs [encodingJobIndex])._status	= EncoderVideoAudioProxy::EncodingJobStatus::ToBeRun;
            (encodingJobs [encodingJobIndex])._encodingJobStart	= chrono::system_clock::now();
            (encodingJobs [encodingJobIndex])._encodingItem		= encodingItem;
            // (encodingJobs [encodingJobIndex])._petEncodingThread			= (void *) NULL;

            break;
        }
    }
    
    if (encodingJobIndex == maxEncodingsToBeManaged)
    {
        _logger->warn(__FILEREF__ + "Max Encodings Manager capacity reached");
        
        throw MaxEncodingsManagerCapacityReached();
    }
    
    _logger->info(__FILEREF__ + "Encoding Job Key added"
        + ", encodingItem->_workspace->_name: " + encodingItem->_workspace->_name
        + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
        + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
    );
}

unsigned long ActiveEncodingsManager:: addEncodingItems (
	std:: vector<shared_ptr<MMSEngineDBFacade::EncodingItem>>& vEncodingItems)
{
    unsigned long       ulEncodingsNumberAdded = 0;
    lock_guard<mutex>   locker(_mtEncodingJobs);

	int encodingItemIndex = 0;
	int encodingItemsNumber = vEncodingItems.size();
    for (shared_ptr<MMSEngineDBFacade::EncodingItem> encodingItem: vEncodingItems)
    {
        _logger->info(__FILEREF__ + "Adding Encoding Item "
				+ to_string(encodingItemIndex) + "/" + to_string(encodingItemsNumber)
            + ", encodingItem->_workspace->_name: " + encodingItem->_workspace->_name
            + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
            + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
            + ", encodingItem->_encodingPriority: " + to_string(static_cast<int>(encodingItem->_encodingPriority))
            + ", encodingItem->_encodingType: " + MMSEngineDBFacade::toString(encodingItem->_encodingType)
            + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
        );

        try
        {
            addEncodingItem (encodingItem);
            ulEncodingsNumberAdded++;
        }
        catch(MaxEncodingsManagerCapacityReached e)
        {
            _logger->info(__FILEREF__ + "Max Encodings Manager Capacity reached "
				+ to_string(encodingItemIndex) + "/" + to_string(encodingItemsNumber)
                + ", encodingItem->_workspace->_name: " + encodingItem->_workspace->_name
                + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
                + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
                + ", encodingItem->_encodingPriority: " + to_string(static_cast<int>(encodingItem->_encodingPriority))
                + ", encodingItem->_encodingType: " + MMSEngineDBFacade::toString(encodingItem->_encodingType)
                + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            );
            
            int64_t mediaItemKey = -1;
            int64_t encodedPhysicalPathKey = -1;
            _mmsEngineDBFacade->updateEncodingJob (encodingItem->_encodingJobKey,
                    MMSEngineDBFacade::EncodingError::MaxCapacityReached, 
                    mediaItemKey, encodedPhysicalPathKey,
                    encodingItem->_ingestionJobKey);
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "addEncodingItem failed "
				+ to_string(encodingItemIndex) + "/" + to_string(encodingItemsNumber)
                + ", encodingItem->_workspace->_name: " + encodingItem->_workspace->_name
                + ", encodingItem->_ingestionJobKey: " + to_string(encodingItem->_ingestionJobKey)
                + ", encodingItem->_encodingJobKey: " + to_string(encodingItem->_encodingJobKey)
                + ", encodingItem->_encodingPriority: " + to_string(static_cast<int>(encodingItem->_encodingPriority))
                + ", encodingItem->_encodingType: " + MMSEngineDBFacade::toString(encodingItem->_encodingType)
                + ", encodingItem->_encodingParameters: " + encodingItem->_encodingParameters
            );
            int64_t mediaItemKey = -1;
            int64_t encodedPhysicalPathKey = -1;
            _mmsEngineDBFacade->updateEncodingJob (encodingItem->_encodingJobKey, 
                MMSEngineDBFacade::EncodingError::ErrorBeforeEncoding,
                mediaItemKey, encodedPhysicalPathKey,
				encodingItem->_ingestionJobKey, "addEncodingItem failed");
        }

		encodingItemIndex++;
    }
    
    if (ulEncodingsNumberAdded > 0)
    {
        _cvAddedEncodingJob.notify_one();
    }

    return ulEncodingsNumberAdded;
}

/*
void ActiveEncodingsManager::readingImageProfile(
        string jsonProfile,
        string& newFormat,
        int& newWidth,
        int& newHeight,
        bool& newAspectRatio,
        string& sNewInterlaceType,
        Magick::InterlaceType& newInterlaceType
)
{
    string field;
    Json::Value encodingProfileRoot;
    try
    {
        Json::CharReaderBuilder builder;
        Json::CharReader* reader = builder.newCharReader();
        string errors;

        bool parsingSuccessful = reader->parse(jsonProfile.c_str(),
                jsonProfile.c_str() + jsonProfile.size(), 
                &encodingProfileRoot, &errors);
        delete reader;

        if (!parsingSuccessful)
        {
            string errorMessage = __FILEREF__ + "failed to parse the encoder details"
                    + ", errors: " + errors
                    + ", jsonProfile: " + jsonProfile
                    ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    catch(...)
    {
        throw runtime_error(string("wrong encoding profile json format")
                + ", jsonProfile: " + jsonProfile
                );
    }

    // FileFormat
    {
        field = "FileFormat";
        if (!_mmsEngineDBFacade->isMetadataPresent(encodingProfileRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        newFormat = encodingProfileRoot.get(field, "XXX").asString();

        encodingImageFormatValidation(newFormat);
    }

    Json::Value encodingProfileImageRoot;
    {
        field = "Image";
        if (!_mmsEngineDBFacade->isMetadataPresent(encodingProfileRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        encodingProfileImageRoot = encodingProfileRoot[field];
    }
    
    // Width
    {
        field = "Width";
        if (!_mmsEngineDBFacade->isMetadataPresent(encodingProfileImageRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        newWidth = encodingProfileImageRoot.get(field, "XXX").asInt();
    }

    // Height
    {
        field = "Height";
        if (!_mmsEngineDBFacade->isMetadataPresent(encodingProfileImageRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        newHeight = encodingProfileImageRoot.get(field, "XXX").asInt();
    }

    // Aspect
    {
        field = "AspectRatio";
        if (!_mmsEngineDBFacade->isMetadataPresent(encodingProfileImageRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        newAspectRatio = encodingProfileImageRoot.get(field, "XXX").asBool();
    }

    // Interlace
    {
        field = "InterlaceType";
        if (!_mmsEngineDBFacade->isMetadataPresent(encodingProfileImageRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        sNewInterlaceType = encodingProfileImageRoot.get(field, "XXX").asString();

        newInterlaceType = encodingImageInterlaceTypeValidation(sNewInterlaceType);
    }
}

void ActiveEncodingsManager::encodingImageFormatValidation(string newFormat)
{    
    auto logger = spdlog::get("mmsEngineService");
    if (newFormat != "JPG" 
            && newFormat != "GIF" 
            && newFormat != "PNG" 
            )
    {
        string errorMessage = __FILEREF__ + "newFormat is wrong"
                + ", newFormat: " + newFormat;

        logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
}

Magick::InterlaceType ActiveEncodingsManager::encodingImageInterlaceTypeValidation(string sNewInterlaceType)
{    
    auto logger = spdlog::get("mmsEngineService");
    Magick::InterlaceType       interlaceType;
    
    if (sNewInterlaceType == "NoInterlace")
        interlaceType       = Magick::NoInterlace;
    else if (sNewInterlaceType == "LineInterlace")
        interlaceType       = Magick::LineInterlace;
    else if (sNewInterlaceType == "PlaneInterlace")
        interlaceType       = Magick::PlaneInterlace;
    else if (sNewInterlaceType == "PartitionInterlace")
        interlaceType       = Magick::PartitionInterlace;
    else
    {
        string errorMessage = __FILEREF__ + "sNewInterlaceType is wrong"
                + ", sNewInterlaceType: " + sNewInterlaceType;

        logger->error(errorMessage);
        
        throw runtime_error(errorMessage);
    }
    
    return interlaceType;
}
*/

