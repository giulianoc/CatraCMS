
#include <stdio.h>

#include "JSONUtils.h"
#include <fstream>
#include <sstream>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include "catralibraries/System.h"
#include "catralibraries/Encrypt.h"
#include "catralibraries/ProcessUtility.h"
#include "catralibraries/Convert.h"
#include "PersistenceLock.h"
#include "FFMpeg.h"
#include "MMSEngineProcessor.h"
#include "CheckIngestionTimes.h"
#include "CheckEncodingTimes.h"
#include "ContentRetentionTimes.h"
#include "DBDataRetentionTimes.h"
#include "CheckRefreshPartitionFreeSizeTimes.h"
#include "MainAndBackupRunningHALiveRecordingEvent.h"
#include "UpdateLiveRecorderVODTimes.h"
#include "catralibraries/md5.h"
#include "EMailSender.h"
#include "Magick++.h"



MMSEngineProcessor::MMSEngineProcessor(
        int processorIdentifier,
        shared_ptr<spdlog::logger> logger, 
        shared_ptr<MultiEventsSet> multiEventsSet,
        shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade,
        shared_ptr<MMSStorage> mmsStorage,
        shared_ptr<long> processorsThreadsNumber,
        ActiveEncodingsManager* pActiveEncodingsManager,
        Json::Value configuration
)
{
    _processorIdentifier         = processorIdentifier;
    _logger             = logger;
    _configuration      = configuration;
    _multiEventsSet     = multiEventsSet;
    _mmsEngineDBFacade  = mmsEngineDBFacade;
    _mmsStorage         = mmsStorage;
    _processorsThreadsNumber = processorsThreadsNumber;
    _pActiveEncodingsManager = pActiveEncodingsManager;

    _processorMMS                   = System::getHostName();
    
    _processorThreads =  JSONUtils::asInt(configuration["mms"], "processorThreads", 1);
    _maxAdditionalProcessorThreads =  JSONUtils::asInt(configuration["mms"], "maxAdditionalProcessorThreads", 1);

    _maxDownloadAttemptNumber       = JSONUtils::asInt(configuration["download"], "maxDownloadAttemptNumber", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", download->maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
    );
    _progressUpdatePeriodInSeconds  = JSONUtils::asInt(configuration["download"], "progressUpdatePeriodInSeconds", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", download->progressUpdatePeriodInSeconds: " + to_string(_progressUpdatePeriodInSeconds)
    );
    _secondsWaitingAmongDownloadingAttempt  = JSONUtils::asInt(configuration["download"], "secondsWaitingAmongDownloadingAttempt", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", download->secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
    );
    
    _maxIngestionJobsPerEvent       = JSONUtils::asInt(configuration["mms"], "maxIngestionJobsPerEvent", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->maxIngestionJobsPerEvent: " + to_string(_maxIngestionJobsPerEvent)
    );
    _maxEncodingJobsPerEvent       = JSONUtils::asInt(configuration["mms"], "maxEncodingJobsPerEvent", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->maxEncodingJobsPerEvent: " + to_string(_maxEncodingJobsPerEvent)
    );

    _maxSecondsToWaitUpdateLiveRecorderVOD	= JSONUtils::asInt(configuration["mms"]["locks"],
		"maxSecondsToWaitUpdateLiveRecorderVOD", 10);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->maxSecondsToWaitUpdateLiveRecorderVOD: " + to_string(_maxSecondsToWaitUpdateLiveRecorderVOD)
    );

    _maxEventManagementTimeInSeconds       = JSONUtils::asInt(configuration["mms"], "maxEventManagementTimeInSeconds", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->maxEventManagementTimeInSeconds: " + to_string(_maxEventManagementTimeInSeconds)
    );

    _dependencyExpirationInHours        = JSONUtils::asInt(configuration["mms"], "dependencyExpirationInHours", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->dependencyExpirationInHours: " + to_string(_dependencyExpirationInHours)
    );

    _downloadChunkSizeInMegaBytes       = JSONUtils::asInt(configuration["download"], "downloadChunkSizeInMegaBytes", 5);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", download->downloadChunkSizeInMegaBytes: " + to_string(_downloadChunkSizeInMegaBytes)
    );
    
    _emailProtocol                      = _configuration["EmailNotification"].get("protocol", "").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", EmailNotification->protocol: " + _emailProtocol
    );
    _emailServer                        = _configuration["EmailNotification"].get("server", "").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", EmailNotification->server: " + _emailServer
    );
    _emailPort                          = JSONUtils::asInt(_configuration["EmailNotification"], "port", 0);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", EmailNotification->port: " + to_string(_emailPort)
    );
    _emailUserName                      = _configuration["EmailNotification"].get("userName", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", EmailNotification->userName: " + _emailUserName
    );
    string _emailPassword;
    {
        string encryptedPassword = _configuration["EmailNotification"].get("password", "XXX").asString();
        _emailPassword = Encrypt::decrypt(encryptedPassword);        
    }
    _emailFrom                          = _configuration["EmailNotification"].get("from", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", EmailNotification->from: " + _emailFrom
    );
    
    _facebookGraphAPIProtocol           = _configuration["FacebookGraphAPI"].get("protocol", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", FacebookGraphAPI->protocol: " + _facebookGraphAPIProtocol
    );
    _facebookGraphAPIHostName           = _configuration["FacebookGraphAPI"].get("hostName", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", FacebookGraphAPI->hostName: " + _facebookGraphAPIHostName
    );
    _facebookGraphAPIPort               = JSONUtils::asInt(_configuration["FacebookGraphAPI"], "port", 0);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", FacebookGraphAPI->port: " + to_string(_facebookGraphAPIPort)
    );
    _facebookGraphAPIVersion           = _configuration["FacebookGraphAPI"].get("version", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", FacebookGraphAPI->version: " + _facebookGraphAPIVersion
    );
    _facebookGraphAPITimeoutInSeconds   = JSONUtils::asInt(_configuration["FacebookGraphAPI"], "timeout", 0);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", FacebookGraphAPI->timeout: " + to_string(_facebookGraphAPITimeoutInSeconds)
    );

    _youTubeDataAPIProtocol           = _configuration["YouTubeDataAPI"].get("protocol", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->protocol: " + _youTubeDataAPIProtocol
    );
    _youTubeDataAPIHostName           = _configuration["YouTubeDataAPI"].get("hostName", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->hostName: " + _youTubeDataAPIHostName
    );
    _youTubeDataAPIPort               = JSONUtils::asInt(_configuration["YouTubeDataAPI"], "port", 0);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->port: " + to_string(_youTubeDataAPIPort)
    );
    _youTubeDataAPIRefreshTokenURI       = _configuration["YouTubeDataAPI"].get("refreshTokenURI", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->refreshTokenURI: " + _youTubeDataAPIRefreshTokenURI
    );
    _youTubeDataAPIUploadVideoURI       = _configuration["YouTubeDataAPI"].get("uploadVideoURI", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->uploadVideoURI: " + _youTubeDataAPIUploadVideoURI
    );
    _youTubeDataAPITimeoutInSeconds   = JSONUtils::asInt(_configuration["YouTubeDataAPI"], "timeout", 0);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->timeout: " + to_string(_youTubeDataAPITimeoutInSeconds)
    );
    _youTubeDataAPIClientId       = _configuration["YouTubeDataAPI"].get("clientId", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->clientId: " + _youTubeDataAPIClientId
    );
    _youTubeDataAPIClientSecret       = _configuration["YouTubeDataAPI"].get("clientSecret", "XXX").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", YouTubeDataAPI->clientSecret: " + _youTubeDataAPIClientSecret
    );

    _localCopyTaskEnabled               =  JSONUtils::asBool(_configuration["mms"], "localCopyTaskEnabled", false);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->localCopyTaskEnabled: " + to_string(_localCopyTaskEnabled)
    );

    _mmsAPIProtocol = _configuration["api"].get("protocol", "").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", api->protocol: " + _mmsAPIProtocol
    );
    _mmsAPIHostname = _configuration["api"].get("hostname", "").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", api->hostname: " + _mmsAPIHostname
    );
    _mmsAPIPort = JSONUtils::asInt(_configuration["api"], "port", 0);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", api->port: " + to_string(_mmsAPIPort)
    );
    _mmsAPIIngestionURI = _configuration["api"].get("ingestionURI", "").asString();
    _logger->info(__FILEREF__ + "Configuration item"
        + ", api->ingestionURI: " + _mmsAPIIngestionURI
    );

	_waitingNFSSync_attemptNumber = JSONUtils::asInt(configuration["storage"],
		"waitingNFSSync_attemptNumber", 1);
	_logger->info(__FILEREF__ + "Configuration item"
		+ ", storage->waitingNFSSync_attemptNumber: " + to_string(_waitingNFSSync_attemptNumber)
	);
	_waitingNFSSync_sleepTimeInSeconds = JSONUtils::asInt(configuration["storage"],
		"waitingNFSSync_sleepTimeInSeconds", 3);
	_logger->info(__FILEREF__ + "Configuration item"
		+ ", storage->waitingNFSSync_sleepTimeInSeconds: "
		+ to_string(_waitingNFSSync_sleepTimeInSeconds)
	);

    _liveRecorderVODImageMediaItemKey	= JSONUtils::asInt64(_configuration["mms"], "liveRecorderVODImage", -1);
    _logger->info(__FILEREF__ + "Configuration item"
        + ", mms->liveRecorderVODImage: " + to_string(_liveRecorderVODImageMediaItemKey)
    );

    if (_processorIdentifier == 0)
    {
        try
        {
            _mmsEngineDBFacade->resetProcessingJobsIfNeeded(_processorMMS);
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->resetProcessingJobsIfNeeded failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", exception: " + e.what()
            );

            throw e;
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "_mmsEngineDBFacade->resetProcessingJobsIfNeeded failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
            );

            throw e;
        }
    }
}

MMSEngineProcessor::~MMSEngineProcessor()
{
    
}

void MMSEngineProcessor::operator ()() 
{
    bool blocking = true;
    chrono::milliseconds milliSecondsToBlock(100);

    //SPDLOG_DEBUG(_logger , "Enabled only #ifdef SPDLOG_TRACE_ON..{} ,{}", 1, 3.23);
    // SPDLOG_TRACE(_logger , "Enabled only #ifdef SPDLOG_TRACE_ON..{} ,{}", 1, 3.23);
    _logger->info(__FILEREF__ + "MMSEngineProcessor thread started"
        + ", _processorIdentifier: " + to_string(_processorIdentifier)
    );

    bool endEvent = false;
    while(!endEvent)
    {
        shared_ptr<Event2> event = _multiEventsSet->getAndRemoveFirstEvent(
				MMSENGINEPROCESSORNAME, blocking, milliSecondsToBlock);
        if (event == nullptr)
        {
            // cout << "No event found or event not yet expired" << endl;

            continue;
        }

		chrono::system_clock::time_point startEvent = chrono::system_clock::now();

        switch(event->getEventKey().first)
        {
            case MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTIONEVENT:	// 1
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTION"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
                    handleCheckIngestionEvent ();
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleCheckIngestionEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTION"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT:	// 2
            {
                _logger->debug(__FILEREF__ + "1. Received LOCALASSETINGESTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent
					= dynamic_pointer_cast<LocalAssetIngestionEvent>(event);

                try
                {
					if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
					{
						_logger->warn(__FILEREF__
							+ "Not enough available threads to manage handleLocalAssetIngestionEvent, activity is postponed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
							+ ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
							+ ", _processorThreads + _maxAdditionalProcessorThreads: "
							+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
						);
            
						/*
						_logger->info(__FILEREF__ + "Threads finished, added a sleep because a new event istantly causes "
								+ "just more logs and file system full because of logs "
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
							+ ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
							+ ", _processorThreads + _maxAdditionalProcessorThreads: "
							+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
							+ ", _secondsWaitingWhenThreadsFinished: " + to_string(_secondsWaitingWhenThreadsFinished)
						);
						this_thread::sleep_for(chrono::seconds(_secondsWaitingWhenThreadsFinished));
						*/

						{
							shared_ptr<LocalAssetIngestionEvent>    cloneLocalAssetIngestionEvent
								= _multiEventsSet->getEventsFactory()->getFreeEvent<LocalAssetIngestionEvent>(
										MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

							cloneLocalAssetIngestionEvent->setSource(
								localAssetIngestionEvent->getSource());
							cloneLocalAssetIngestionEvent->setDestination(
								localAssetIngestionEvent->getDestination());
							/* 2019-11-15: it is important this message will expire later.
							 *	Before this change (+ 5 seconds), the event expires istantly and we have file system full "
							*	because of the two messages
							*	- Not enough available threads... and
							*	- addEvent: EVENT_TYPE...
							*/
							cloneLocalAssetIngestionEvent->setExpirationTimePoint(
								chrono::system_clock::now() + chrono::seconds(5));

							cloneLocalAssetIngestionEvent->setExternalReadOnlyStorage(
								localAssetIngestionEvent->getExternalReadOnlyStorage());
							cloneLocalAssetIngestionEvent->setExternalStorageMediaSourceURL(
								localAssetIngestionEvent->getExternalStorageMediaSourceURL());
							cloneLocalAssetIngestionEvent->setIngestionJobKey(
								localAssetIngestionEvent->getIngestionJobKey());
							cloneLocalAssetIngestionEvent->setIngestionSourceFileName(
								localAssetIngestionEvent->getIngestionSourceFileName());
							cloneLocalAssetIngestionEvent->setMMSSourceFileName(
								localAssetIngestionEvent->getMMSSourceFileName());
							cloneLocalAssetIngestionEvent->setForcedAvgFrameRate(
								localAssetIngestionEvent->getForcedAvgFrameRate());
							cloneLocalAssetIngestionEvent->setWorkspace(
								localAssetIngestionEvent->getWorkspace());
							cloneLocalAssetIngestionEvent->setIngestionType(
								localAssetIngestionEvent->getIngestionType());
							cloneLocalAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(
								localAssetIngestionEvent->getIngestionRowToBeUpdatedAsSuccess());

							cloneLocalAssetIngestionEvent->setMetadataContent(
								localAssetIngestionEvent->getMetadataContent());

							shared_ptr<Event2>    cloneEvent = dynamic_pointer_cast<Event2>(
									cloneLocalAssetIngestionEvent);
							_multiEventsSet->addEvent(cloneEvent);

							_logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", getEventKey().first: " + to_string(event->getEventKey().first)
								+ ", getEventKey().second: " + to_string(event->getEventKey().second));
						}
					}
					else
					{
						// handleLocalAssetIngestionEvent (localAssetIngestionEvent);
                        thread handleLocalAssetIngestionEventThread(&MMSEngineProcessor::handleLocalAssetIngestionEventThread,
								this, _processorsThreadsNumber, *localAssetIngestionEvent);
                        handleLocalAssetIngestionEventThread.detach();
					}
                }
                catch(runtime_error e)
                {
                    _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<LocalAssetIngestionEvent>(
						localAssetIngestionEvent);

                _logger->debug(__FILEREF__ + "2. Received LOCALASSETINGESTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_CHECKENCODINGEVENT:	// 3
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKENCODING"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
                    handleCheckEncodingEvent ();
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleCheckEncodingEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKENCODING"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_CONTENTRETENTIONEVENT:	// 4
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_CONTENTRETENTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
                    if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                    {
                        // content retention is a periodical event, we will wait the next one
                        
                        _logger->warn(__FILEREF__ + "Not enough available threads to manage handleContentRetentionEventThread, activity is postponed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                            + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                        );
                    }
                    else
                    {
                        thread contentRetention(&MMSEngineProcessor::handleContentRetentionEventThread, this,
                            _processorsThreadsNumber);
                        contentRetention.detach();
                    }
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleContentRetentionEventThread failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_CONTENTRETENTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_MULTILOCALASSETINGESTIONEVENT:	// 5
            {
                _logger->debug(__FILEREF__ + "1. Received MULTILOCALASSETINGESTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                shared_ptr<MultiLocalAssetIngestionEvent>    multiLocalAssetIngestionEvent
					= dynamic_pointer_cast<MultiLocalAssetIngestionEvent>(event);

                try
                {
					if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
					{
						_logger->warn(__FILEREF__
							+ "Not enough available threads to manage handleLocalAssetIngestionEvent, activity is postponed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent->getIngestionJobKey())
							+ ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
							+ ", _processorThreads + _maxAdditionalProcessorThreads: "
							+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
						);

						/*
						_logger->info(__FILEREF__ + "Threads finished, added a sleep because a new event istantly causes "
								+ "just more logs and file system full because of logs "
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent->getIngestionJobKey())
							+ ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
							+ ", _processorThreads + _maxAdditionalProcessorThreads: "
							+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
							+ ", _secondsWaitingWhenThreadsFinished: " + to_string(_secondsWaitingWhenThreadsFinished)
						);
						this_thread::sleep_for(chrono::seconds(_secondsWaitingWhenThreadsFinished));
						*/
            
						{
							shared_ptr<MultiLocalAssetIngestionEvent>    cloneMultiLocalAssetIngestionEvent
								= _multiEventsSet->getEventsFactory()->getFreeEvent<MultiLocalAssetIngestionEvent>(
										MMSENGINE_EVENTTYPEIDENTIFIER_MULTILOCALASSETINGESTIONEVENT);

							cloneMultiLocalAssetIngestionEvent->setSource(
								multiLocalAssetIngestionEvent->getSource());
							cloneMultiLocalAssetIngestionEvent->setDestination(
								multiLocalAssetIngestionEvent->getDestination());
							/* 2019-11-15: it is important this message will expire later.
							 *	Before this change (+ 5 seconds), the event expires istantly and we have file system full "
							*	because of the two messages
							*	- Not enough available threads... and
							*	- addEvent: EVENT_TYPE...
							*/
							cloneMultiLocalAssetIngestionEvent->setExpirationTimePoint(
								chrono::system_clock::now() + chrono::seconds(5));

							cloneMultiLocalAssetIngestionEvent->setIngestionJobKey(
								multiLocalAssetIngestionEvent->getIngestionJobKey());
							cloneMultiLocalAssetIngestionEvent->setEncodingJobKey(
								multiLocalAssetIngestionEvent->getEncodingJobKey());
							cloneMultiLocalAssetIngestionEvent->setWorkspace(
								multiLocalAssetIngestionEvent->getWorkspace());
							cloneMultiLocalAssetIngestionEvent->setParametersRoot(
								multiLocalAssetIngestionEvent->getParametersRoot());

							shared_ptr<Event2>    cloneEvent = dynamic_pointer_cast<Event2>(
									cloneMultiLocalAssetIngestionEvent);
							_multiEventsSet->addEvent(cloneEvent);

							_logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (MULTIINGESTASSETEVENT)"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", getEventKey().first: " + to_string(event->getEventKey().first)
								+ ", getEventKey().second: " + to_string(event->getEventKey().second));
						}
					}
					else
					{
						// handleMultiLocalAssetIngestionEvent (multiLocalAssetIngestionEvent);
                        thread handleMultiLocalAssetIngestionEventThread(
								&MMSEngineProcessor::handleMultiLocalAssetIngestionEventThread, this,
                            _processorsThreadsNumber, *multiLocalAssetIngestionEvent);
                        handleMultiLocalAssetIngestionEventThread.detach();
					}
                }
                catch(runtime_error e)
                {
                    _logger->error(__FILEREF__ + "handleMultiLocalAssetIngestionEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleMultiLocalAssetIngestionEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<MultiLocalAssetIngestionEvent>(multiLocalAssetIngestionEvent);

                _logger->debug(__FILEREF__ + "2. Received MULTILOCALASSETINGESTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_MAINANDBACKUPRUNNINGHALIVERECORDINGEVENT:	// 6
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_MAINANDBACKUPRUNNINGHALIVERECORDINGEVENT:"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
                    if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                    {
                        // main and backup running HA live recording is a periodical event, we will wait the next one
                        
                        _logger->warn(__FILEREF__ + "Not enough available threads to manage handleMainAndBackupOfRunnungLiveRecordingHA, activity is postponed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                            + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                        );
                    }
                    else
                    {
                        thread mainAndBackupOfRunnungLiveRecordingHA(&MMSEngineProcessor::handleMainAndBackupOfRunnungLiveRecordingHA, this,
                            _processorsThreadsNumber);
                        mainAndBackupOfRunnungLiveRecordingHA.detach();
                    }
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleMainAndBackupOfRunnungLiveRecordingHA failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_MAINANDBACKUPRUNNINGHALIVERECORDINGEVENT:"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_DBDATARETENTIONEVENT:	// 7
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_DBDATARETENTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
					/* 2019-07-10: this check was removed since this event happens once a day
                    if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                    {
                        // content retention is a periodical event, we will wait the next one
                        
                        _logger->warn(__FILEREF__ + "Not enough available threads to manage handleContentRetentionEventThread, activity is postponed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                            + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                        );
                    }
                    else
					*/
                    {
                        thread dbDataRetention(&MMSEngineProcessor::handleDBDataRetentionEventThread, this);
                        dbDataRetention.detach();
                    }
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleDBDataRetentionEventThread failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_DBDATARETENTIONEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_CHECKREFRESHPARTITIONFREESIZEEVENT:	// 8
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKREFRESHPARTITIONFREESIZEEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
					/* 2019-07-10: this check was removed since this event happens once a day
                    if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                    {
                        // content retention is a periodical event, we will wait the next one
                        
                        _logger->warn(__FILEREF__ + "Not enough available threads to manage handleContentRetentionEventThread, activity is postponed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                            + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                        );
                    }
                    else
					*/
                    {
                        thread checkRefreshPartitionFreeSize(&MMSEngineProcessor::handleCheckRefreshPartitionFreeSizeEventThread,
								this);
                        checkRefreshPartitionFreeSize.detach();
                    }
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleCheckRefreshPartitionFreeSizeEvent failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKREFRESHPARTITIONFREESIZEEVENT"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_UPDATELIVERECORDERVOD:	// 9
            {
                _logger->debug(__FILEREF__ + "1. Received MMSENGINE_EVENTTYPEIDENTIFIER_UPDATELIVERECORDERVOD"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );

                try
                {
                    if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                    {
                        // it is a periodical event, we will wait the next one
                        
                        _logger->warn(__FILEREF__ + "Not enough available threads to manage handleUpdateLiveRecorderVODEventThread, activity is postponed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                            + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                        );
                    }
                    else
                    {
                        thread updateLiveRecorderVOD(&MMSEngineProcessor::handleUpdateLiveRecorderVODEventThread, this,
                            _processorsThreadsNumber);
                        updateLiveRecorderVOD.detach();
                    }
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleUpdateLiveRecorderVODEventThread failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event2>(event);

                _logger->debug(__FILEREF__ + "2. Received MMSENGINE_EVENTTYPEIDENTIFIER_UPDATELIVERECORDERVOD"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                );
            }
            break;
            default:
                throw runtime_error(string("Event type identifier not managed")
                        + to_string(event->getEventKey().first));
        }

		chrono::system_clock::time_point endEvent = chrono::system_clock::now();
		long elapsedInSeconds = chrono::duration_cast<chrono::seconds>(endEvent - startEvent).count();

		if (elapsedInSeconds > _maxEventManagementTimeInSeconds)
			_logger->warn(__FILEREF__ + "MMSEngineProcessor. Event management took too time"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", event id: " + to_string(event->getEventKey().first)
				+ ", _maxEventManagementTimeInSeconds: " + to_string(_maxEventManagementTimeInSeconds)
				+ ", elapsed in seconds: " + to_string(elapsedInSeconds)
		);
    }

    _logger->info(__FILEREF__ + "MMSEngineProcessor thread terminated"
        + ", _processorIdentifier: " + to_string(_processorIdentifier)
    );
}

void MMSEngineProcessor::handleCheckIngestionEvent()
{

    try
    {
        vector<tuple<int64_t,shared_ptr<Workspace>,string, string,
			MMSEngineDBFacade::IngestionType, MMSEngineDBFacade::IngestionStatus>> 
                ingestionsToBeManaged;

        try
        {
			_mmsEngineDBFacade->getIngestionsToBeManaged(ingestionsToBeManaged, 
				_processorMMS, _maxIngestionJobsPerEvent 
            );

            _logger->info(__FILEREF__ + "getIngestionsToBeManaged result"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionsToBeManaged.size: " + to_string(ingestionsToBeManaged.size())
            );
        }
        catch(AlreadyLocked e)
        {
            _logger->warn(__FILEREF__ + "getIngestionsToBeManaged failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", exception: " + e.what()
            );

			return;
            // throw e;
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "getIngestionsToBeManaged failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", exception: " + e.what()
            );

            throw e;
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "getIngestionsToBeManaged failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", exception: " + e.what()
            );

            throw e;
        }
        
        for (tuple<int64_t, shared_ptr<Workspace>, string, string,
				MMSEngineDBFacade::IngestionType, MMSEngineDBFacade::IngestionStatus> 
                ingestionToBeManaged: ingestionsToBeManaged)
        {
            int64_t ingestionJobKey;
            try
            {
                shared_ptr<Workspace> workspace;
                string ingestionDate;
                string metaDataContent;
                string sourceReference;
                MMSEngineDBFacade::IngestionType ingestionType;
                MMSEngineDBFacade::IngestionStatus ingestionStatus;

                tie(ingestionJobKey, workspace, ingestionDate, metaDataContent,
                        ingestionType, ingestionStatus) = ingestionToBeManaged;
                
                _logger->info(__FILEREF__ + "json to be processed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", workspace->_workspaceKey: " + to_string(workspace->_workspaceKey)
                    + ", ingestionDate: " + ingestionDate
                    + ", ingestionType: " + MMSEngineDBFacade::toString(ingestionType)
                    + ", ingestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                    + ", metaDataContent: " + metaDataContent
                );

                try
                {
					if (ingestionType != MMSEngineDBFacade::IngestionType::RemoveContent)
					{
						_mmsEngineDBFacade->checkWorkspaceStorageAndMaxIngestionNumber (
                            workspace->_workspaceKey);
					}
                }
                catch(runtime_error e)
                {
                    _logger->error(__FILEREF__ + "checkWorkspaceStorageAndMaxIngestionNumber failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", exception: " + e.what()
                    );
                    string errorMessage = e.what();

                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_WorkspaceReachedMaxStorageOrIngestionNumber"
                        + ", errorMessage: " + e.what()
                    );                            
					try
					{
						_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                            MMSEngineDBFacade::IngestionStatus::End_WorkspaceReachedMaxStorageOrIngestionNumber,
                            e.what()
						);
					}
					catch(runtime_error& re)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", IngestionStatus: " + "End_WorkspaceReachedMaxStorageOrIngestionNumber"
							+ ", errorMessage: " + re.what()
						);
					}
					catch(exception ex)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", IngestionStatus: " + "End_WorkspaceReachedMaxStorageOrIngestionNumber"
							+ ", errorMessage: " + ex.what()
						);
					}

                    throw e;
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "checkWorkspaceStorageAndMaxIngestionNumber failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", exception: " + e.what()
                    );
                    string errorMessage = e.what();

                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_WorkspaceReachedMaxStorageOrIngestionNumber"
                        + ", errorMessage: " + e.what()
                    );                            
					try
					{
						_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                            MMSEngineDBFacade::IngestionStatus::End_WorkspaceReachedMaxStorageOrIngestionNumber,
                            e.what()
						);
					}
					catch(runtime_error& re)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", IngestionStatus: " + "End_WorkspaceReachedMaxStorageOrIngestionNumber"
							+ ", errorMessage: " + re.what()
						);
					}
					catch(exception ex)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", IngestionStatus: " + "End_WorkspaceReachedMaxStorageOrIngestionNumber"
							+ ", errorMessage: " + ex.what()
						);
					}

                    throw e;
                }
                
                if (ingestionStatus == MMSEngineDBFacade::IngestionStatus::SourceDownloadingInProgress
                        || ingestionStatus == MMSEngineDBFacade::IngestionStatus::SourceMovingInProgress
                        || ingestionStatus == MMSEngineDBFacade::IngestionStatus::SourceCopingInProgress
                        || ingestionStatus == MMSEngineDBFacade::IngestionStatus::SourceUploadingInProgress)
                {
                    // source binary download or uploaded terminated

                    string sourceFileName = to_string(ingestionJobKey) + "_source";

                    {
                        shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                                ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

                        localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
                        localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
                        localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

						localAssetIngestionEvent->setExternalReadOnlyStorage(false);
                        localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
                        localAssetIngestionEvent->setIngestionSourceFileName(sourceFileName);
                        localAssetIngestionEvent->setMMSSourceFileName("");
                        localAssetIngestionEvent->setWorkspace(workspace);
                        localAssetIngestionEvent->setIngestionType(ingestionType);
                        localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(true);

                        localAssetIngestionEvent->setMetadataContent(metaDataContent);

                        shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
                        _multiEventsSet->addEvent(event);

                        _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", getEventKey().first: " + to_string(event->getEventKey().first)
                            + ", getEventKey().second: " + to_string(event->getEventKey().second));
                    }
                }
                else    // Start_TaskQueued
                {
                    Json::Value parametersRoot;
                    try
                    {
                        Json::CharReaderBuilder builder;
                        Json::CharReader* reader = builder.newCharReader();
                        string errors;

                        bool parsingSuccessful = reader->parse(metaDataContent.c_str(),
                                metaDataContent.c_str() + metaDataContent.size(), 
                                &parametersRoot, &errors);
                        delete reader;

                        if (!parsingSuccessful)
                        {
                            string errorMessage = __FILEREF__ + "failed to parse the metadata"
                                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", errors: " + errors
                                    + ", metaDataContent: " + metaDataContent
                                    ;
                            _logger->error(errorMessage);

                            throw runtime_error(errorMessage);
                        }
                    }
                    catch(...)
                    {
                        string errorMessage = string("metadata json is not well format")
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", metaDataContent: " + metaDataContent
                                ;
                        _logger->error(__FILEREF__ + errorMessage);

                        _logger->info(__FILEREF__ + "Update IngestionJob"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                            + ", errorMessage: " + errorMessage
                            + ", processorMMS: " + ""
                        );
						try
						{
							_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                errorMessage
							);
						}
						catch(runtime_error& re)
						{
							_logger->info(__FILEREF__ + "Update IngestionJob failed"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
								+ ", errorMessage: " + re.what()
							);
						}
						catch(exception ex)
						{
							_logger->info(__FILEREF__ + "Update IngestionJob failed"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
								+ ", errorMessage: " + ex.what()
							);
						}

                        throw runtime_error(errorMessage);
                    }

                    vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies;
                    try
                    {
                        Validator validator(_logger, _mmsEngineDBFacade, _configuration);
						if (ingestionType == MMSEngineDBFacade::IngestionType::GroupOfTasks)
							validator.validateGroupOfTasksMetadata(
                                workspace->_workspaceKey, parametersRoot);                        
						else
							dependencies = validator.validateSingleTaskMetadata(
                                workspace->_workspaceKey, ingestionType, parametersRoot);                        

						// Scenario: Live-Recording using HighAvailability, both main and backup contents are ingested (Add-Content)
						//		and both potentially will have the tasks for onSuccess, onFailure and onComplete.
						//		In this scenario, only the content having validated=true has to execute
						//		the tasks (onSuccess, onFailure and onComplete) and the one having validated=false,
						//		does not have to execute the tasks
						//		To manage this scenario we will check if the dependency is a content coming
						//		from Live-Recording using HighAvailability and, if it is validated=false
						//		we will set NOT_TO_BE_EXECUTED to the potential tasks 
						if (dependencies.size() == 1)
						{
							string userData;
							string ingestionDate;
							{
								int64_t key;
								MMSEngineDBFacade::ContentType referenceContentType;
								Validator::DependencyType dependencyType;
            
								tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
									keyAndDependencyType	= dependencies[0];
								tie(key, referenceContentType, dependencyType) = keyAndDependencyType;
            
								try
								{
									if (dependencyType == Validator::DependencyType::MediaItemKey)
									{
										bool warningIfMissing = false;
										tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
											contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
											_mmsEngineDBFacade->getMediaItemKeyDetails(
											workspace->_workspaceKey, key, warningIfMissing);

										string localTitle;
										int64_t localIngestionJobKey;
										tie(referenceContentType, localTitle, userData, ingestionDate, ignore,
												localIngestionJobKey)
											= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
									}
									else
									{
										bool warningIfMissing = false;
										tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,
											int64_t, string>
											mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
											_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
											workspace->_workspaceKey, key, warningIfMissing);

										int64_t mediaItemKey;
										string localTitle;
										string userData;
										int64_t localIngestionJobKey;
										tie(mediaItemKey, referenceContentType, localTitle, userData,
												ingestionDate, localIngestionJobKey, ignore)
											= mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
									}
								}
								catch (exception e)
								{
									// in case MediaItemKey is not present, just continue,
									// we will have an error during the management of the task
									string errorMessage = __FILEREF__ + "Exception to retrieve the MediaItemKey details"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
											+ ", ingestionJobKey: " + to_string(ingestionJobKey)
											+ ", key: " + to_string(key)
											;
									_logger->error(errorMessage);
								}
							}

							if (userData != "")
							{
								bool userDataParsedSuccessful = false;

								Json::Value userDataRoot;
								try
								{
									Json::CharReaderBuilder builder;
									Json::CharReader* reader = builder.newCharReader();
									string errors;

									userDataParsedSuccessful = reader->parse(userData.c_str(),
										userData.c_str() + userData.size(), 
										&userDataRoot, &errors);
									delete reader;

									if (!userDataParsedSuccessful)
									{
										string errorMessage = __FILEREF__ + "failed to parse userData"
											+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", errors: " + errors
												+ ", userData: " + userData
												;
										_logger->error(errorMessage);
									}
								}
								catch(...)
								{
									string errorMessage = string("userData json is not well format")
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", userData: " + userData
										;
									_logger->error(__FILEREF__ + errorMessage);
								}

								if (userDataParsedSuccessful)
								{
									string mmsDataField = "mmsData";
									string dataTypeField = "dataType";
									if (JSONUtils::isMetadataPresent(userDataRoot, mmsDataField)
											&& JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], dataTypeField)
											)
									{
										string dataType = (userDataRoot[mmsDataField]).get(dataTypeField, "XXX").asString();

										if (dataType == "liveRecordingChunk")
										{
											string validatedField = "validated";
											if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], validatedField))
											{
												bool validated = JSONUtils::asBool((userDataRoot[mmsDataField]), validatedField, false);

												if (!validated)
												{
													_logger->info(__FILEREF__ + "This task and all his dependencies will not be executed "
														"because caused by a liveRecordingChunk that was not validated. setNotToBeExecutedStartingFromBecauseChunkNotSelected will be called"
														+ ", _processorIdentifier: " + to_string(_processorIdentifier)
														+ ", ingestionJobKey: " + to_string(ingestionJobKey)
													);

													_mmsEngineDBFacade->setNotToBeExecutedStartingFromBecauseChunkNotSelected(
															ingestionJobKey, _processorMMS);

													continue;
												}
												else
												{
													// it is validated, just continue to manage the task
												}
											}
											else if (ingestionDate != "")
											{
												time_t utcIngestionDate;
												bool ingestionDateParsedSuccessful = true;
												{
													unsigned long		ulUTCYear;
													unsigned long		ulUTCMonth;
													unsigned long		ulUTCDay;
													unsigned long		ulUTCHour;
													unsigned long		ulUTCMinutes;
													unsigned long		ulUTCSeconds;
													tm					tmIngestionDate;
													int					sscanfReturn;


													if ((sscanfReturn = sscanf (ingestionDate.c_str(),
														"%4lu-%2lu-%2luT%2lu:%2lu:%2luZ",
														&ulUTCYear,
														&ulUTCMonth,
														&ulUTCDay,
														&ulUTCHour,
														&ulUTCMinutes,
														&ulUTCSeconds)) != 6)
													{
														string errorMessage = __FILEREF__ + "IngestionDate has a wrong format (sscanf failed)"
															+ ", _processorIdentifier: " + to_string(_processorIdentifier)
															+ ", sscanfReturn: " + to_string(sscanfReturn)
															;
														_logger->error(errorMessage);

														ingestionDateParsedSuccessful = false;
														// throw runtime_error(errorMessage);
													}
													else
													{
														time (&utcIngestionDate);
														gmtime_r(&utcIngestionDate, &tmIngestionDate);

														tmIngestionDate.tm_year		= ulUTCYear - 1900;
														tmIngestionDate.tm_mon		= ulUTCMonth - 1;
														tmIngestionDate.tm_mday		= ulUTCDay;
														tmIngestionDate.tm_hour		= ulUTCHour;
														tmIngestionDate.tm_min		= ulUTCMinutes;
														tmIngestionDate.tm_sec		= ulUTCSeconds;

														utcIngestionDate = timegm(&tmIngestionDate);
													}
												}

												if (ingestionDateParsedSuccessful)
												{
													time_t utcNow;
													{
														chrono::system_clock::time_point now = chrono::system_clock::now();
														utcNow = chrono::system_clock::to_time_t(now);
													}

													int waitingTimeoutToValidateInSeconds = 120;
													if (utcNow - utcIngestionDate >= waitingTimeoutToValidateInSeconds)
													{
														_logger->info(__FILEREF__ + "This task is caused by a liveRecordingChunk and we do not know yet "
															"if it will be validated or not. The waiting timeout expired, so setNotToBeExecutedStartingFromBecauseChunkNotSelected will be called"
															+ ", _processorIdentifier: " + to_string(_processorIdentifier)
															+ ", ingestionJobKey: " + to_string(ingestionJobKey)
															+ ", mediaItemKey ingestionDate: " + ingestionDate
															+ ", waitingTimeoutToValidateInSeconds: " + to_string(waitingTimeoutToValidateInSeconds)
														);

														_mmsEngineDBFacade->setNotToBeExecutedStartingFromBecauseChunkNotSelected(
															ingestionJobKey, _processorMMS);

														continue;
													}
													else
													{
														_logger->info(__FILEREF__ + "This task is caused by a liveRecordingChunk and we do not know yet "
															"if it will be validated or not. For this reason we will just wait the validation"
															+ ", _processorIdentifier: " + to_string(_processorIdentifier)
															+ ", ingestionJobKey: " + to_string(ingestionJobKey)
														);

														string errorMessage = "";
														string processorMMS = "";

														_logger->info(__FILEREF__ + "Update IngestionJob"
															+ ", _processorIdentifier: " + to_string(_processorIdentifier)
															+ ", ingestionJobKey: " + to_string(ingestionJobKey)
															+ ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
															+ ", errorMessage: " + errorMessage
															+ ", processorMMS: " + processorMMS
														);
														_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
															ingestionStatus, 
															errorMessage,
															processorMMS
															);

														continue;
													}
												}
											}
										}
									}
								}
							}
						}
                    }
                    catch(runtime_error e)
                    {
                        _logger->error(__FILEREF__ + "validateMetadata failed"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", exception: " + e.what()
                        );

                        string errorMessage = e.what();

                        _logger->info(__FILEREF__ + "Update IngestionJob"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                            + ", errorMessage: " + errorMessage
                            + ", processorMMS: " + ""
                        );
						try
						{
							_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                errorMessage
							);
						}
						catch(runtime_error& re)
						{
							_logger->info(__FILEREF__ + "Update IngestionJob failed"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
								+ ", errorMessage: " + re.what()
							);
						}
						catch(exception ex)
						{
							_logger->info(__FILEREF__ + "Update IngestionJob failed"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
								+ ", errorMessage: " + ex.what()
							);
						}

                        throw runtime_error(errorMessage);
                    }
                    catch(exception e)
                    {
                        _logger->error(__FILEREF__ + "validateMetadata failed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", exception: " + e.what()
                        );

                        string errorMessage = e.what();

                        _logger->info(__FILEREF__ + "Update IngestionJob"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                            + ", errorMessage: " + errorMessage
                            + ", processorMMS: " + ""
                        );
						try
						{
							_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                errorMessage
							);
						}
						catch(runtime_error& re)
						{
							_logger->info(__FILEREF__ + "Update IngestionJob failed"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
								+ ", errorMessage: " + re.what()
							);
						}
						catch(exception ex)
						{
							_logger->info(__FILEREF__ + "Update IngestionJob failed"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
								+ ", errorMessage: " + ex.what()
							);
						}

                        throw runtime_error(errorMessage);
                    }

                    {
                        if (ingestionType == MMSEngineDBFacade::IngestionType::GroupOfTasks)
                        {
                            try
                            {
                                manageGroupOfTasks(
									ingestionJobKey, 
									workspace, 
									parametersRoot);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageGroupOfTasks failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageGroupOfTasks failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
						else if (ingestionType == MMSEngineDBFacade::IngestionType::AddContent)
                        {
                            MMSEngineDBFacade::IngestionStatus nextIngestionStatus;
                            string mediaSourceURL;
                            string mediaFileFormat;
                            string md5FileCheckSum;
                            int fileSizeInBytes;
							bool externalReadOnlyStorage;
                            try
                            {
                                tuple<MMSEngineDBFacade::IngestionStatus, string, string, string, int, bool>
									mediaSourceDetails = getMediaSourceDetails(
                                        ingestionJobKey, workspace,
                                        ingestionType, parametersRoot);

                                tie(nextIngestionStatus, mediaSourceURL, mediaFileFormat, 
									md5FileCheckSum, fileSizeInBytes, externalReadOnlyStorage) =
									mediaSourceDetails;                        
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }

                            try
                            {
								if (externalReadOnlyStorage)
								{
									shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
										->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

									localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
									localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
									localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

									localAssetIngestionEvent->setExternalReadOnlyStorage(true);
									localAssetIngestionEvent->setExternalStorageMediaSourceURL(mediaSourceURL);
									localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
									// localAssetIngestionEvent->setIngestionSourceFileName(sourceFileName);
									// localAssetIngestionEvent->setMMSSourceFileName("");
									localAssetIngestionEvent->setWorkspace(workspace);
									localAssetIngestionEvent->setIngestionType(ingestionType);
									localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(true);

									localAssetIngestionEvent->setMetadataContent(metaDataContent);

									shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
									_multiEventsSet->addEvent(event);

									_logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", getEventKey().first: " + to_string(event->getEventKey().first)
										+ ", getEventKey().second: " + to_string(event->getEventKey().second));
								}
								else
								{
									bool segmentedContent = false;
									if (mediaFileFormat == "m3u8")
										segmentedContent = true;

									if (nextIngestionStatus ==
										MMSEngineDBFacade::IngestionStatus::SourceDownloadingInProgress)
									{
										if (_processorsThreadsNumber.use_count() >
											_processorThreads + _maxAdditionalProcessorThreads)
										{
											_logger->warn(__FILEREF__ + "Not enough available threads to manage downloadMediaSourceFileThread, activity is postponed"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
												+ ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
											);

											string errorMessage = "";
											string processorMMS = "";

											_logger->info(__FILEREF__ + "Update IngestionJob"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
												+ ", errorMessage: " + errorMessage
												+ ", processorMMS: " + processorMMS
											);
											_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                                ingestionStatus,
                                                errorMessage,
                                                processorMMS
                                                );
										}
										else
										{
											string errorMessage = "";
											string processorMMS = "";

											_logger->info(__FILEREF__ + "Update IngestionJob"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", IngestionStatus: " + MMSEngineDBFacade::toString(nextIngestionStatus)
												+ ", errorMessage: " + errorMessage
												+ ", processorMMS: " + processorMMS
											);                            
											_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                                nextIngestionStatus,
                                                errorMessage,
                                                processorMMS
                                                );

											thread downloadMediaSource(&MMSEngineProcessor::downloadMediaSourceFileThread, this, 
												_processorsThreadsNumber, mediaSourceURL, segmentedContent, ingestionJobKey, workspace);
											downloadMediaSource.detach();
										}
									}
									else if (nextIngestionStatus ==
										MMSEngineDBFacade::IngestionStatus::SourceMovingInProgress)
									{
										if (_processorsThreadsNumber.use_count() >
											_processorThreads + _maxAdditionalProcessorThreads)
										{
											_logger->warn(__FILEREF__
												+ "Not enough available threads to manage moveMediaSourceFileThread, activity is postponed"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", _processorsThreadsNumber.use_count(): "
												+ to_string(_processorsThreadsNumber.use_count())
												+ ", _processorThreads + _maxAdditionalProcessorThreads: "
												+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
											);

											string errorMessage = "";
											string processorMMS = "";

											_logger->info(__FILEREF__ + "Update IngestionJob"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
												+ ", errorMessage: " + errorMessage
												+ ", processorMMS: " + processorMMS
											);
											_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                                ingestionStatus,
                                                errorMessage,
                                                processorMMS
                                                );
										}
										else
										{
											string errorMessage = "";
											string processorMMS = "";

											_logger->info(__FILEREF__ + "Update IngestionJob"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", IngestionStatus: " + MMSEngineDBFacade::toString(nextIngestionStatus)
												+ ", errorMessage: " + errorMessage
												+ ", processorMMS: " + processorMMS
											);
											_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                                nextIngestionStatus,
                                                errorMessage,
                                                processorMMS
                                                );
                                        
											thread moveMediaSource(&MMSEngineProcessor::moveMediaSourceFileThread, this, 
												_processorsThreadsNumber, mediaSourceURL, segmentedContent, ingestionJobKey, workspace);
											moveMediaSource.detach();
										}
									}
									else if (nextIngestionStatus ==
										MMSEngineDBFacade::IngestionStatus::SourceCopingInProgress)
									{
										if (_processorsThreadsNumber.use_count() >
											_processorThreads + _maxAdditionalProcessorThreads)
										{
											_logger->warn(__FILEREF__
												+ "Not enough available threads to manage copyMediaSourceFileThread, activity is postponed"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", _processorsThreadsNumber.use_count(): "
												+ to_string(_processorsThreadsNumber.use_count())
												+ ", _processorThreads + _maxAdditionalProcessorThreads: "
												+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
											);

											string errorMessage = "";
											string processorMMS = "";

											_logger->info(__FILEREF__ + "Update IngestionJob"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
												+ ", errorMessage: " + errorMessage
												+ ", processorMMS: " + processorMMS
											);                            
											_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                                ingestionStatus, 
                                                errorMessage,
                                                processorMMS
                                                );
										}
										else
										{
											string errorMessage = "";
											string processorMMS = "";

											_logger->info(__FILEREF__ + "Update IngestionJob"
												+ ", _processorIdentifier: " + to_string(_processorIdentifier)
												+ ", ingestionJobKey: " + to_string(ingestionJobKey)
												+ ", IngestionStatus: "
												+ MMSEngineDBFacade::toString(nextIngestionStatus)
												+ ", errorMessage: " + errorMessage
												+ ", processorMMS: " + processorMMS
											);
											_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                                nextIngestionStatus, 
                                                errorMessage,
                                                processorMMS
                                                );

											thread copyMediaSource(&MMSEngineProcessor::copyMediaSourceFileThread, this, 
												_processorsThreadsNumber, mediaSourceURL, segmentedContent, ingestionJobKey, workspace);
											copyMediaSource.detach();
										}
									}
									else // if (nextIngestionStatus == MMSEngineDBFacade::IngestionStatus::SourceUploadingInProgress)
									{
										string errorMessage = "";
										string processorMMS = "";

										_logger->info(__FILEREF__ + "Update IngestionJob"
											+ ", _processorIdentifier: " + to_string(_processorIdentifier)
											+ ", ingestionJobKey: " + to_string(ingestionJobKey)
											+ ", IngestionStatus: " + MMSEngineDBFacade::toString(nextIngestionStatus)
											+ ", errorMessage: " + errorMessage
											+ ", processorMMS: " + processorMMS
										);                            
										_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                            nextIngestionStatus, 
                                            errorMessage,
                                            processorMMS
                                            );
									}
								}
                            }
                            catch(exception e)
                            {
                                string errorMessage = string("Downloading media source or update Ingestion job failed")
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                ;
                                _logger->error(__FILEREF__ + errorMessage);

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::RemoveContent)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                removeContentTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "removeContentTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									 _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "removeContentTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::FTPDelivery)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                ftpDeliveryContentTask(
                                        ingestionJobKey, 
                                        ingestionStatus,
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "ftpDeliveryContentTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									 _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "ftpDeliveryContentTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::LocalCopy)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                if (!_localCopyTaskEnabled)
                                {
                                    string errorMessage = string("Local-Copy Task is not enabled in this MMS deploy")
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    ;
                                    _logger->error(__FILEREF__ + errorMessage);

                                    throw runtime_error(errorMessage);
                                }
                                
                                localCopyContentTask(
                                        ingestionJobKey,
                                        ingestionStatus,
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "localCopyContentTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "localCopyContentTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::HTTPCallback)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                httpCallbackTask(
                                        ingestionJobKey,
                                        ingestionStatus,
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "httpCallbackTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "httpCallbackTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::Encode)
                        {
                            try
                            {
                                manageEncodeTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageEncodeTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageEncodeTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::VideoSpeed)
                        {
                            try
                            {
                                manageVideoSpeedTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageVideoSpeedTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageVideoSpeedTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::PictureInPicture)
                        {
                            try
                            {
                                managePictureInPictureTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "managePictureInPictureTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "managePictureInPictureTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::Frame
                                || ingestionType == MMSEngineDBFacade::IngestionType::PeriodicalFrames
                                || ingestionType == MMSEngineDBFacade::IngestionType::IFrames
                                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByPeriodicalFrames
                                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByIFrames
                                )
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                if (ingestionType == MMSEngineDBFacade::IngestionType::PeriodicalFrames
                                    || ingestionType == MMSEngineDBFacade::IngestionType::IFrames
                                    || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByPeriodicalFrames
                                    || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByIFrames)
                                {
                                    manageGenerateFramesTask(
                                        ingestionJobKey,
                                        workspace,
                                        ingestionType,
                                        parametersRoot,
                                        dependencies);
                                }
                                else // Frame
                                {
									if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
									{
										_logger->warn(__FILEREF__
											+ "Not enough available threads to manage changeFileFormatThread, activity is postponed"
											+ ", _processorIdentifier: " + to_string(_processorIdentifier)
											+ ", ingestionJobKey: " + to_string(ingestionJobKey)
											+ ", _processorsThreadsNumber.use_count(): "
												+ to_string(_processorsThreadsNumber.use_count())
											+ ", _processorThreads + _maxAdditionalProcessorThreads: "
											+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
										);

										string errorMessage = "";
										string processorMMS = "";

										_logger->info(__FILEREF__ + "Update IngestionJob"
											+ ", _processorIdentifier: " + to_string(_processorIdentifier)
											+ ", ingestionJobKey: " + to_string(ingestionJobKey)
											+ ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
											+ ", errorMessage: " + errorMessage
											+ ", processorMMS: " + processorMMS
										);                            
										_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                            ingestionStatus, 
                                            errorMessage,
                                            processorMMS
                                            );
									}
									else
									{
										thread generateAndIngestFramesThread(&MMSEngineProcessor::generateAndIngestFramesThread,
												this, _processorsThreadsNumber, ingestionJobKey, workspace,
												ingestionType,
												parametersRoot,
												// it cannot be passed as reference because it will change soon by the parent thread
												dependencies
										);
										generateAndIngestFramesThread.detach();
										/*
										generateAndIngestFramesTask(
											ingestionJobKey, 
											workspace, 
											ingestionType,
											parametersRoot, 
											dependencies);
										*/
									}
                                }
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestFramesTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestFramesTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::Slideshow)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                manageSlideShowTask(
                                        ingestionJobKey,
                                        workspace,
                                        parametersRoot,
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageSlideShowTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageSlideShowTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::ConcatDemuxer)
                        {
                            // mediaItemKeysDependency is present because checked by
							// _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                if (_processorsThreadsNumber.use_count() >
										_processorThreads + _maxAdditionalProcessorThreads)
                                {
                                    _logger->warn(__FILEREF__ + "Not enough available threads to manage generateAndIngestConcatenationThread, activity is postponed"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", _processorsThreadsNumber.use_count(): "
											+ to_string(_processorsThreadsNumber.use_count())
                                        + ", _processorThreads + _maxAdditionalProcessorThreads: "
											+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
                                    );

                                    string errorMessage = "";
                                    string processorMMS = "";

                                    _logger->info(__FILEREF__ + "Update IngestionJob"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                                        + ", errorMessage: " + errorMessage
                                        + ", processorMMS: " + processorMMS
                                    );                            
                                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                            ingestionStatus, 
                                            errorMessage,
                                            processorMMS
                                            );
                                }
                                else
                                {
                                    thread generateAndIngestConcatenationThread(
										&MMSEngineProcessor::generateAndIngestConcatenationThread, this, 
                                        _processorsThreadsNumber, ingestionJobKey, 
										workspace, 
										parametersRoot,
										
										// it cannot be passed as reference because it will change soon
										// by the parent thread
										dependencies
									);
									generateAndIngestConcatenationThread.detach();
                                }
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestConcatenationThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestConcatenationThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::Cut)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                                {
                                    _logger->warn(__FILEREF__ + "Not enough available threads to manage generateAndIngestCutMediaThread, activity is postponed"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                                        + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                                    );

                                    string errorMessage = "";
                                    string processorMMS = "";

                                    _logger->info(__FILEREF__ + "Update IngestionJob"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                                        + ", errorMessage: " + errorMessage
                                        + ", processorMMS: " + processorMMS
                                    );                            
                                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                            ingestionStatus, 
                                            errorMessage,
                                            processorMMS
                                            );
                                }
                                else
                                {
                                    thread generateAndIngestCutMediaThread(&MMSEngineProcessor::generateAndIngestCutMediaThread, this, 
                                        _processorsThreadsNumber, ingestionJobKey, 
                                            workspace, 
                                            parametersRoot,
                                            dependencies    // it cannot be passed as reference because it will change soon by the parent thread
                                            );
                                    generateAndIngestCutMediaThread.detach();
                                }
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestCutMediaThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestCutMediaThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
							/*
                            try
                            {
                                generateAndIngestCutMediaTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestCutMediaTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "generateAndIngestCutMediaTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
							*/
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::ExtractTracks)
                        {
                            try
                            {
                                if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                                {
                                    _logger->warn(__FILEREF__ + "Not enough available threads to manage extractTracksContentThread, activity is postponed"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                                        + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
                                    );

                                    string errorMessage = "";
                                    string processorMMS = "";

                                    _logger->info(__FILEREF__ + "Update IngestionJob"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                                        + ", errorMessage: " + errorMessage
                                        + ", processorMMS: " + processorMMS
                                    );                            
                                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                            ingestionStatus, 
                                            errorMessage,
                                            processorMMS
                                            );
                                }
                                else
                                {
                                    thread extractTracksContentThread(&MMSEngineProcessor::extractTracksContentThread, this, 
                                        _processorsThreadsNumber, ingestionJobKey, 
                                            workspace, 
                                            parametersRoot,
                                            dependencies    // it cannot be passed as reference because it will change soon by the parent thread
                                            );
                                    extractTracksContentThread.detach();
                                }
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "extractTracksContentThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "extractTracksContentThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::OverlayImageOnVideo)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                manageOverlayImageOnVideoTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageOverlayImageOnVideoTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageOverlayImageOnVideoTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::OverlayTextOnVideo)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                manageOverlayTextOnVideoTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageOverlayTextOnVideoTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageOverlayTextOnVideoTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::EmailNotification)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                manageEmailNotificationTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageEmailNotificationTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageEmailNotificationTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::MediaCrossReference)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                manageMediaCrossReferenceTask(
                                        ingestionJobKey, 
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageMediaCrossReferenceTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageMediaCrossReferenceTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::PostOnFacebook)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                postOnFacebookTask(
                                        ingestionJobKey, 
                                        ingestionStatus,
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "postOnFacebookTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "postOnFacebookTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::PostOnYouTube)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
                                postOnYouTubeTask(
                                        ingestionJobKey, 
                                        ingestionStatus,
                                        workspace, 
                                        parametersRoot, 
                                        dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "postOnYouTubeTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "postOnYouTubeTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::FaceRecognition)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
								manageFaceRecognitionMediaTask(
									ingestionJobKey, 
									ingestionStatus,
									workspace, 
									parametersRoot, 
									dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageFaceRecognitionMediaTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageFaceRecognitionMediaTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::FaceIdentification)
                        {
                            // mediaItemKeysDependency is present because checked by _mmsEngineDBFacade->getIngestionsToBeManaged
                            try
                            {
								manageFaceIdentificationMediaTask(
									ingestionJobKey, 
									ingestionStatus,
									workspace, 
									parametersRoot, 
									dependencies);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageFaceIdentificationMediaTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageFaceIdentificationMediaTask failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::LiveRecorder)
                        {
                            try
                            {
								manageLiveRecorder(
									ingestionJobKey, 
									ingestionStatus,
									workspace, 
									parametersRoot);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageLiveRecorder failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageLiveRecorder failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::LiveProxy)
                        {
                            try
                            {
								manageLiveProxy(
									ingestionJobKey, 
									ingestionStatus,
									workspace, 
									parametersRoot);
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "manageLiveProxy failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "manageLiveProxy failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::LiveCut)
                        {
							try
							{
								if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
								{
									_logger->warn(__FILEREF__ + "Not enough available threads to manage liveCutThread, activity is postponed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
										+ ", _processorThreads + _maxAdditionalProcessorThreads: "
											+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
									);

									string errorMessage = "";
									string processorMMS = "";

									_logger->info(__FILEREF__ + "Update IngestionJob"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
										+ ", errorMessage: " + errorMessage
										+ ", processorMMS: " + processorMMS
									);                            
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
										ingestionStatus, 
										errorMessage,
										processorMMS
									);
								}
								else
								{
									thread liveCutThread(&MMSEngineProcessor::liveCutThread, this, 
										_processorsThreadsNumber, ingestionJobKey,
										workspace,
										parametersRoot
									);
									liveCutThread.detach();
								}
							}
							catch(runtime_error e)
							{
								_logger->error(__FILEREF__ + "liveCutThread failed"
									+ ", _processorIdentifier: " + to_string(_processorIdentifier)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", exception: " + e.what()
								);

								string errorMessage = e.what();

								_logger->info(__FILEREF__ + "Update IngestionJob"
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", IngestionStatus: " + "End_IngestionFailure"
									+ ", errorMessage: " + errorMessage
									+ ", processorMMS: " + ""
								);                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
										MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
										errorMessage
									);
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

								throw runtime_error(errorMessage);
							}
							catch(exception e)
							{
								_logger->error(__FILEREF__ + "liveCutThread failed"
									+ ", _processorIdentifier: " + to_string(_processorIdentifier)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", exception: " + e.what()
								);

								string errorMessage = e.what();

								_logger->info(__FILEREF__ + "Update IngestionJob"
									+ ", _processorIdentifier: " + to_string(_processorIdentifier)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", IngestionStatus: " + "End_IngestionFailure"
									+ ", errorMessage: " + errorMessage
									+ ", processorMMS: " + ""
								);                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
										MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
										errorMessage
									);
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

								throw runtime_error(errorMessage);
							}
						}
                        else if (ingestionType == MMSEngineDBFacade::IngestionType::ChangeFileFormat)
                        {
                            try
                            {
								if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
                                {
                                    _logger->warn(__FILEREF__
											+ "Not enough available threads to manage changeFileFormatThread, activity is postponed"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                                        + ", _processorThreads + _maxAdditionalProcessorThreads: "
											+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
                                    );

                                    string errorMessage = "";
                                    string processorMMS = "";

                                    _logger->info(__FILEREF__ + "Update IngestionJob"
                                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                                        + ", errorMessage: " + errorMessage
                                        + ", processorMMS: " + processorMMS
                                    );                            
                                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                            ingestionStatus, 
                                            errorMessage,
                                            processorMMS
                                            );
								}
								else
                                {
                                    thread changeFileFormatThread(&MMSEngineProcessor::changeFileFormatThread, this, 
                                        _processorsThreadsNumber, ingestionJobKey, 
                                            workspace, 
                                            parametersRoot,
                                            dependencies    // it cannot be passed as reference because it will change soon by the parent thread
                                            );
                                    changeFileFormatThread.detach();
                                }
                            }
                            catch(runtime_error e)
                            {
                                _logger->error(__FILEREF__ + "changeFileFormatThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                            catch(exception e)
                            {
                                _logger->error(__FILEREF__ + "changeFileFormatThread failed"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                        + ", exception: " + e.what()
                                );

                                string errorMessage = e.what();

                                _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", IngestionStatus: " + "End_IngestionFailure"
                                    + ", errorMessage: " + errorMessage
                                    + ", processorMMS: " + ""
                                );                            
								try
								{
									_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                                        errorMessage
                                        );
								}
								catch(runtime_error& re)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + re.what()
									);
								}
								catch(exception ex)
								{
									_logger->info(__FILEREF__ + "Update IngestionJob failed"
										+ ", _processorIdentifier: " + to_string(_processorIdentifier)
										+ ", ingestionJobKey: " + to_string(ingestionJobKey)
										+ ", IngestionStatus: " + "End_IngestionFailure"
										+ ", errorMessage: " + ex.what()
									);
								}

                                throw runtime_error(errorMessage);
                            }
                        }
                        else
                        {
                            string errorMessage = string("Unknown IngestionType")
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                    + ", ingestionType: " + MMSEngineDBFacade::toString(ingestionType);
                            _logger->error(__FILEREF__ + errorMessage);

                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                + ", errorMessage: " + errorMessage
                                + ", processorMMS: " + ""
                            );                            
							try
							{
								_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                    MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                    errorMessage
                                    );
							}
							catch(runtime_error& re)
							{
								_logger->info(__FILEREF__ + "Update IngestionJob failed"
									+ ", _processorIdentifier: " + to_string(_processorIdentifier)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
									+ ", errorMessage: " + re.what()
								);
							}
							catch(exception ex)
							{
								_logger->info(__FILEREF__ + "Update IngestionJob failed"
									+ ", _processorIdentifier: " + to_string(_processorIdentifier)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
									+ ", errorMessage: " + ex.what()
								);
							}

                            throw runtime_error(errorMessage);
                        }
                    }
                }
            }
            catch(runtime_error e)
            {
                _logger->error(__FILEREF__ + "Exception managing the Ingestion entry"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", exception: " + e.what()
                );
            }
            catch(exception e)
            {
                _logger->error(__FILEREF__ + "Exception managing the Ingestion entry"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", exception: " + e.what()
                );
            }
        }

		if (ingestionsToBeManaged.size() >= _maxIngestionJobsPerEvent)
		{
			shared_ptr<Event2>    event = _multiEventsSet->getEventsFactory()->getFreeEvent<Event2>(
				MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTIONEVENT);

			event->setSource(MMSENGINEPROCESSORNAME);
			event->setDestination(MMSENGINEPROCESSORNAME);
			event->setExpirationTimePoint(chrono::system_clock::now());

			_multiEventsSet->addEvent(event);

			_logger->debug(__FILEREF__ + "addEvent: EVENT_TYPE"
					+ ", MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTION"
					+ ", getEventKey().first: " + to_string(event->getEventKey().first)
					+ ", getEventKey().second: " + to_string(event->getEventKey().second)
			);
		}
    }
    catch(...)
    {
        _logger->error(__FILEREF__ + "handleCheckIngestionEvent failed"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
        );
    }
}

void MMSEngineProcessor::handleLocalAssetIngestionEventThread (
		shared_ptr<long> processorsThreadsNumber,
    LocalAssetIngestionEvent localAssetIngestionEvent)
{

    Json::Value parametersRoot;
    try
    {
        Json::CharReaderBuilder builder;
        Json::CharReader* reader = builder.newCharReader();
        string errors;

        string sMetadataContent = localAssetIngestionEvent.getMetadataContent();
        
        // LF and CR create problems to the json parser...
        while (sMetadataContent.back() == 10 || sMetadataContent.back() == 13)
            sMetadataContent.pop_back();
        
        bool parsingSuccessful = reader->parse(sMetadataContent.c_str(),
                sMetadataContent.c_str() + sMetadataContent.size(), 
                &parametersRoot, &errors);
        delete reader;

        if (!parsingSuccessful)
        {
			string errorMessage = __FILEREF__ + "failed to parse the metadata"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errors: " + errors
				+ ", metaDataContent: " + sMetadataContent
			;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "parsing parameters failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
			+ ", localAssetIngestionEvent.getMetadataContent(): " + localAssetIngestionEvent.getMetadataContent()
			+ ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "validateMetadata failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				 MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
				 e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }

	string binaryPathName;
	string externalStorageRelativePathName;
    try
    {
		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			string workspaceIngestionBinaryPathName;

			workspaceIngestionBinaryPathName = _mmsStorage->getWorkspaceIngestionRepository(
				localAssetIngestionEvent.getWorkspace());
			workspaceIngestionBinaryPathName
				.append("/")
				.append(localAssetIngestionEvent.getIngestionSourceFileName())
			;

			binaryPathName = workspaceIngestionBinaryPathName;
			/*
			string field = "FileFormat";
			string fileFormat = parametersRoot.get(field, "").asString();
			if (fileFormat == "m3u8")
			{
			}
			*/
		}
		else
		{
			string mediaSourceURL =
				localAssetIngestionEvent.getExternalStorageMediaSourceURL();

			string externalStoragePrefix("externalStorage://");
			if (!(mediaSourceURL.size() >= externalStoragePrefix.size()
					&& 0 == mediaSourceURL.compare(0, externalStoragePrefix.size(),
						externalStoragePrefix)))
			{
				string errorMessage = string("mediaSourceURL is not an externalStorage reference")
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", mediaSourceURL: " + mediaSourceURL 
				;

				_logger->error(__FILEREF__ + errorMessage);
            
				throw runtime_error(errorMessage);
			}
			externalStorageRelativePathName = mediaSourceURL.substr(externalStoragePrefix.length());
			binaryPathName = _mmsStorage->getMMSRootRepository()
				+"ExternalStorage_" + localAssetIngestionEvent.getWorkspace()->_directoryName
				+ externalStorageRelativePathName;
		}
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "binaryPathName initialization failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "binaryPathName initialization failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				 MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
				 e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }

	_logger->info(__FILEREF__ + "binaryPathName"
		+ ", _processorIdentifier: " + to_string(_processorIdentifier)
		+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
		+ ", binaryPathName: " + binaryPathName);

    string      metadataFileContent;
    vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies;
    Validator validator(_logger, _mmsEngineDBFacade, _configuration);
    try
    {
		dependencies = validator.validateSingleTaskMetadata(
			localAssetIngestionEvent.getWorkspace()->_workspaceKey,
			localAssetIngestionEvent.getIngestionType(), parametersRoot);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "validateMetadata failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", localAssetIngestionEvent.getMetadataContent(): " + localAssetIngestionEvent.getMetadataContent()
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "validateMetadata failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				 MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
				 e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMetadataFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}
            
        // throw e;
		return;	// return because it is a thread
    }

    MMSEngineDBFacade::IngestionStatus nextIngestionStatus;
    string mediaFileFormat;
    string md5FileCheckSum;
    int fileSizeInBytes;
	bool externalReadOnlyStorage;
    try
    {
		string mediaSourceURL;

        tuple<MMSEngineDBFacade::IngestionStatus, string, string, string, int, bool>
            mediaSourceDetails = getMediaSourceDetails(
                localAssetIngestionEvent.getIngestionJobKey(),
                localAssetIngestionEvent.getWorkspace(),
                localAssetIngestionEvent.getIngestionType(), parametersRoot);
        
        tie(nextIngestionStatus,
                mediaSourceURL, mediaFileFormat, 
                md5FileCheckSum, fileSizeInBytes, externalReadOnlyStorage) = mediaSourceDetails;

		// in case of youtube url, the real URL to be used has to be calcolated
		// Here the mediaFileFormat is retrieved
		{
			string youTubePrefix1 ("https://www.youtube.com/");
			string youTubePrefix2 ("https://youtu.be/");
			if (
				(mediaSourceURL.size() >= youTubePrefix1.size()
					&& 0 == mediaSourceURL.compare(0, youTubePrefix1.size(), youTubePrefix1))
				||
				(mediaSourceURL.size() >= youTubePrefix2.size()
					&& 0 == mediaSourceURL.compare(0, youTubePrefix2.size(), youTubePrefix2))
				)
			{
				FFMpeg ffmpeg (_configuration, _logger);
				pair<string, string> streamingURLDetails =
					ffmpeg.retrieveStreamingYouTubeURL(
					localAssetIngestionEvent.getIngestionJobKey(),
					-1,
					mediaSourceURL);

				tie(ignore, mediaFileFormat) = streamingURLDetails;


				_logger->info(__FILEREF__ + "Retrieve streaming YouTube URL"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", initial YouTube URL: " + mediaSourceURL
				);
			}
		}
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        // throw e;
		return;	// return because it is a thread
    }

    try
    {
        validateMediaSourceFile(
                localAssetIngestionEvent.getIngestionJobKey(),
                binaryPathName, mediaFileFormat,
                md5FileCheckSum, fileSizeInBytes);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "validateMediaSourceFile failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "validateMediaSourceFile failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", exception: " + e.what()
        );

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
				+ ", errorMessage: " + ex.what()
				);
		}

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        // throw e;
		return;	// return because it is a thread
    }

	string mediaSourceFileName;
	string mmsAssetPathName;
	string relativePathToBeUsed;
	long mmsPartitionUsed;
	try
	{
		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			mediaSourceFileName = localAssetIngestionEvent.getMMSSourceFileName();
			if (mediaSourceFileName == "")
			{
				mediaSourceFileName = localAssetIngestionEvent.getIngestionSourceFileName() + "." + mediaFileFormat;
			}

			relativePathToBeUsed = _mmsEngineDBFacade->nextRelativePathToBeUsed (
                localAssetIngestionEvent.getWorkspace()->_workspaceKey);
        
			unsigned long mmsPartitionIndexUsed;
			bool partitionIndexToBeCalculated   = true;
			bool deliveryRepositoriesToo        = true;
			mmsAssetPathName = _mmsStorage->moveAssetInMMSRepository(
				binaryPathName,
				localAssetIngestionEvent.getWorkspace()->_directoryName,
				mediaSourceFileName,
				relativePathToBeUsed,
				partitionIndexToBeCalculated,
				&mmsPartitionIndexUsed,
				deliveryRepositoriesToo,
				localAssetIngestionEvent.getWorkspace()->_territories
            );
			mmsPartitionUsed = mmsPartitionIndexUsed;

			if (mediaFileFormat == "m3u8")
			{
				relativePathToBeUsed += (mediaSourceFileName + "/");
			}
		}
		else
		{
			mmsAssetPathName = binaryPathName;
			mmsPartitionUsed = -1;

			size_t fileNameIndex = externalStorageRelativePathName.find_last_of("/");
			if (fileNameIndex == string::npos)
			{
				string errorMessage = __FILEREF__ + "No fileName found in externalStorageRelativePathName"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", externalStorageRelativePathName: " + externalStorageRelativePathName
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			relativePathToBeUsed = externalStorageRelativePathName.substr(0, fileNameIndex + 1);
			mediaSourceFileName = externalStorageRelativePathName.substr(fileNameIndex + 1);
		}
	}
	catch(runtime_error e)
	{
		_logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", errorMessage: " + e.what()
		);
       
		_logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
			+ ", IngestionStatus: " + "End_IngestionFailure"
			+ ", errorMessage: " + e.what()
		);                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
			);
		}
       
		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        // throw e;
		return;	// return because it is a thread
	}
	catch(exception e)
	{
		_logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
		);
       
		_logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
			+ ", IngestionStatus: " + "End_IngestionFailure"
			+ ", errorMessage: " + e.what()
		);                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
			);
		}
       
		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", binaryPathName: " + binaryPathName
				);

				FileIO::remove(binaryPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}
           
        // throw e;
		return;	// return because it is a thread
	}

	string m3u8FileName;
	if (mediaFileFormat == "m3u8")
	{
		// in this case mmsAssetPathName refers a directory and we need to find out the m3u8 file name

		try
		{
			FileIO::DirectoryEntryType_t detDirectoryEntryType;
			shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (
				mmsAssetPathName + "/");
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

					string m3u8Suffix(".m3u8");
					if (directoryEntry.size() >= m3u8Suffix.size()
							&& 0 == directoryEntry.compare(
								directoryEntry.size()-m3u8Suffix.size(),
								m3u8Suffix.size(), m3u8Suffix))
					{
						m3u8FileName = directoryEntry;

						scanDirectoryFinished = true;
					}
				}
				catch(DirectoryListFinished e)
				{
					scanDirectoryFinished = true;
				}
				catch(runtime_error e)
				{
					string errorMessage = __FILEREF__ + "listing directory failed"
						+ ", e.what(): " + e.what()
					;
					_logger->error(errorMessage);

					throw e;
				}
				catch(exception e)
				{
					string errorMessage = __FILEREF__ + "listing directory failed"
						+ ", e.what(): " + e.what()
					;
					_logger->error(errorMessage);

					throw e;
				}
			}

			FileIO::closeDirectory (directory);

			if (m3u8FileName == "")
			{
				string errorMessage = __FILEREF__ + "m3u8 file not found"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", mmsAssetPathName: " + mmsAssetPathName
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			mediaSourceFileName = m3u8FileName;
		}
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "retrieving m3u8 file failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", mmsAssetPathName: " + mmsAssetPathName
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure,
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

			// throw e;
			return;	// return because it is a thread
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "retrieving m3u8 file failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", mmsAssetPathName: " + mmsAssetPathName
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

			// throw e;
			return;	// return because it is a thread
        }        
	}

    MMSEngineDBFacade::ContentType contentType;

	/*
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
	*/
	pair<int64_t, long> mediaInfoDetails;
	vector<tuple<int, int64_t, string, string, int, int, string, long>> videoTracks;
	vector<tuple<int, int64_t, string, long, int, long, string>> audioTracks;

    int imageWidth = -1;
    int imageHeight = -1;
    string imageFormat;
    int imageQuality = -1;
    if (validator.isVideoAudioFileFormat(mediaFileFormat))
    {
        try
        {
            FFMpeg ffmpeg (_configuration, _logger);
            // tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long> mediaInfo;

			if (mediaFileFormat == "m3u8")
				mediaInfoDetails = ffmpeg.getMediaInfo(mmsAssetPathName + "/" + m3u8FileName,
						videoTracks, audioTracks);
			else
				mediaInfoDetails = ffmpeg.getMediaInfo(mmsAssetPathName, videoTracks, audioTracks);

			int64_t durationInMilliSeconds = -1;
			long bitRate = -1;
			tie(durationInMilliSeconds, bitRate) = mediaInfoDetails;

			_logger->info(__FILEREF__ + "ffmpeg.getMediaInfo"
				+ ", mmsAssetPathName: " + mmsAssetPathName
				+ ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
				+ ", bitRate: " + to_string(bitRate)
				+ ", videoTracks.size: " + to_string(videoTracks.size())
				+ ", audioTracks.size: " + to_string(audioTracks.size())
			);

			/*
            tie(durationInMilliSeconds, bitRate, 
                videoCodecName, videoProfile, videoWidth, videoHeight, videoAvgFrameRate, videoBitRate,
                audioCodecName, audioSampleRate, audioChannels, audioBitRate) = mediaInfo;
			*/

			/*
			 * 2019-10-13: commented because I guess the avg frame rate returned by ffmpeg is OK
			 * avg frame rate format is: total duration / total # of frames
            if (localAssetIngestionEvent.getForcedAvgFrameRate() != "")
            {
                _logger->info(__FILEREF__ + "handleLocalAssetIngestionEvent. Forced Avg Frame Rate"
                    + ", current avgFrameRate: " + videoAvgFrameRate
                    + ", forced avgFrameRate: " + localAssetIngestionEvent.getForcedAvgFrameRate()
                );
                
                videoAvgFrameRate = localAssetIngestionEvent.getForcedAvgFrameRate();
            }
			*/

            if (videoTracks.size() == 0)
                contentType = MMSEngineDBFacade::ContentType::Audio;
            else
                contentType = MMSEngineDBFacade::ContentType::Video;
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "EncoderVideoAudioProxy::getMediaInfo failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure,
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

			// throw e;
			return;	// return because it is a thread
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "EncoderVideoAudioProxy::getVideoOrAudioDurationInMilliSeconds failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

			// throw e;
			return;	// return because it is a thread
        }        
    }
    else if (validator.isImageFileFormat(mediaFileFormat))
    {
        try
        {
            _logger->info(__FILEREF__ + "Processing through Magick"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", mmsAssetPathName: " + mmsAssetPathName
            );
            Magick::Image      imageToEncode;

            imageToEncode.read (mmsAssetPathName.c_str());

            imageWidth	= imageToEncode.columns();
            imageHeight	= imageToEncode.rows();
            imageFormat = imageToEncode.magick();
            imageQuality = imageToEncode.quality();
            
            contentType = MMSEngineDBFacade::ContentType::Image;
        }
        catch( Magick::WarningCoder &e )
        {
            // Process coder warning while loading file (e.g. TIFF warning)
            // Maybe the user will be interested in these warnings (or not).
            // If a warning is produced while loading an image, the image
            // can normally still be used (but not if the warning was about
            // something important!)
            _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", e.what(): " + e.what()
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

            // throw runtime_error(e.what());
			// throw e;
			return;	// return because it is a thread
        }
        catch( Magick::Warning &e )
        {
            _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", e.what(): " + e.what()
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

            // throw runtime_error(e.what());
			// throw e;
			return;	// return because it is a thread
        }
        catch( Magick::ErrorFileOpen &e ) 
        { 
            _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", e.what(): " + e.what()
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

            // throw runtime_error(e.what());
			// throw e;
			return;	// return because it is a thread
        }
        catch (Magick::Error &e)
        { 
            _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", e.what(): " + e.what()
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

            // throw runtime_error(e.what());
			// throw e;
			return;	// return because it is a thread
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            );

			if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
			{
				try
				{
					_logger->info(__FILEREF__ + "Remove file"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", mmsAssetPathName: " + mmsAssetPathName
					);

					FileIO::remove(mmsAssetPathName);
				}
				catch(runtime_error e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->info(__FILEREF__ + "remove failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
						+ ", errorMessage: " + e.what()
					);
				}
			}

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                    e.what()
				);
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + re.what()
					);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + ex.what()
					);
			}

            // throw e;
			// throw e;
			return;	// return because it is a thread
        }
    }
    else
    {
        string errorMessage = string("Unknown mediaFileFormat")
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
			+ ", mmsAssetPathName: " + mmsAssetPathName
        ;

        _logger->error(__FILEREF__ + errorMessage);
        
		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", mmsAssetPathName: " + mmsAssetPathName
				);

				FileIO::remove(mmsAssetPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + errorMessage
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                errorMessage
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw runtime_error(errorMessage);
		return;	// return because it is a thread
    }

    // int64_t mediaItemKey;
    try
    {
        bool inCaseOfLinkHasItToBeRead = false;
        unsigned long long sizeInBytes;
		if (mediaFileFormat == "m3u8")
			sizeInBytes = FileIO::getDirectorySizeInBytes(mmsAssetPathName);   
		else
			sizeInBytes = FileIO::getFileSizeInBytes(mmsAssetPathName, inCaseOfLinkHasItToBeRead);   

		int64_t variantOfMediaItemKey = -1;
		{
			string variantOfMediaItemKeyField = "VariantOfMediaItemKey";
			string variantOfUniqueNameField = "VariantOfUniqueName";
			string variantOfIngestionJobKeyField = "VariantOfIngestionJobKey";
			if (JSONUtils::isMetadataPresent(parametersRoot, variantOfMediaItemKeyField))
			{
				variantOfMediaItemKey = JSONUtils::asInt64(parametersRoot, variantOfMediaItemKeyField, -1);
			}
			else if (JSONUtils::isMetadataPresent(parametersRoot, variantOfUniqueNameField))
			{
				bool warningIfMissing = false;

				string variantOfUniqueName = parametersRoot.get(variantOfUniqueNameField, "").asString();

				pair<int64_t, MMSEngineDBFacade::ContentType> mediaItemKeyDetails =
					_mmsEngineDBFacade->getMediaItemKeyDetailsByUniqueName(
						localAssetIngestionEvent.getWorkspace()->_workspaceKey,
						variantOfUniqueName, warningIfMissing);
				tie(variantOfMediaItemKey, ignore) = mediaItemKeyDetails;
			}
			else if (JSONUtils::isMetadataPresent(parametersRoot, variantOfIngestionJobKeyField))
			{
				int64_t variantOfIngestionJobKey = JSONUtils::asInt64(parametersRoot, variantOfIngestionJobKeyField, -1);
				vector<tuple<int64_t,int64_t,MMSEngineDBFacade::ContentType>> mediaItemsDetails;
				bool warningIfMissing = false;

				_mmsEngineDBFacade->getMediaItemDetailsByIngestionJobKey(
					localAssetIngestionEvent.getWorkspace()->_workspaceKey, variantOfIngestionJobKey,
					mediaItemsDetails, warningIfMissing);

				if (mediaItemsDetails.size() != 1)
				{
					string errorMessage = string("IngestionJob does not refer the correct media Items number")
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", variantOfIngestionJobKey: " + to_string(variantOfIngestionJobKey)
						+ ", workspaceKey: " + to_string(localAssetIngestionEvent.getWorkspace()->_workspaceKey)
						+ ", mediaItemsDetails.size(): " + to_string(mediaItemsDetails.size())
					;
					_logger->error(__FILEREF__ + errorMessage);
        
					throw runtime_error(errorMessage);
				}

				tuple<int64_t,int64_t,MMSEngineDBFacade::ContentType> mediaItemsDetailsReturn
					= mediaItemsDetails[0];
				tie(variantOfMediaItemKey, ignore, ignore) = mediaItemsDetailsReturn;
			}
		}

		if (variantOfMediaItemKey == -1)
		{
			_logger->info(__FILEREF__ + "_mmsEngineDBFacade->saveSourceContentMetadata..."
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", contentType: " + MMSEngineDBFacade::toString(contentType)
				+ ", ExternalReadOnlyStorage: " + to_string(localAssetIngestionEvent.getExternalReadOnlyStorage())
				+ ", relativePathToBeUsed: " + relativePathToBeUsed
				+ ", mediaSourceFileName: " + mediaSourceFileName
				+ ", mmsPartitionUsed: " + to_string(mmsPartitionUsed)
				+ ", sizeInBytes: " + to_string(sizeInBytes)

				/*
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
				*/
				+ ", videoTracks.size: " + to_string(videoTracks.size())
				+ ", audioTracks.size: " + to_string(audioTracks.size())

				+ ", imageWidth: " + to_string(imageWidth)
				+ ", imageHeight: " + to_string(imageHeight)
				+ ", imageFormat: " + imageFormat
				+ ", imageQuality: " + to_string(imageQuality)
			);

			pair<int64_t,int64_t> mediaItemKeyAndPhysicalPathKey =
                _mmsEngineDBFacade->saveSourceContentMetadata (
                    localAssetIngestionEvent.getWorkspace(),
                    localAssetIngestionEvent.getIngestionJobKey(),
                    localAssetIngestionEvent.getIngestionRowToBeUpdatedAsSuccess(),
                    contentType,
                    parametersRoot,
					localAssetIngestionEvent.getExternalReadOnlyStorage(),
                    relativePathToBeUsed,
                    mediaSourceFileName,
                    mmsPartitionUsed,
                    sizeInBytes,
                
                    // video-audio
					mediaInfoDetails,
					videoTracks,
					audioTracks,
					/*
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
					*/

                    // image
                    imageWidth,
                    imageHeight,
                    imageFormat,
                    imageQuality
			);

			int64_t mediaItemKey = mediaItemKeyAndPhysicalPathKey.first;

			_logger->info(__FILEREF__ + "Added a new ingested content"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", mediaItemKey: " + to_string(mediaItemKeyAndPhysicalPathKey.first)
				+ ", physicalPathKey: " + to_string(mediaItemKeyAndPhysicalPathKey.second)
			);
		}
		else
		{
			int64_t liveRecordingIngestionJobKey = -1;
			int64_t encodingProfileKey = -1;

			string externalDeliveryTechnology;
			string externalDeliveryURL;
			{
				string field = "ExternalDeliveryTechnology";
				if (JSONUtils::isMetadataPresent(parametersRoot, field))
					externalDeliveryTechnology = parametersRoot.get(field, "").asString();

				field = "ExternalDeliveryURL";
				if (JSONUtils::isMetadataPresent(parametersRoot, field))
					externalDeliveryURL = parametersRoot.get(field, "").asString();
			}

			_logger->info(__FILEREF__ + "_mmsEngineDBFacade->saveVariantContentMetadata..."
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", workspaceKey: " + to_string(localAssetIngestionEvent.getWorkspace()->_workspaceKey)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", liveRecordingIngestionJobKey: " + to_string(liveRecordingIngestionJobKey)
				+ ", variantOfMediaItemKey: " + to_string(variantOfMediaItemKey)
				+ ", ExternalReadOnlyStorage: " + to_string(localAssetIngestionEvent.getExternalReadOnlyStorage())
				+ ", externalDeliveryTechnology: " + externalDeliveryTechnology
				+ ", externalDeliveryURL: " + externalDeliveryURL

				+ ", mediaSourceFileName: " + mediaSourceFileName
				+ ", relativePathToBeUsed: " + relativePathToBeUsed
				+ ", mmsPartitionUsed: " + to_string(mmsPartitionUsed)
				+ ", sizeInBytes: " + to_string(sizeInBytes)
				+ ", encodingProfileKey: " + to_string(encodingProfileKey)

				+ ", videoTracks.size: " + to_string(videoTracks.size())
				+ ", audioTracks.size: " + to_string(audioTracks.size())
				/*
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
				*/

				+ ", imageWidth: " + to_string(imageWidth)
				+ ", imageHeight: " + to_string(imageHeight)
				+ ", imageFormat: " + imageFormat
				+ ", imageQuality: " + to_string(imageQuality)
			);

			int64_t physicalPathKey = _mmsEngineDBFacade->saveVariantContentMetadata (
                    localAssetIngestionEvent.getWorkspace()->_workspaceKey,
                    localAssetIngestionEvent.getIngestionJobKey(),
					liveRecordingIngestionJobKey,
					variantOfMediaItemKey,
					localAssetIngestionEvent.getExternalReadOnlyStorage(),
					externalDeliveryTechnology,
					externalDeliveryURL,

                    mediaSourceFileName,
                    relativePathToBeUsed,
                    mmsPartitionUsed,
                    sizeInBytes,
					encodingProfileKey,
                
                    // video-audio
					mediaInfoDetails,
					videoTracks,
					audioTracks,
					/*
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
					*/

                    // image
                    imageWidth,
                    imageHeight,
                    imageFormat,
                    imageQuality
			);
			_logger->info(__FILEREF__ + "Added a new variant content"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", variantOfMediaItemKey,: " + to_string(variantOfMediaItemKey)
				+ ", physicalPathKey: " + to_string(physicalPathKey)
			);

			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", IngestionStatus: " + "End_TaskSuccess"
				+ ", errorMessage: " + ""
			);                            
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
			);
		}
    }
    catch(MediaItemKeyNotFound e)	// getMediaItemDetailsByIngestionJobKey failure
    {
        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->getMediaItemDetailsByIngestionJobKey failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", workspaceKey: " + to_string(localAssetIngestionEvent.getWorkspace()->_workspaceKey)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
			+ ", e.what: " + e.what()
        );

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", mmsAssetPathName: " + mmsAssetPathName
				);

				FileIO::remove(mmsAssetPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
			+ ", IngestionStatus: " + "End_IngestionFailure"
			+ ", errorMessage: " + e.what()
		);                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->saveSourceContentMetadata failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", e.what: " + e.what()
        );

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", mmsAssetPathName: " + mmsAssetPathName
				);

				FileIO::remove(mmsAssetPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->saveSourceContentMetadata failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
        );

		if (!localAssetIngestionEvent.getExternalReadOnlyStorage())
		{
			try
			{
				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", mmsAssetPathName: " + mmsAssetPathName
				);

				FileIO::remove(mmsAssetPathName);
			}
			catch(runtime_error e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
			catch(exception e)
			{
				_logger->info(__FILEREF__ + "remove failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
					+ ", errorMessage: " + e.what()
				);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent.getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(localAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
				);
		}

        // throw e;
		return;	// return because it is a thread
    }
}

/*
void MMSEngineProcessor::exploidTarGzContentFile(
	string tarGzBinaryPathName,
	string workspaceIngestionBinaryPathName,
	int64_t ingestionJobKey)
{
	// string binaryPathName;

	try
	{
		string tarGzSuffix(".tar.gz");
		if (tarGzBinaryPathName.size() >= tarGzSuffix.size() && 0 == tarGzBinaryPathName.compare(
			tarGzBinaryPathName.size()-tarGzSuffix.size(), tarGzSuffix.size(), tarGzSuffix))
		{
			size_t binaryPathNameIndex = tarGzBinaryPathName.find_last_of(".tar.gz");
			if (binaryPathNameIndex == string::npos)
			{
				// error
			}

			binaryPathName = tarGzBinaryPathName.substr(0, binaryPathNameIndex);
		}
		else
		{
			// error
		}


		string m3u8FileName;
		{
			try
			{
				FileIO::DirectoryEntryType_t detDirectoryEntryType;
				shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (
					binaryPathName + "/");
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

						string m3u8Suffix(".m3u8");
						if (directoryEntry.size() >= m3u8Suffix.size()
								&& 0 == directoryEntry.compare(
									directoryEntry.size()-m3u8Suffix.size(),
									m3u8Suffix.size(), m3u8Suffix))
						{
							m3u8FileName = directoryEntry;

							scanDirectoryFinished = true;
						}
					}
					catch(DirectoryListFinished e)
					{
						scanDirectoryFinished = true;
					}
					catch(runtime_error e)
					{
						string errorMessage = __FILEREF__ + "listing directory failed"
							+ ", e.what(): " + e.what()
						;
						_logger->error(errorMessage);

						throw e;
					}
					catch(exception e)
					{
						string errorMessage = __FILEREF__ + "listing directory failed"
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
				// error
			}
			catch(exception e)
			{
				// error
			}
		}

		if (m3u8FileName == "")
		{
			// error
		}

		binaryPathName += ("/" + m3u8FileName);
	}
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "exploidTarGzContentFile failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "exploidTarGzContentFile failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );

        throw e;
    }

	// return binaryPathName;
}
*/

void MMSEngineProcessor::manageGroupOfTasks(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot)
{
    try
    {
		vector<pair<int64_t, int64_t>>	referencesOutput;

		Validator validator(_logger, _mmsEngineDBFacade, _configuration);
		// ReferencesOutput tag is always present:
		// 1. because it is already set by the Workflow (by the user)
		// 2. because it is automatically set by API_Ingestion.cpp using the list of Tasks.
		//	This is when it was not found into the Workflow
		validator.fillReferencesOutput(workspace->_workspaceKey, parametersRoot,
				referencesOutput);

		int64_t liveRecordingIngestionJobKey = -1;
		for (pair<int64_t, int64_t>  referenceOutput: referencesOutput)
		{
			_logger->info(__FILEREF__ + "References.Output"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", mediaItemKey: " + to_string(referenceOutput.first)
				+ ", physicalPathKey: " + to_string(referenceOutput.second)
			);
			_mmsEngineDBFacade->addIngestionJobOutput(ingestionJobKey,
				referenceOutput.first, referenceOutput.second, liveRecordingIngestionJobKey);
		}

		/*
		 * 2019-09-23: It is not clean now how to manage the status of the GroupOfTasks:
		 *	- depend on the status of his children (first level of Tasks of the GroupOfTasks)
		 *		as calculated below (now commented)?
		 *	- depend on the ReferencesOutput?
		 *
		 *	Since this is not clean, I left it always Success
		 *
		 */
		/*
		// GroupOfTasks Ingestion Status is by default Failure;
		// It will be Success if at least just one Status of the children is Success
		MMSEngineDBFacade::IngestionStatus groupOfTasksIngestionStatus
			= MMSEngineDBFacade::IngestionStatus::End_IngestionFailure;
		{
			vector<pair<int64_t, MMSEngineDBFacade::IngestionStatus>> groupOfTasksChildrenStatus;

			_mmsEngineDBFacade->getGroupOfTasksChildrenStatus(ingestionJobKey, groupOfTasksChildrenStatus);

			for (pair<int64_t, MMSEngineDBFacade::IngestionStatus> groupOfTasksChildStatus: groupOfTasksChildrenStatus)
			{
				int64_t childIngestionJobKey = groupOfTasksChildStatus.first;
				MMSEngineDBFacade::IngestionStatus childStatus = groupOfTasksChildStatus.second;

				_logger->info(__FILEREF__ + "manageGroupOfTasks, child status"
						+ ", group of tasks ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", childIngestionJobKey: " + to_string(childIngestionJobKey)
						+ ", IngestionStatus: " + MMSEngineDBFacade::toString(childStatus)
				);

				if (!MMSEngineDBFacade::isIngestionStatusFinalState(childStatus))
				{
					_logger->error(__FILEREF__ + "manageGroupOfTasks, child status is not a final status. It should never happens because when this GroupOfTasks is executed, all the children should be finished"
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", IngestionStatus: " + MMSEngineDBFacade::toString(childStatus)
					);

					continue;
				}

				if (childStatus == MMSEngineDBFacade::IngestionStatus::End_TaskSuccess)
				{
					groupOfTasksIngestionStatus = MMSEngineDBFacade::IngestionStatus::End_TaskSuccess;

					break;
				}
			}
		}
		*/
		MMSEngineDBFacade::IngestionStatus groupOfTasksIngestionStatus
			= MMSEngineDBFacade::IngestionStatus::End_TaskSuccess;

		string errorMessage = "";
		if (groupOfTasksIngestionStatus != MMSEngineDBFacade::IngestionStatus::End_TaskSuccess)
			errorMessage = "Failed because there is no one child with Status Success";

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + MMSEngineDBFacade::toString(groupOfTasksIngestionStatus)
            + ", errorMessage: " + errorMessage
        );
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                groupOfTasksIngestionStatus,
                errorMessage
        );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageGroupOfTasks failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageGroupOfTasks failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::removeContentTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any media to be removed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

		bool multipleInput_ReturnErrorInCaseOfOneFailure = false;
		string field = "MultipleInput_ReturnErrorInCaseOfOneFailure";
		if (JSONUtils::isMetadataPresent(parametersRoot, field))
			multipleInput_ReturnErrorInCaseOfOneFailure = JSONUtils::asBool(parametersRoot, field, false);

		int dependencyIndex = 0;
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType:
				dependencies)
        {
			try
			{
				int64_t key;
				MMSEngineDBFacade::ContentType referenceContentType;
				Validator::DependencyType dependencyType;

				tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

				// check if there are ingestion dependencies on this media item
				{
					if (dependencyType == Validator::DependencyType::MediaItemKey)
					{
						bool warningIfMissing = false;
						tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
							contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
							_mmsEngineDBFacade->getMediaItemKeyDetails(
								workspace->_workspaceKey, key, warningIfMissing);

						MMSEngineDBFacade::ContentType localContentType;
						string localTitle;
						string localUserData;
						string localIngestionDate;
						int64_t localIngestionJobKey;
						tie(localContentType, localTitle, localUserData, localIngestionDate, ignore, localIngestionJobKey)
							= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;

						int ingestionDependenciesNumber = 
							_mmsEngineDBFacade->getNotFinishedIngestionDependenciesNumberByIngestionJobKey(
									localIngestionJobKey);
						if (ingestionDependenciesNumber > 0)
						{
							string errorMessage = __FILEREF__ + "MediaItem cannot be removed because there are still ingestion dependencies"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", ingestionDependenciesNumber: " + to_string(ingestionDependenciesNumber);
							_logger->error(errorMessage);

							throw runtime_error(errorMessage);
						}
					}
					else
					{
						bool warningIfMissing = false;
						tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
							mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
							_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
								workspace->_workspaceKey, key, warningIfMissing);

						int64_t localMediaItemKey;
						MMSEngineDBFacade::ContentType localContentType;
						string localTitle;
						string localUserData;
						string localIngestionDate;
						int64_t localIngestionJobKey;
						tie(localMediaItemKey, localContentType, localTitle, localUserData, localIngestionDate,
							localIngestionJobKey, ignore)
							= mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;

						int ingestionDependenciesNumber = 
						_mmsEngineDBFacade->getNotFinishedIngestionDependenciesNumberByIngestionJobKey(
								localIngestionJobKey);
						if (ingestionDependenciesNumber > 0)
						{
							string errorMessage = __FILEREF__ + "MediaItem cannot be removed because there are still ingestion dependencies"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", ingestionDependenciesNumber: " + to_string(ingestionDependenciesNumber);
							_logger->error(errorMessage);

							throw runtime_error(errorMessage);
						}
					}
				}

				if (dependencyType == Validator::DependencyType::MediaItemKey)
				{
					_logger->info(__FILEREF__ + "removeMediaItem"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", mediaItemKey: " + to_string(key)
					);
					_mmsStorage->removeMediaItem(_mmsEngineDBFacade, key);
				}
				else
				{
					_logger->info(__FILEREF__ + "removePhysicalPath"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", physicalPathKey: " + to_string(key)
					);
					_mmsStorage->removePhysicalPath(_mmsEngineDBFacade, key);
				}
			}
			catch(runtime_error e)
			{
				string errorMessage = __FILEREF__ + "Remove Content failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", dependencyIndex: " + to_string(dependencyIndex);
					+ ", dependencies.size(): " + to_string(dependencies.size())
					+ ", e.what(): " + e.what()
				;
				_logger->error(errorMessage);

				if (dependencies.size() > 1)
				{
					if (multipleInput_ReturnErrorInCaseOfOneFailure)
						throw runtime_error(errorMessage);
				}
				else
					throw runtime_error(errorMessage);
			}
			catch (exception e)
			{
				string errorMessage = __FILEREF__ + "Remove Content failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", dependencyIndex: " + to_string(dependencyIndex);
					+ ", dependencies.size(): " + to_string(dependencies.size())
				;
				_logger->error(errorMessage);

				if (dependencies.size() > 1)
				{
					if (multipleInput_ReturnErrorInCaseOfOneFailure)
						throw runtime_error(errorMessage);
				}
				else
					throw runtime_error(errorMessage);
			}

			dependencyIndex++;
		}

		_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_TaskSuccess"
				+ ", errorMessage: " + ""
		);
		_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
			MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
			"" // errorMessage
		);
	}
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "removeContentTask failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "removeContentTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::ftpDeliveryContentTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any media to be uploaded (FTP)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        if (_processorsThreadsNumber.use_count() + dependencies.size() >
				_processorThreads + _maxAdditionalProcessorThreads)
        {
            _logger->warn(__FILEREF__ + "Not enough available threads to manage ftpUploadMediaSourceThread, activity is postponed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
            );

            string errorMessage = "";
            string processorMMS = "";

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                + ", errorMessage: " + errorMessage
                + ", processorMMS: " + processorMMS
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                    ingestionStatus, 
                    errorMessage,
                    processorMMS
                    );
            
            return;
        }

        string configurationLabel;
        {
            string field = "ConfigurationLabel";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            configurationLabel = parametersRoot.get(field, "XXX").asString();
        }
        
        string ftpServer;
        int ftpPort;
        string ftpUserName;
        string ftpPassword;
        string ftpRemoteDirectory;

        tuple<string, int, string, string, string> ftp = _mmsEngineDBFacade->getFTPByConfigurationLabel(
                workspace->_workspaceKey, configurationLabel);            
        tie(ftpServer, ftpPort, ftpUserName, ftpPassword, ftpRemoteDirectory) = ftp;

        // for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType:
		// 		dependencies)
        for (int dependencyIndex = 0; dependencyIndex < dependencies.size(); dependencyIndex++)
        {
			string mmsAssetPathName;
			string fileName;
			int64_t sizeInBytes;
			string deliveryFileName;
			int64_t mediaItemKey;
			int64_t physicalPathKey;

			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType =
				dependencies[dependencyIndex];
            
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;
            
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
				mediaItemKey = key;

                int64_t encodingProfileKey = -1;
               
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(physicalPathKey, mmsAssetPathName, ignore, ignore, fileName, sizeInBytes, deliveryFileName)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
            }
            else
            {
				physicalPathKey = key;

				{
					bool warningIfMissing = false;
					tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
						mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
						_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
							workspace->_workspaceKey, physicalPathKey, warningIfMissing);            
					tie(mediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore) =
						mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
				}

				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, fileName, sizeInBytes, deliveryFileName)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;
            }

			bool updateIngestionJobToBeDone;
			if (dependencyIndex + 1 >= dependencies.size())
				updateIngestionJobToBeDone = true;
			else
				updateIngestionJobToBeDone = false;

            // check on thread availability was done at the beginning in this method
			thread ftpUploadMediaSource(&MMSEngineProcessor::ftpUploadMediaSourceThread, this, 
				_processorsThreadsNumber, mmsAssetPathName, fileName, sizeInBytes,
				ingestionJobKey, workspace, mediaItemKey, physicalPathKey,
				ftpServer, ftpPort, ftpUserName, ftpPassword,
				ftpRemoteDirectory, deliveryFileName, updateIngestionJobToBeDone);
			ftpUploadMediaSource.detach();
		}
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "ftpDeliveryContentTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "ftpDeliveryContentTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::postOnFacebookTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any media to be posted on Facebook"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        if (_processorsThreadsNumber.use_count() + dependencies.size() >
				_processorThreads + _maxAdditionalProcessorThreads)
        {
            _logger->warn(__FILEREF__ + "Not enough available threads to manage postOnFacebookTask, activity is postponed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
            );

            string errorMessage = "";
            string processorMMS = "";

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                + ", errorMessage: " + errorMessage
                + ", processorMMS: " + processorMMS
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                    ingestionStatus, 
                    errorMessage,
                    processorMMS
                    );
            
            return;
        }

        string facebookConfigurationLabel;
        string facebookNodeId;
        {
            string field = "ConfigurationLabel";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            facebookConfigurationLabel = parametersRoot.get(field, "XXX").asString();

            field = "NodeId";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            facebookNodeId = parametersRoot.get(field, "XXX").asString();
        }
        
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType:
				dependencies)
        {
			string mmsAssetPathName;
            int64_t sizeInBytes;
			/*
            int mmsPartitionNumber;
            string workspaceDirectoryName;
            string relativePath;
            string fileName;
            string deliveryFileName;
			*/
            MMSEngineDBFacade::ContentType contentType;
            
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;
            
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(ignore, mmsAssetPathName, ignore, ignore, ignore, sizeInBytes, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(
                        key, encodingProfileKey);

                int64_t physicalPathKey;
                shared_ptr<Workspace> workspace;
                string title;
                
                tie(physicalPathKey, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/

                {
                    bool warningIfMissing = false;
                    tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
						contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
                        _mmsEngineDBFacade->getMediaItemKeyDetails(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(contentType, ignore, ignore, ignore, ignore, ignore)
						= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
                }
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, ignore, sizeInBytes, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(key);

                shared_ptr<Workspace> workspace;
                string title;
                
                tie(ignore, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
                
                {
                    bool warningIfMissing = false;
                    tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
						mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                        _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(ignore, contentType, ignore, ignore, ignore, ignore, ignore)
                            = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
                }
            }

			/*
            _logger->info(__FILEREF__ + "getMMSAssetPathName ..."
                + ", mmsPartitionNumber: " + to_string(mmsPartitionNumber)
                + ", workspaceDirectoryName: " + workspaceDirectoryName
                + ", relativePath: " + relativePath
                + ", fileName: " + fileName
            );
            string mmsAssetPathName = _mmsStorage->getMMSAssetPathName(
                mmsPartitionNumber,
                workspaceDirectoryName,
                relativePath,
                fileName);
			*/

            // check on thread availability was done at the beginning in this method
            if (contentType == MMSEngineDBFacade::ContentType::Video)
            {
                thread postOnFacebook(&MMSEngineProcessor::postVideoOnFacebookThread, this,
                    _processorsThreadsNumber, mmsAssetPathName, 
                    sizeInBytes, ingestionJobKey, workspace,
                    facebookNodeId, facebookConfigurationLabel);
                postOnFacebook.detach();
            }
            else // if (contentType == ContentType::Audio)
            {
                /*
                thread postOnFacebook(&MMSEngineProcessor::postVideoOnFacebookThread, this,
                    _processorsThreadsNumber, mmsAssetPathName, 
                    sizeInBytes, ingestionJobKey, workspace,
                    facebookNodeId, facebookAccessToken);
                postOnFacebook.detach();
                */
            }
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "postOnFacebookTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "postOnFacebookTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::postOnYouTubeTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any media to be posted on YouTube"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        if (_processorsThreadsNumber.use_count() + dependencies.size() >
				_processorThreads + _maxAdditionalProcessorThreads)
        {
            _logger->warn(__FILEREF__ + "Not enough available threads to manage postOnYouTubeTask, activity is postponed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
            );

            string errorMessage = "";
            string processorMMS = "";

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                + ", errorMessage: " + errorMessage
                + ", processorMMS: " + processorMMS
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                    ingestionStatus, 
                    errorMessage,
                    processorMMS
                    );
            
            return;
        }

        string youTubeConfigurationLabel;
        string youTubeTitle;
        string youTubeDescription;
        Json::Value youTubeTags = Json::nullValue;
        int youTubeCategoryId = -1;
        string youTubePrivacy;
        {
            string field = "ConfigurationLabel";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            youTubeConfigurationLabel = parametersRoot.get(field, "XXX").asString();

            field = "Title";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
                youTubeTitle = parametersRoot.get(field, "XXX").asString();

            field = "Description";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
                youTubeDescription = parametersRoot.get(field, "XXX").asString();
            
            field = "Tags";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
                youTubeTags = parametersRoot[field];
            
            field = "CategoryId";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
                youTubeCategoryId = JSONUtils::asInt(parametersRoot, field, 0);

            field = "Privacy";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
                youTubePrivacy = parametersRoot.get(field, "XXX").asString();
            else
                youTubePrivacy = "private";
        }
        
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType:
				dependencies)
        {
			string mmsAssetPathName;
            int64_t sizeInBytes;
			/*
            int mmsPartitionNumber;
            string workspaceDirectoryName;
            string relativePath;
            string fileName;
            string deliveryFileName;
			*/
            MMSEngineDBFacade::ContentType contentType;
            string title;
            
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;
            
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(ignore, mmsAssetPathName, ignore, ignore, ignore, sizeInBytes, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(
                        key, encodingProfileKey);

                int64_t physicalPathKey;
                shared_ptr<Workspace> workspace;
                
                tie(physicalPathKey, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/

                {
                    bool warningIfMissing = false;
                    tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
						contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
                        _mmsEngineDBFacade->getMediaItemKeyDetails(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(contentType, ignore, ignore, ignore, ignore, ignore)
						= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
                }
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, ignore, sizeInBytes, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(key);

                shared_ptr<Workspace> workspace;
                
                tie(ignore, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
                
                {
                    bool warningIfMissing = false;
                    tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
						mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                        _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(ignore, contentType, ignore, ignore, ignore, ignore, ignore)
                            = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
                }
            }
            
            if (youTubeTitle == "")
                youTubeTitle = title;

			/*
            _logger->info(__FILEREF__ + "getMMSAssetPathName ..."
                + ", mmsPartitionNumber: " + to_string(mmsPartitionNumber)
                + ", workspaceDirectoryName: " + workspaceDirectoryName
                + ", relativePath: " + relativePath
                + ", fileName: " + fileName
            );
            string mmsAssetPathName = _mmsStorage->getMMSAssetPathName(
                mmsPartitionNumber,
                workspaceDirectoryName,
                relativePath,
                fileName);
			*/

            // check on thread availability was done at the beginning in this method
            thread postOnYouTube(&MMSEngineProcessor::postVideoOnYouTubeThread, this,
                _processorsThreadsNumber, mmsAssetPathName, 
                sizeInBytes, ingestionJobKey, workspace,
                youTubeConfigurationLabel, youTubeTitle,
                youTubeDescription, youTubeTags,
                youTubeCategoryId, youTubePrivacy);
            postOnYouTube.detach();
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "postOnYouTubeTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "postOnYouTubeTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::httpCallbackTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        /*
         * dependencies could be even empty
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any media to be notified (HTTP Callback)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
         */

        if (_processorsThreadsNumber.use_count() > _processorThreads + _maxAdditionalProcessorThreads)
        {
            _logger->warn(__FILEREF__
				+ "Not enough available threads to manage userHttpCallbackThread, activity is postponed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                + ", _processorThreads + _maxAdditionalProcessorThreads: "
					+ to_string(_processorThreads + _maxAdditionalProcessorThreads)
            );

            string errorMessage = "";
            string processorMMS = "";

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                + ", errorMessage: " + errorMessage
                + ", processorMMS: " + processorMMS
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                    ingestionStatus, 
                    errorMessage,
                    processorMMS
                    );
            
            return;
        }

        string httpProtocol;
        string httpHostName;
        int httpPort;
        string httpURI;
        string httpURLParameters;
        string httpMethod;
        long callbackTimeoutInSeconds;
		int maxRetries;
        Json::Value httpHeadersRoot(Json::arrayValue);
        {
            string field = "Protocol";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                httpProtocol = "http";
            }
            else
            {
                httpProtocol = parametersRoot.get(field, "XXX").asString();
                if (httpProtocol == "")
                    httpProtocol = "http";
            }
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", httpProtocol: " + httpProtocol
            );

            field = "HostName";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            httpHostName = parametersRoot.get(field, "XXX").asString();
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", httpHostName: " + httpHostName
            );

            field = "Port";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                if (httpProtocol == "http")
                    httpPort = 80;
                else
                    httpPort = 443;
            }
            else
                httpPort = JSONUtils::asInt(parametersRoot, field, 0);
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", httpPort: " + to_string(httpPort)
            );

            field = "Timeout";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
                callbackTimeoutInSeconds = 120;
            else
                callbackTimeoutInSeconds = JSONUtils::asInt(parametersRoot, field, 0);
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", callbackTimeoutInSeconds: " + to_string(callbackTimeoutInSeconds)
            );
            
            field = "URI";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            httpURI = parametersRoot.get(field, "XXX").asString();
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", httpURI: " + httpURI
            );

            field = "Parameters";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                httpURLParameters = parametersRoot.get(field, "XXX").asString();
            }
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", httpURLParameters: " + httpURLParameters
            );

            field = "Method";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                httpMethod = "POST";
            }
            else
            {
                httpMethod = parametersRoot.get(field, "XXX").asString();
                if (httpMethod == "")
                    httpMethod = "POST";
            }
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", httpMethod: " + httpMethod
            );
            
            field = "Headers";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
				// semicolon as separator
				stringstream ss(parametersRoot.get(field, "XXX").asString());
				string token;
				char delim = ';';
				while (getline(ss, token, delim))
				{
					if (!token.empty())
						httpHeadersRoot.append(token);
				}
                // httpHeadersRoot = parametersRoot[field];
            }

            field = "MaxRetries";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
				maxRetries = 2;
            }
            else
			{
                maxRetries = JSONUtils::asInt(parametersRoot, field, 3);
				if (maxRetries == 0)
					maxRetries = 2;
			}
            _logger->info(__FILEREF__ + "Retrieved configuration parameter"
                    + ", maxRetries: " + to_string(maxRetries)
            );
        }

        Json::Value callbackMedatada;
        {
            for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType:
					dependencies)
            {
                int64_t key;
                MMSEngineDBFacade::ContentType referenceContentType;
                Validator::DependencyType dependencyType;

                tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

				callbackMedatada["workspaceKey"] = (int64_t) (workspace->_workspaceKey);

				MMSEngineDBFacade::ContentType contentType;
				int64_t physicalPathKey;
				int64_t mediaItemKey;

                if (dependencyType == Validator::DependencyType::MediaItemKey)
                {
					mediaItemKey = key;

                    callbackMedatada["mediaItemKey"] = mediaItemKey;

					{
						bool warningIfMissing = false;
						tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
							contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
							_mmsEngineDBFacade->getMediaItemKeyDetails(
								workspace->_workspaceKey, mediaItemKey, warningIfMissing);

						string localTitle;
						string userData;
						tie(contentType, localTitle, userData, ignore, ignore, ignore)
							= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;

						callbackMedatada["title"] = localTitle;

						if (userData == "")
							callbackMedatada["userData"] = Json::nullValue;
						else
						{
							Json::Value userDataRoot;
							{
								Json::CharReaderBuilder builder;
								Json::CharReader* reader = builder.newCharReader();
								string errors;

								bool parsingSuccessful = reader->parse(userData.c_str(),
                                    userData.c_str() + userData.size(), 
                                    &userDataRoot, &errors);
								delete reader;

								if (!parsingSuccessful)
								{
									string errorMessage = __FILEREF__ + "failed to parse the userData"
                                        + ", errors: " + errors
                                        + ", userData: " + userData
                                        ;
									_logger->error(errorMessage);

									throw runtime_error(errors);
								}
							}

							callbackMedatada["userData"] = userDataRoot;
						}
					}

					{
						int64_t encodingProfileKey = -1;
						bool warningIfMissing = false;
						tuple<int64_t, string, int, string, string, int64_t, string> physicalPathDetails =
							_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);

						string physicalPath;
						string fileName;
						int64_t sizeInBytes;
						string deliveryFileName;

						tie(physicalPathKey, physicalPath, ignore, ignore, fileName, ignore, ignore) = physicalPathDetails;

						callbackMedatada["physicalPathKey"] = physicalPathKey;
						callbackMedatada["fileName"] = fileName;
						// callbackMedatada["physicalPath"] = physicalPath;
					}
                }
                else
                {
					physicalPathKey = key;

                    callbackMedatada["physicalPathKey"] = physicalPathKey;

					{
						bool warningIfMissing = false;
						tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
							mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
							_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
								workspace->_workspaceKey, physicalPathKey, warningIfMissing);

						string localTitle;
						string userData;
						tie(mediaItemKey, contentType, localTitle, userData, ignore, ignore, ignore)
                            = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;

						callbackMedatada["mediaItemKey"] = mediaItemKey;
						callbackMedatada["title"] = localTitle;

						if (userData == "")
							callbackMedatada["userData"] = Json::nullValue;
						else
						{
							Json::Value userDataRoot;
							{
								Json::CharReaderBuilder builder;
								Json::CharReader* reader = builder.newCharReader();
								string errors;

								bool parsingSuccessful = reader->parse(userData.c_str(),
                                    userData.c_str() + userData.size(), 
                                    &userDataRoot, &errors);
								delete reader;

								if (!parsingSuccessful)
								{
									string errorMessage = __FILEREF__ + "failed to parse the userData"
                                        + ", errors: " + errors
                                        + ", userData: " + userData
                                        ;
									_logger->error(errorMessage);

									throw runtime_error(errors);
								}
							}

							callbackMedatada["userData"] = userDataRoot;
						}
					}

					{
						int64_t encodingProfileKey = -1;
						tuple<string, int, string, string, int64_t, string> physicalPathDetails =
							_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, physicalPathKey);

						string physicalPath;
						string fileName;
						int64_t sizeInBytes;
						string deliveryFileName;

						tie(physicalPath, ignore, ignore, fileName, ignore, ignore) = physicalPathDetails;

						callbackMedatada["fileName"] = fileName;
						// callbackMedatada["physicalPath"] = physicalPath;
					}
                }

				if (contentType == MMSEngineDBFacade::ContentType::Video
					|| contentType == MMSEngineDBFacade::ContentType::Audio)
				{
					try
					{
						int64_t durationInMilliSeconds =
							_mmsEngineDBFacade->getMediaDurationInMilliseconds(
							mediaItemKey, physicalPathKey);

						float durationInSeconds = durationInMilliSeconds / 1000;

						callbackMedatada["durationInSeconds"] = durationInSeconds;
					}
					catch(runtime_error e)
					{
						_logger->error(__FILEREF__ + "getMediaDurationInMilliseconds failed"
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", mediaItemKey: " + to_string(mediaItemKey)
							+ ", physicalPathKey: " + to_string(physicalPathKey)
							+ ", exception: " + e.what()
						);
					}
					catch(exception e)
					{
						_logger->error(__FILEREF__ + "getMediaDurationInMilliseconds failed"
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", mediaItemKey: " + to_string(mediaItemKey)
							+ ", physicalPathKey: " + to_string(physicalPathKey)
						);
					}
				}
				/*
				if (contentType == MMSEngineDBFacade::ContentType::Video)
				{
					int64_t durationInMilliSeconds;

					tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long>
						videoDetails = _mmsEngineDBFacade->getVideoDetails(mediaItemKey, physicalPathKey);

					tie(durationInMilliSeconds, ignore,
						ignore, ignore, ignore, ignore, ignore, ignore,
						ignore, ignore, ignore, ignore) = videoDetails;

					float durationInSeconds = durationInMilliSeconds / 1000;

					callbackMedatada["durationInSeconds"] = durationInSeconds;
				}
				else if (contentType == MMSEngineDBFacade::ContentType::Audio)
				{
					int64_t durationInMilliSeconds;

					tuple<int64_t,string,long,long,int> audioDetails
						= _mmsEngineDBFacade->getAudioDetails(mediaItemKey, physicalPathKey);

					tie(durationInMilliSeconds, ignore, ignore, ignore, ignore)
						= audioDetails;

					float durationInSeconds = durationInMilliSeconds / 1000;

					callbackMedatada["durationInSeconds"] = durationInSeconds;
				}
				*/
			}
        }

        // check on thread availability was done at the beginning in this method
        thread httpCallbackThread(&MMSEngineProcessor::userHttpCallbackThread, this, 
            _processorsThreadsNumber, ingestionJobKey, httpProtocol, httpHostName, 
            httpPort, httpURI, httpURLParameters, httpMethod, callbackTimeoutInSeconds,
            httpHeadersRoot, callbackMedatada, maxRetries);
        httpCallbackThread.detach();
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "httpCallbackTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "httpCallbackTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::localCopyContentTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any media to be copied"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        if (_processorsThreadsNumber.use_count() + dependencies.size() >
				_processorThreads + _maxAdditionalProcessorThreads)
        {
            _logger->warn(__FILEREF__ + "Not enough available threads to manage copyContentThread, activity is postponed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", _processorsThreadsNumber.use_count(): " + to_string(_processorsThreadsNumber.use_count())
                + ", _processorThreads + _maxAdditionalProcessorThreads: " + to_string(_processorThreads + _maxAdditionalProcessorThreads)
            );

            string errorMessage = "";
            string processorMMS = "";

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + MMSEngineDBFacade::toString(ingestionStatus)
                + ", errorMessage: " + errorMessage
                + ", processorMMS: " + processorMMS
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                    ingestionStatus, 
                    errorMessage,
                    processorMMS
                    );
            
            return;
        }

        string localPath;
        string localFileName;
        {
            string field = "LocalPath";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            localPath = parametersRoot.get(field, "XXX").asString();

            field = "LocalFileName";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                localFileName = parametersRoot.get(field, "XXX").asString();
            }
        }
        
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType:
				dependencies)
        {
			string mmsAssetPathName;
			/*
            int mmsPartitionNumber;
            string workspaceDirectoryName;
            string relativePath;
            string fileName;
            int64_t sizeInBytes;
            string deliveryFileName;
			*/
			string fileFormat;
            
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;
            
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

			int64_t physicalPathKey;
            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(ignore, mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(
                        key, encodingProfileKey);

                shared_ptr<Workspace> workspace;
                string title;
                
                tie(physicalPathKey, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
            }
            else
            {
				physicalPathKey = key;

				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(key);

                shared_ptr<Workspace> workspace;
                string title;
                
                tie(ignore, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
            }

			/*
            _logger->info(__FILEREF__ + "getMMSAssetPathName to be copied"
                + ", physicalPathKey: " + to_string(physicalPathKey)
                + ", mmsPartitionNumber: " + to_string(mmsPartitionNumber)
                + ", workspaceDirectoryName: " + workspaceDirectoryName
                + ", relativePath: " + relativePath
                + ", fileName: " + fileName
            );
            string mmsAssetPathName = _mmsStorage->getMMSAssetPathName(
                mmsPartitionNumber,
                workspaceDirectoryName,
                relativePath,
                fileName);
			*/
            
            // check on thread availability was done at the beginning in this method
            thread copyContentThread(&MMSEngineProcessor::copyContentThread, this, 
                _processorsThreadsNumber, ingestionJobKey, mmsAssetPathName,
				localPath, localFileName);
            copyContentThread.detach();
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "localCopyContentTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
                
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "localCopyContentTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageFaceRecognitionMediaTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong medias number to be processed for Face Recognition"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

		MMSEngineDBFacade::EncodingPriority encodingPriority;
		string field = "EncodingPriority";
		if (!JSONUtils::isMetadataPresent(parametersRoot, field))
		{
			encodingPriority = 
				static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
		}
		else
		{
			encodingPriority =
				MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
		}

        string faceRecognitionCascadeName;
        string faceRecognitionOutput;
		long initialFramesNumberToBeSkipped;
		bool oneFramePerSecond;
        {
            string field = "CascadeName";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            faceRecognitionCascadeName = parametersRoot.get(field, "XXX").asString();

            field = "Output";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            faceRecognitionOutput = parametersRoot.get(field, "XXX").asString();

			initialFramesNumberToBeSkipped = 0;
			oneFramePerSecond = true;
			if (faceRecognitionOutput == "FrameContainingFace")
			{
				field = "InitialFramesNumberToBeSkipped";
				if (JSONUtils::isMetadataPresent(parametersRoot, field))
					initialFramesNumberToBeSkipped = JSONUtils::asInt(parametersRoot, field, 0);

				field = "OneFramePerSecond";
				if (JSONUtils::isMetadataPresent(parametersRoot, field))
					oneFramePerSecond = JSONUtils::asBool(parametersRoot, field, false);
			}
        }
        
        // for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType: dependencies)
        {
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType	= dependencies[0];

			string mmsAssetPathName;
			/*
            int mmsPartitionNumber;
            string workspaceDirectoryName;
            string relativePath;
            string fileName;
            int64_t sizeInBytes;
            string deliveryFileName;
			*/
            MMSEngineDBFacade::ContentType contentType;
            string title;
			int64_t sourceMediaItemKey;
			int64_t sourcePhysicalPathKey;
            
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;
            
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(sourcePhysicalPathKey, mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;

				sourceMediaItemKey = key;

				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(
                        key, encodingProfileKey);

                shared_ptr<Workspace> workspace;
                
                tie(sourcePhysicalPathKey, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/

                {
                    bool warningIfMissing = false;
                    tuple<MMSEngineDBFacade::ContentType,string,string,string, int64_t, int64_t>
						contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
                        _mmsEngineDBFacade->getMediaItemKeyDetails(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(contentType, ignore, ignore, ignore, ignore, ignore)
						= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
                }
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;

				sourcePhysicalPathKey = key;

				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(key);

                shared_ptr<Workspace> workspace;
                
                tie(ignore, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
                
                {
                    bool warningIfMissing = false;
                    tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
						mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                        _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(sourceMediaItemKey, contentType, ignore, ignore, ignore, ignore, ignore)
                            = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
                }
            }
            
			/*
            _logger->info(__FILEREF__ + "getMMSAssetPathName ..."
                + ", mmsPartitionNumber: " + to_string(mmsPartitionNumber)
                + ", workspaceDirectoryName: " + workspaceDirectoryName
                + ", relativePath: " + relativePath
                + ", fileName: " + fileName
            );
            string mmsAssetPathName = _mmsStorage->getMMSAssetPathName(
                mmsPartitionNumber,
                workspaceDirectoryName,
                relativePath,
                fileName);
			*/

			_mmsEngineDBFacade->addEncoding_FaceRecognitionJob(workspace, ingestionJobKey,
                sourceMediaItemKey, sourcePhysicalPathKey, mmsAssetPathName,
				faceRecognitionCascadeName, faceRecognitionOutput, encodingPriority,
				initialFramesNumberToBeSkipped, oneFramePerSecond);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageFaceRecognitionMediaTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageFaceRecognitionMediaTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageFaceIdentificationMediaTask(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong medias number to be processed for Face Identification"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

		MMSEngineDBFacade::EncodingPriority encodingPriority;
		string field = "EncodingPriority";
		if (!JSONUtils::isMetadataPresent(parametersRoot, field))
		{
			encodingPriority = 
				static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
		}
		else
		{
			encodingPriority =
				MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
		}

		string faceIdentificationCascadeName;
        string deepLearnedModelTagsCommaSeparated;
        {
            string field = "CascadeName";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            faceIdentificationCascadeName = parametersRoot.get(field, "XXX").asString();

            field = "DeepLearnedModelTags";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            deepLearnedModelTagsCommaSeparated = parametersRoot.get(field, "XXX").asString();
        }
        
        // for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType: dependencies)
        {
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType	= dependencies[0];

			string mmsAssetPathName;
			/*
            int mmsPartitionNumber;
            string workspaceDirectoryName;
            string relativePath;
            string fileName;
            int64_t sizeInBytes;
            string deliveryFileName;
			*/
            MMSEngineDBFacade::ContentType contentType;
            string title;
            
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;
            
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(ignore, mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(
                        key, encodingProfileKey);

                int64_t physicalPathKey;
                shared_ptr<Workspace> workspace;
                
                tie(physicalPathKey, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/

                {
                    bool warningIfMissing = false;
                    tuple<MMSEngineDBFacade::ContentType,string,string,string, int64_t, int64_t>
						contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey =
                        _mmsEngineDBFacade->getMediaItemKeyDetails(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(contentType, ignore, ignore, ignore, ignore, ignore)
						= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
                }
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t, int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(key);

                shared_ptr<Workspace> workspace;
                
                tie(ignore, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
                
                {
                    bool warningIfMissing = false;
                    tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
						mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                        _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                            workspace->_workspaceKey, key, warningIfMissing);

                    tie(ignore, contentType, ignore, ignore, ignore, ignore, ignore)
                            = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
                }
            }
            
			/*
            _logger->info(__FILEREF__ + "getMMSAssetPathName ..."
                + ", mmsPartitionNumber: " + to_string(mmsPartitionNumber)
                + ", workspaceDirectoryName: " + workspaceDirectoryName
                + ", relativePath: " + relativePath
                + ", fileName: " + fileName
            );
            string mmsAssetPathName = _mmsStorage->getMMSAssetPathName(
                mmsPartitionNumber,
                workspaceDirectoryName,
                relativePath,
                fileName);
			*/

			_mmsEngineDBFacade->addEncoding_FaceIdentificationJob(workspace, ingestionJobKey,
                mmsAssetPathName, faceIdentificationCascadeName, deepLearnedModelTagsCommaSeparated,
				encodingPriority);
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageFaceIdendificationMediaTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageFaceIdendificationMediaTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageLiveRecorder(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot
)
{
    try
    {
		MMSEngineDBFacade::EncodingPriority encodingPriority;
		string field = "EncodingPriority";
		if (!JSONUtils::isMetadataPresent(parametersRoot, field))
		{
			encodingPriority = 
				static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
		}
		else
		{
			encodingPriority =
				MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
		}

		string configurationLabel;
		string userAgent;
        string recordingPeriodStart;
        string recordingPeriodEnd;
        bool autoRenew;
		int segmentDurationInSeconds;
		string outputFileFormat;
		bool highAvailability = false;
        {
            string field = "ConfigurationLabel";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            configurationLabel = parametersRoot.get(field, "XXX").asString();

            field = "UserAgent";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
				userAgent = parametersRoot.get(field, "").asString();

            field = "HighAvailability";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
				highAvailability = JSONUtils::asBool(parametersRoot, field, false);
            }

            field = "RecordingPeriod";
			Json::Value recordingPeriodRoot = parametersRoot[field];

            field = "Start";
            if (!JSONUtils::isMetadataPresent(recordingPeriodRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            recordingPeriodStart = recordingPeriodRoot.get(field, "XXX").asString();

            field = "End";
            if (!JSONUtils::isMetadataPresent(recordingPeriodRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            recordingPeriodEnd = recordingPeriodRoot.get(field, "XXX").asString();

            field = "AutoRenew";
            if (!JSONUtils::isMetadataPresent(recordingPeriodRoot, field))
				autoRenew = false;
			else
				autoRenew = JSONUtils::asBool(recordingPeriodRoot, field, false);

            field = "SegmentDuration";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            segmentDurationInSeconds = JSONUtils::asInt(parametersRoot, field, 0);

            field = "OutputFileFormat";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
				outputFileFormat = "ts";
			else
            	outputFileFormat = parametersRoot.get(field, "XXX").asString();
        }

		// next code is the same in the Validator class
		time_t utcRecordingPeriodStart;
		{
			unsigned long		ulUTCYear;
			unsigned long		ulUTCMonth;
			unsigned long		ulUTCDay;
			unsigned long		ulUTCHour;
			unsigned long		ulUTCMinutes;
			unsigned long		ulUTCSeconds;
			tm					tmRecordingPeriodStart;
			int					sscanfReturn;


			// _logger->error(__FILEREF__ + "recordingPeriodStart 1: " + recordingPeriodStart);
			// recordingPeriodStart.replace(10, 1, string(" "), 0, 1);
			// _logger->error(__FILEREF__ + "recordingPeriodStart 2: " + recordingPeriodStart);
			if ((sscanfReturn = sscanf (recordingPeriodStart.c_str(),
				"%4lu-%2lu-%2luT%2lu:%2lu:%2luZ",
				&ulUTCYear,
				&ulUTCMonth,
				&ulUTCDay,
				&ulUTCHour,
				&ulUTCMinutes,
				&ulUTCSeconds)) != 6)
			{
				string field = "Start";

				string errorMessage = __FILEREF__ + "Field has a wrong format (sscanf failed)"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", Field: " + field
					+ ", sscanfReturn: " + to_string(sscanfReturn)
					;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			time (&utcRecordingPeriodStart);
			gmtime_r(&utcRecordingPeriodStart, &tmRecordingPeriodStart);

			tmRecordingPeriodStart.tm_year		= ulUTCYear - 1900;
			tmRecordingPeriodStart.tm_mon		= ulUTCMonth - 1;
			tmRecordingPeriodStart.tm_mday		= ulUTCDay;
			tmRecordingPeriodStart.tm_hour		= ulUTCHour;
			tmRecordingPeriodStart.tm_min		= ulUTCMinutes;
			tmRecordingPeriodStart.tm_sec		= ulUTCSeconds;

			utcRecordingPeriodStart = timegm(&tmRecordingPeriodStart);
		}
		// _logger->error(__FILEREF__ + "ctime recordingPeriodStart: " + ctime(utcRecordingPeriodStart));

		// next code is the same in the Validator class
		time_t utcRecordingPeriodEnd;
		{
			unsigned long		ulUTCYear;
			unsigned long		ulUTCMonth;
			unsigned long		ulUTCDay;
			unsigned long		ulUTCHour;
			unsigned long		ulUTCMinutes;
			unsigned long		ulUTCSeconds;
			tm					tmRecordingPeriodEnd;
			int					sscanfReturn;


			if ((sscanfReturn = sscanf (recordingPeriodEnd.c_str(),
				"%4lu-%2lu-%2luT%2lu:%2lu:%2luZ",
				&ulUTCYear,
				&ulUTCMonth,
				&ulUTCDay,
				&ulUTCHour,
				&ulUTCMinutes,
				&ulUTCSeconds)) != 6)
			{
				string field = "Start";

				string errorMessage = __FILEREF__ + "Field has a wrong format (sscanf failed)"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", Field: " + field
					+ ", sscanfReturn: " + to_string(sscanfReturn)
					;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			time (&utcRecordingPeriodEnd);
			gmtime_r(&utcRecordingPeriodEnd, &tmRecordingPeriodEnd);

			tmRecordingPeriodEnd.tm_year		= ulUTCYear - 1900;
			tmRecordingPeriodEnd.tm_mon			= ulUTCMonth - 1;
			tmRecordingPeriodEnd.tm_mday		= ulUTCDay;
			tmRecordingPeriodEnd.tm_hour		= ulUTCHour;
			tmRecordingPeriodEnd.tm_min			= ulUTCMinutes;
			tmRecordingPeriodEnd.tm_sec			= ulUTCSeconds;

			utcRecordingPeriodEnd = timegm(&tmRecordingPeriodEnd);
		}

        pair<int64_t, string> confKeyAndLiveURL = _mmsEngineDBFacade->getLiveURLConfDetails(
                workspace->_workspaceKey, configurationLabel);            
		int64_t confKey;
		string liveURL;
		tie(confKey, liveURL) = confKeyAndLiveURL;

		_mmsEngineDBFacade->addEncoding_LiveRecorderJob(workspace, ingestionJobKey,
			highAvailability, configurationLabel, confKey, liveURL, userAgent,
			utcRecordingPeriodStart, utcRecordingPeriodEnd,
			autoRenew, segmentDurationInSeconds, outputFileFormat, encodingPriority);

		/*
		if (highAvailability)
		{
			main = false;

			_mmsEngineDBFacade->addEncoding_LiveRecorderJob(workspace, ingestionJobKey,
				highAvailability, main, configurationLabel, liveURL, utcRecordingPeriodStart, utcRecordingPeriodEnd,
				autoRenew, segmentDurationInSeconds, outputFileFormat, encodingPriority);
		}
		*/
	}
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageLiveRecorder failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageLiveRecorder failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageLiveProxy(
        int64_t ingestionJobKey,
        MMSEngineDBFacade::IngestionStatus ingestionStatus,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot
)
{
    try
    {
		MMSEngineDBFacade::EncodingPriority encodingPriority;
		string field = "EncodingPriority";
		if (!JSONUtils::isMetadataPresent(parametersRoot, field))
		{
			encodingPriority = 
				static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
		}
		else
		{
			encodingPriority =
				MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
		}

		string configurationLabel;
		string outputType;
		// string userAgent;
		int segmentDurationInSeconds = 0;
		int playlistEntriesNumber = 0;
		long waitingSecondsBetweenAttemptsInCaseOfErrors;
		long maxAttemptsNumberInCaseOfErrors;
		string cdnURL;
        {
            string field = "ConfigurationLabel";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            configurationLabel = parametersRoot.get(field, "XXX").asString();

            field = "OutputType";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
				outputType = "HLS";
			else
            	outputType = parametersRoot.get(field, "XXX").asString();

			if (outputType == "HLS" || outputType == "DASH")
			{
				field = "SegmentDurationInSeconds";
				if (!JSONUtils::isMetadataPresent(parametersRoot, field))
					segmentDurationInSeconds = 10;
				else
					segmentDurationInSeconds = JSONUtils::asInt(parametersRoot, field, 0);

				field = "PlaylistEntriesNumber";
				if (!JSONUtils::isMetadataPresent(parametersRoot, field))
					playlistEntriesNumber = 6;
				else
					playlistEntriesNumber = JSONUtils::asInt(parametersRoot, field, 0);
			}
			else if (outputType == "CDN77")
			{
				field = "CDN_URL";
				if (!JSONUtils::isMetadataPresent(parametersRoot, field))
				{
					string errorMessage = __FILEREF__ + "Field is not present or it is null"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}

				cdnURL = parametersRoot.get(field, "XXX").asString();
			}

			field = "MaxAttemptsNumberInCaseOfErrors";
			if (!JSONUtils::isMetadataPresent(parametersRoot, field))
				maxAttemptsNumberInCaseOfErrors = 3;
			else
				maxAttemptsNumberInCaseOfErrors = JSONUtils::asInt(parametersRoot, field, 0);

			field = "WaitingSecondsBetweenAttemptsInCaseOfErrors";
			if (!JSONUtils::isMetadataPresent(parametersRoot, field))
				waitingSecondsBetweenAttemptsInCaseOfErrors = 5;
			else
				waitingSecondsBetweenAttemptsInCaseOfErrors = JSONUtils::asInt64(parametersRoot, field, 0);
        }

        pair<int64_t, string> confKeyAndLiveURL = _mmsEngineDBFacade->getLiveURLConfDetails(
			workspace->_workspaceKey, configurationLabel);            

		int64_t liveURLConfKey;
        string liveURL;
		tie(liveURLConfKey, liveURL) = confKeyAndLiveURL;

		_mmsEngineDBFacade->addEncoding_LiveProxyJob(workspace, ingestionJobKey,
			liveURLConfKey, configurationLabel, liveURL, outputType,
			segmentDurationInSeconds, playlistEntriesNumber,
			cdnURL,
			maxAttemptsNumberInCaseOfErrors, waitingSecondsBetweenAttemptsInCaseOfErrors, encodingPriority);
	}
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageLiveProxy failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageLiveProxy failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::liveCutThread(
	shared_ptr<long> processorsThreadsNumber,
	int64_t ingestionJobKey,
	shared_ptr<Workspace> workspace,
	Json::Value liveCutParametersRoot
)
{
    try
    {
		string configurationLabel;
        string cutPeriodStartTimeInMilliSeconds;
        string cutPeriodEndTimeInMilliSeconds;
		int maxWaitingForLastChunkInSeconds = 90;
        {
            string field = "ConfigurationLabel";
            if (!JSONUtils::isMetadataPresent(liveCutParametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            configurationLabel = liveCutParametersRoot.get(field, "XXX").asString();

			field = "MaxWaitingForLastChunkInSeconds";
			if (JSONUtils::isMetadataPresent(liveCutParametersRoot, field))
				maxWaitingForLastChunkInSeconds = JSONUtils::asInt64(liveCutParametersRoot, field, 90);

            field = "CutPeriod";
			Json::Value cutPeriodRoot = liveCutParametersRoot[field];

            field = "Start";
            if (!JSONUtils::isMetadataPresent(cutPeriodRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            cutPeriodStartTimeInMilliSeconds = cutPeriodRoot.get(field, "").asString();

            field = "End";
            if (!JSONUtils::isMetadataPresent(cutPeriodRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            cutPeriodEndTimeInMilliSeconds = cutPeriodRoot.get(field, "").asString();
        }

		// next code is the same in the Validator class
		int64_t utcCutPeriodStartTimeInMilliSeconds;
		{
			unsigned long		ulUTCYear;
			unsigned long		ulUTCMonth;
			unsigned long		ulUTCDay;
			unsigned long		ulUTCHour;
			unsigned long		ulUTCMinutes;
			unsigned long		ulUTCSeconds;
			unsigned long		ulUTCMilliSeconds;
			tm					tmCutPeriodStart;
			int					sscanfReturn;


			// _logger->error(__FILEREF__ + "recordingPeriodStart 1: " + recordingPeriodStart);
			// recordingPeriodStart.replace(10, 1, string(" "), 0, 1);
			// _logger->error(__FILEREF__ + "recordingPeriodStart 2: " + recordingPeriodStart);
			if ((sscanfReturn = sscanf (cutPeriodStartTimeInMilliSeconds.c_str(),
				"%4lu-%2lu-%2luT%2lu:%2lu:%2lu.%3luZ",
				&ulUTCYear,
				&ulUTCMonth,
				&ulUTCDay,
				&ulUTCHour,
				&ulUTCMinutes,
				&ulUTCSeconds,
				&ulUTCMilliSeconds)) != 7)
			{
				string field = "Start";

				string errorMessage = __FILEREF__ + "Field has a wrong format (sscanf failed)"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", Field: " + field
					+ ", cutPeriodStartTimeInMilliSeconds: " + cutPeriodStartTimeInMilliSeconds
					+ ", sscanfReturn: " + to_string(sscanfReturn)
					;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			time_t utcCutPeriodStartTime;
			time (&utcCutPeriodStartTime);
			gmtime_r(&utcCutPeriodStartTime, &tmCutPeriodStart);

			tmCutPeriodStart.tm_year		= ulUTCYear - 1900;
			tmCutPeriodStart.tm_mon		= ulUTCMonth - 1;
			tmCutPeriodStart.tm_mday		= ulUTCDay;
			tmCutPeriodStart.tm_hour		= ulUTCHour;
			tmCutPeriodStart.tm_min		= ulUTCMinutes;
			tmCutPeriodStart.tm_sec		= ulUTCSeconds;

			utcCutPeriodStartTimeInMilliSeconds = timegm(&tmCutPeriodStart) * 1000;
			utcCutPeriodStartTimeInMilliSeconds += ulUTCMilliSeconds;
		}

		// next code is the same in the Validator class
		int64_t utcCutPeriodEndTimeInMilliSeconds;
		{
			unsigned long		ulUTCYear;
			unsigned long		ulUTCMonth;
			unsigned long		ulUTCDay;
			unsigned long		ulUTCHour;
			unsigned long		ulUTCMinutes;
			unsigned long		ulUTCSeconds;
			unsigned long		ulUTCMilliSeconds;
			tm					tmCutPeriodEnd;
			int					sscanfReturn;


			if ((sscanfReturn = sscanf (cutPeriodEndTimeInMilliSeconds.c_str(),
				"%4lu-%2lu-%2luT%2lu:%2lu:%2lu.%3luZ",
				&ulUTCYear,
				&ulUTCMonth,
				&ulUTCDay,
				&ulUTCHour,
				&ulUTCMinutes,
				&ulUTCSeconds,
				&ulUTCMilliSeconds)) != 7)
			{
				string field = "End";

				string errorMessage = __FILEREF__ + "Field has a wrong format (sscanf failed)"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", Field: " + field
					+ ", cutPeriodEndTimeInMilliSeconds: " + cutPeriodEndTimeInMilliSeconds
					+ ", sscanfReturn: " + to_string(sscanfReturn)
					;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			time_t utcCutPeriodEndTime;
			time (&utcCutPeriodEndTime);
			gmtime_r(&utcCutPeriodEndTime, &tmCutPeriodEnd);

			tmCutPeriodEnd.tm_year		= ulUTCYear - 1900;
			tmCutPeriodEnd.tm_mon			= ulUTCMonth - 1;
			tmCutPeriodEnd.tm_mday		= ulUTCDay;
			tmCutPeriodEnd.tm_hour		= ulUTCHour;
			tmCutPeriodEnd.tm_min			= ulUTCMinutes;
			tmCutPeriodEnd.tm_sec			= ulUTCSeconds;

			utcCutPeriodEndTimeInMilliSeconds = timegm(&tmCutPeriodEnd) * 1000;
			utcCutPeriodEndTimeInMilliSeconds += ulUTCMilliSeconds;
		}

		/*
		 * 2020-03-30: scenario: period end time is 300 seconds (5 minutes). In case the chunk is 1 minute,
		 * we will take 5 chunks.
		 * The result is that the Cut will fail because:
		 * - we need to cut to 300 seconds
		 * - the duration of the video is 298874 milliseconds
		 * For this reason, when we retrieve the chunks, we will use 'period end time' plus one second
		 */
		int64_t utcCutPeriodEndTimeInMilliSecondsPlusOneSecond = utcCutPeriodEndTimeInMilliSeconds + 1000;

        pair<int64_t, string> confKeyAndLiveURL = _mmsEngineDBFacade->getLiveURLConfDetails(
                workspace->_workspaceKey, configurationLabel);
		int64_t confKey;
		string liveURL;
		tie(confKey, liveURL) = confKeyAndLiveURL;

		Json::Value mediaItemKeyReferencesRoot(Json::arrayValue);
		int64_t utcFirstChunkStartTime;
		string firstChunkStartTime;
		int64_t utcLastChunkEndTime;
		string lastChunkEndTime;

		chrono::system_clock::time_point startLookingForChunks = chrono::system_clock::now();

		bool allChunksAvailable = false;
		while (!allChunksAvailable
			&& (chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - startLookingForChunks).count() < maxWaitingForLastChunkInSeconds)
		)
		{
			int64_t mediaItemKey = -1;
			int64_t physicalPathKey = -1;
			string uniqueName;
			vector<int64_t> otherMediaItemsKey;
			int start = 0;
			int rows = 60 * 1;	// assuming every MediaItem is one minute, let's take 1 hour
			bool contentTypePresent = true;
			MMSEngineDBFacade::ContentType contentType = MMSEngineDBFacade::ContentType::Video;
			bool startAndEndIngestionDatePresent = false;
			string startIngestionDate;
			string endIngestionDate;
			string title;
			int liveRecordingChunk = 1;
			vector<string> tagsIn;
			vector<string> tagsNotIn;
			string ingestionDateOrder = "desc";
			bool admin = false;

			string jsonCondition;
			{
				// SC: Start Chunk
				// PS: Playout Start, PE: Playout End
				// --------------SC--------------SC--------------SC--------------SC
				//                       PS-------------------------------PE

				jsonCondition = "( JSON_EXTRACT(userData, '$.mmsData.validated') = true and ";
				jsonCondition += "JSON_EXTRACT(userData, '$.mmsData.liveURLConfKey') = " + to_string(confKey) + " and (";

				// first chunk of the cut
				jsonCondition += (
					"(JSON_EXTRACT(userData, '$.mmsData.utcChunkStartTime') * 1000 <= "
						+ to_string(utcCutPeriodStartTimeInMilliSeconds) + " "
					+ "and " + to_string(utcCutPeriodStartTimeInMilliSeconds) + " < JSON_EXTRACT(userData, '$.mmsData.utcChunkEndTime') * 1000 ) "
				);

				jsonCondition += " or ";

				// internal chunk of the cut
				jsonCondition += (
					"( " + to_string(utcCutPeriodStartTimeInMilliSeconds) + " <= JSON_EXTRACT(userData, '$.mmsData.utcChunkStartTime') * 1000 "
					+ "and JSON_EXTRACT(userData, '$.mmsData.utcChunkEndTime') * 1000 <= " + to_string(utcCutPeriodEndTimeInMilliSecondsPlusOneSecond) + ") "
				);

				jsonCondition += " or ";

				// last chunk of the cut
				jsonCondition += (
				"( JSON_EXTRACT(userData, '$.mmsData.utcChunkStartTime') * 1000 < " + to_string(utcCutPeriodEndTimeInMilliSecondsPlusOneSecond) + " "
					+ "and " + to_string(utcCutPeriodEndTimeInMilliSecondsPlusOneSecond) + " <= JSON_EXTRACT(userData, '$.mmsData.utcChunkEndTime') * 1000 ) "
				);

				jsonCondition += ") )";
			}
			string jsonOrderBy = "JSON_EXTRACT(userData, '$.mmsData.utcChunkStartTime')";

			long utcPreviousUtcChunkEndTime = -1;
			bool firstChunk = true;
			bool lastChunk = false;

			// retrieve the reference of all the MediaItems to be concatenate
			mediaItemKeyReferencesRoot.clear();

			Json::Value mediaItemsListRoot;
			do
			{
				mediaItemsListRoot = _mmsEngineDBFacade->getMediaItemsList(
					workspace->_workspaceKey, mediaItemKey, uniqueName, physicalPathKey, otherMediaItemsKey,
					start, rows,
					contentTypePresent, contentType,
					startAndEndIngestionDatePresent, startIngestionDate, endIngestionDate,
					title, liveRecordingChunk, jsonCondition, tagsIn, tagsNotIn,
					ingestionDateOrder, jsonOrderBy, admin);

				string field = "response";
				Json::Value responseRoot = mediaItemsListRoot[field];

				field = "mediaItems";
				Json::Value mediaItemsRoot = responseRoot[field];

				for (int mediaItemIndex = 0; mediaItemIndex < mediaItemsRoot.size(); mediaItemIndex++)
				{
					Json::Value mediaItemRoot = mediaItemsRoot[mediaItemIndex];

					field = "mediaItemKey";
					int64_t mediaItemKey = JSONUtils::asInt64(mediaItemRoot, field, 0);

					Json::Value userDataRoot;
					{
						field = "userData";
						string userData = mediaItemRoot.get(field, "").asString();
						if (userData == "")
						{
							string errorMessage = __FILEREF__ + "recording media item without userData!!!"
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", mediaItemKey: " + to_string(mediaItemKey)
							;
							_logger->error(errorMessage);

							throw runtime_error(errorMessage);
						}

						try
						{
							Json::CharReaderBuilder builder;                                  
							Json::CharReader* reader = builder.newCharReader();               
							string errors;                                                    

							bool parsingSuccessful = reader->parse(                           
								userData.c_str(),                             
								userData.c_str() + userData.size(),
								&userDataRoot, &errors);                      
							delete reader;

							if (!parsingSuccessful)                                           
							{
								string errorMessage = __FILEREF__ + "failed to parse the userData"
									+ ", _processorIdentifier: " + to_string(_processorIdentifier)
									+ ", ingestionJobKey: " + to_string(ingestionJobKey)
									+ ", mediaItemKey: " + to_string(mediaItemKey)
									+ ", errors: " + errors
									+ ", userData: " + userData
								;
								_logger->error(errorMessage);

								throw runtime_error(errors);
							}
						}
						catch(runtime_error e)
						{
							string errorMessage = string("userData json is not well format")
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", mediaItemKey: " + to_string(mediaItemKey)
								+ ", userData: " + userData
								+ ", e.what(): " + e.what()
							;
							_logger->error(__FILEREF__ + errorMessage);

							throw runtime_error(errorMessage);
						}
						catch(exception e)
						{
							string errorMessage = string("userData json is not well format")
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", mediaItemKey: " + to_string(mediaItemKey)
								+ ", userData: " + userData
							;
							_logger->error(__FILEREF__ + errorMessage);

							throw runtime_error(errorMessage);
						}
					}

					field = "mmsData";
					Json::Value mmsDataRoot = userDataRoot[field];

					field = "utcChunkStartTime";
					int64_t utcChunkStartTime = JSONUtils::asInt64(mmsDataRoot, field, 0);

					field = "utcChunkEndTime";
					int64_t utcChunkEndTime = JSONUtils::asInt64(mmsDataRoot, field, 0);

					string chunkStartTime;
					string chunkEndTime;
					{
						char strDateTime [64];
						tm tmDateTime;

						localtime_r (&utcChunkStartTime, &tmDateTime);
						sprintf (strDateTime, "%04d-%02d-%02d %02d:%02d:%02d",
							tmDateTime. tm_year + 1900,
							tmDateTime. tm_mon + 1,
							tmDateTime. tm_mday,
							tmDateTime. tm_hour,
							tmDateTime. tm_min,
							tmDateTime. tm_sec);
						chunkStartTime = strDateTime;

						localtime_r (&utcChunkEndTime, &tmDateTime);
						sprintf (strDateTime, "%04d-%02d-%02d %02d:%02d:%02d",
							tmDateTime. tm_year + 1900,
							tmDateTime. tm_mon + 1,
							tmDateTime. tm_mday,
							tmDateTime. tm_hour,
							tmDateTime. tm_min,
							tmDateTime. tm_sec);
						chunkEndTime = strDateTime;
					}

					_logger->error(__FILEREF__ + "Retrieved chunk"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaITemKey: " + to_string(mediaItemKey)
						+ ", utcChunkStartTime: " + to_string(utcChunkStartTime) + " (" + chunkStartTime + ")"
						+ ", utcChunkEndTime: " + to_string(utcChunkEndTime) + " (" + chunkEndTime + ")"
					);

					// check if it is the next chunk
					if (utcPreviousUtcChunkEndTime != -1 && utcPreviousUtcChunkEndTime != utcChunkStartTime)
					{
						string previousUtcChunkEndTime;
						{
							char strDateTime [64];
							tm tmDateTime;

							localtime_r (&utcPreviousUtcChunkEndTime, &tmDateTime);

							sprintf (strDateTime, "%04d-%02d-%02d %02d:%02d:%02d",
								tmDateTime. tm_year + 1900,
								tmDateTime. tm_mon + 1,
								tmDateTime. tm_mday,
								tmDateTime. tm_hour,
								tmDateTime. tm_min,
								tmDateTime. tm_sec);
							previousUtcChunkEndTime = strDateTime;
						}

						// it is not the next chunk
						string errorMessage = string("Next chunk was not found")
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", utcPreviousUtcChunkEndTime: " + to_string(utcPreviousUtcChunkEndTime) + " (" + previousUtcChunkEndTime + ")"
							+ ", utcChunkStartTime: " + to_string(utcChunkStartTime) + " (" + chunkStartTime + ")"
							+ ", utcChunkEndTime: " + to_string(utcChunkEndTime) + " (" + chunkEndTime + ")"
							+ ", utcCutPeriodStartTimeInMilliSeconds: " + to_string(utcCutPeriodStartTimeInMilliSeconds)
								+ " (" + cutPeriodStartTimeInMilliSeconds + ")"
							+ ", utcCutPeriodEndTimeInMilliSeconds: " + to_string(utcCutPeriodEndTimeInMilliSeconds)
								+ " (" + cutPeriodEndTimeInMilliSeconds + ")"
						;
						_logger->error(__FILEREF__ + errorMessage);

						throw runtime_error(errorMessage);
					}

					// check if it is the first chunk
					if (firstChunk)
					{
						firstChunk = false;

						// check that it is the first chunk

						if (!(utcChunkStartTime * 1000 <= utcCutPeriodStartTimeInMilliSeconds
							&& utcCutPeriodStartTimeInMilliSeconds < utcChunkEndTime * 1000))
						{
							// it is not the first chunk
							string errorMessage = string("First chunk was not found")
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", first utcChunkStart: " + to_string(utcChunkStartTime) + " (" + chunkStartTime + ")"
								+ ", first utcChunkEndTime: " + to_string(utcChunkEndTime) + " (" + chunkEndTime + ")"
								+ ", utcCutPeriodStartTimeInMilliSeconds: " + to_string(utcCutPeriodStartTimeInMilliSeconds)
									+ " (" + cutPeriodStartTimeInMilliSeconds + ")"
								+ ", utcCutPeriodEndTimeInMilliSeconds: " + to_string(utcCutPeriodEndTimeInMilliSeconds)
									+ " (" + cutPeriodEndTimeInMilliSeconds + ")"
							;
							_logger->error(__FILEREF__ + errorMessage);

							throw runtime_error(errorMessage);
						}

						utcFirstChunkStartTime = utcChunkStartTime;
						firstChunkStartTime = chunkStartTime;
					}

					{
						Json::Value mediaItemKeyReferenceRoot;

						field = "ReferenceMediaItemKey";
						mediaItemKeyReferenceRoot[field] = mediaItemKey;

						mediaItemKeyReferencesRoot.append(mediaItemKeyReferenceRoot);
					}

					{
						// check if it is the last chunk

						if (!(utcChunkStartTime * 1000 < utcCutPeriodEndTimeInMilliSecondsPlusOneSecond
								&& utcCutPeriodEndTimeInMilliSecondsPlusOneSecond <= utcChunkEndTime * 1000))
							lastChunk = false;
						else
						{
							lastChunk = true;
							utcLastChunkEndTime = utcChunkEndTime;
							lastChunkEndTime = chunkEndTime;
						}
					}

					utcPreviousUtcChunkEndTime = utcChunkEndTime;
				}

				start += rows;
			}
			while(mediaItemsListRoot.size() == rows);

			// just waiting if the last chunk was not finished yet
			if (!lastChunk)
			{
				if (chrono::duration_cast<chrono::seconds>(
					chrono::system_clock::now() - startLookingForChunks).count() < maxWaitingForLastChunkInSeconds)
				{
					int secondsToWaitLastChunk = 30;

					this_thread::sleep_for(chrono::seconds(secondsToWaitLastChunk));
				}
			}
			else
			{
				allChunksAvailable = true;
			}
		}

		if (!allChunksAvailable)
		{
			string errorMessage = string("Chunks not available")
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", configurationLabel: " + configurationLabel
				+ ", cutPeriodStartTimeInMilliSeconds: " + cutPeriodStartTimeInMilliSeconds
				+ ", cutPeriodEndTimeInMilliSeconds: " + cutPeriodEndTimeInMilliSeconds
				+ ", maxWaitingForLastChunkInSeconds: " + to_string(maxWaitingForLastChunkInSeconds)
			;
			_logger->error(__FILEREF__ + errorMessage);

			throw runtime_error(errorMessage);
		}

		Json::Value liveCutOnSuccess = Json::nullValue;
		Json::Value liveCutOnError = Json::nullValue;
		Json::Value liveCutOnComplete = Json::nullValue;
		int64_t userKey;
		string apiKey;
		{
			string field = "InternalMMS";
			if (JSONUtils::isMetadataPresent(liveCutParametersRoot, field))
			{
				Json::Value internalMMSRoot = liveCutParametersRoot[field];

				field = "userKey";
				userKey = JSONUtils::asInt64(internalMMSRoot, field, -1);

				field = "apiKey";
				apiKey = internalMMSRoot.get(field, "").asString();

				field = "OnSuccess";
				if (JSONUtils::isMetadataPresent(internalMMSRoot, field))
					liveCutOnSuccess = internalMMSRoot[field];

				field = "OnError";
				if (JSONUtils::isMetadataPresent(internalMMSRoot, field))
					liveCutOnError = internalMMSRoot[field];

				field = "OnComplete";
				if (JSONUtils::isMetadataPresent(internalMMSRoot, field))
					liveCutOnComplete = internalMMSRoot[field];
			}
		}

		// create workflow to ingest
		string workflowMetadata;
		{
			Json::Value concatDemuxerRoot;
			Json::Value concatDemuxerParametersRoot;
			{
				string field = "Label";
				concatDemuxerRoot[field] = "Concat from " + to_string(utcFirstChunkStartTime) + " (" + firstChunkStartTime
					+ ") to " + to_string(utcLastChunkEndTime) + " (" + lastChunkEndTime + ")";

				field = "Type";
				concatDemuxerRoot[field] = "Concat-Demuxer";

				concatDemuxerParametersRoot = liveCutParametersRoot;
				{
					Json::Value removed;
					field = "ConfigurationLabel";
					concatDemuxerParametersRoot.removeMember(field, &removed);
				}
				{
					Json::Value removed;
					field = "CutPeriod";
					concatDemuxerParametersRoot.removeMember(field, &removed);
				}
				{
					field = "MaxWaitingForLastChunkInSeconds";
					if (JSONUtils::isMetadataPresent(concatDemuxerParametersRoot, field))
					{
						Json::Value removed;
						concatDemuxerParametersRoot.removeMember(field, &removed);
					}
				}

				field = "Retention";
				concatDemuxerParametersRoot[field] = "0";

				field = "References";
				concatDemuxerParametersRoot[field] = mediaItemKeyReferencesRoot;

				field = "Parameters";
				concatDemuxerRoot[field] = concatDemuxerParametersRoot;
			}

			Json::Value cutRoot;
			{
				string field = "Label";
				cutRoot[field] = string("Live Cut from ") + to_string(utcCutPeriodStartTimeInMilliSeconds)
					+ " (" + cutPeriodStartTimeInMilliSeconds + ") to "
					+ to_string(utcCutPeriodEndTimeInMilliSeconds) + " (" + cutPeriodEndTimeInMilliSeconds + ")";

				field = "Type";
				cutRoot[field] = "Cut";

				Json::Value cutParametersRoot = concatDemuxerParametersRoot;
				{
					Json::Value removed;
					field = "References";
					cutParametersRoot.removeMember(field, &removed);
				}

				field = "Retention";
				cutParametersRoot[field] = liveCutParametersRoot.get(field, "").asString();

				double startTimeInSeconds =
					(utcCutPeriodStartTimeInMilliSeconds - (utcFirstChunkStartTime * 1000)) / 1000;
				field = "StartTimeInSeconds";
				cutParametersRoot[field] = startTimeInSeconds;

				double endTimeInSeconds =
					(utcCutPeriodEndTimeInMilliSeconds - (utcFirstChunkStartTime * 1000)) / 1000;
				field = "EndTimeInSeconds";
				cutParametersRoot[field] = endTimeInSeconds;

				field = "Parameters";
				cutRoot[field] = cutParametersRoot;

				if (liveCutOnSuccess != Json::nullValue)
				{
					field = "OnSuccess";
					cutRoot[field] = liveCutOnSuccess;
				}
				if (liveCutOnError != Json::nullValue)
				{
					field = "OnError";
					cutRoot[field] = liveCutOnError;
				}
				if (liveCutOnComplete != Json::nullValue)
				{
					field = "OnComplete";
					cutRoot[field] = liveCutOnComplete;
				}
			}

			Json::Value concatOnSuccessRoot;
			{
				Json::Value cutTaskRoot;
				string field = "Task";
				cutTaskRoot[field] = cutRoot;

				field = "OnSuccess";
				concatDemuxerRoot[field] = cutTaskRoot;
			}

			Json::Value workflowRoot;
			{
				string field = "Label";
				workflowRoot[field] = string("Cut from ") + to_string(utcCutPeriodStartTimeInMilliSeconds)
					+ " (" + cutPeriodStartTimeInMilliSeconds + ") to "
					+ to_string(utcCutPeriodEndTimeInMilliSeconds) + " (" + cutPeriodEndTimeInMilliSeconds + ")";

				field = "Type";
				workflowRoot[field] = "Workflow";

				field = "Task";
				workflowRoot[field] = concatDemuxerRoot;
			}

			{
				Json::StreamWriterBuilder wbuilder;
				workflowMetadata = Json::writeString(wbuilder, workflowRoot);
			}
		}

		{
			string mmsAPIURL =
				_mmsAPIProtocol
				+ "://"
				+ _mmsAPIHostname + ":"
				+ to_string(_mmsAPIPort)
				+ _mmsAPIIngestionURI
            ;

			list<string> header;

			header.push_back("Content-Type: application/json");
			{
				// string userPasswordEncoded = Convert::base64_encode(_mmsAPIUser + ":" + _mmsAPIPassword);
				string userPasswordEncoded = Convert::base64_encode(to_string(userKey) + ":" + apiKey);
				string basicAuthorization = string("Authorization: Basic ") + userPasswordEncoded;

				header.push_back(basicAuthorization);
			}

			curlpp::Cleanup cleaner;
			curlpp::Easy request;

			// Setting the URL to retrive.
			request.setOpt(new curlpp::options::Url(mmsAPIURL));

			if (_mmsAPIProtocol == "https")
			{
				/*
				typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
				typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
				typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
				typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
				typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
				typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
				typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
				typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
				typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
				typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
				typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
				typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
				typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    
				*/

				/*
				// cert is stored PEM coded in file... 
				// since PEM is default, we needn't set it for PEM 
				// curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
				curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
				equest.setOpt(sslCertType);

				// set the cert for client authentication
				// "testcert.pem"
				// curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
				curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
				request.setOpt(sslCert);
				*/

				/*
				// sorry, for engine we must set the passphrase
				//   (if the key has one...)
				// const char *pPassphrase = NULL;
				if(pPassphrase)
				curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

				// if we use a key stored in a crypto engine,
				//   we must set the key type to "ENG"
				// pKeyType  = "PEM";
				curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

				// set the private key (file or ID in engine)
				// pKeyName  = "testkey.pem";
				curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

				// set the file with the certs vaildating the server
				// *pCACertFile = "cacert.pem";
				curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);
				*/

				// disconnect if we can't validate server's cert
				bool bSslVerifyPeer = false;
				curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
				request.setOpt(sslVerifyPeer);
               
				curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
				request.setOpt(sslVerifyHost);
               
				// request.setOpt(new curlpp::options::SslEngineDefault());                                              
			}

			request.setOpt(new curlpp::options::HttpHeader(header));
			request.setOpt(new curlpp::options::PostFields(workflowMetadata));
			request.setOpt(new curlpp::options::PostFieldSize(workflowMetadata.length()));

			ostringstream response;

			request.setOpt(new curlpp::options::WriteStream(&response));

			_logger->info(__FILEREF__ + "Ingesting CutLive workflow"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
				+ ", mmsAPIURL: " + mmsAPIURL
				+ ", workflowMetadata: " + workflowMetadata
			);
			chrono::system_clock::time_point startIngesting = chrono::system_clock::now();
			request.perform();
			chrono::system_clock::time_point endIngesting = chrono::system_clock::now();

			string sResponse = response.str();
			// LF and CR create problems to the json parser...
			while (sResponse.back() == 10 || sResponse.back() == 13)
				sResponse.pop_back();

			long responseCode = curlpp::infos::ResponseCode::get(request);
			if (responseCode == 201)
			{
				string message = __FILEREF__ + "Ingested CutLive workflow response"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
					+ ", ingestingDuration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(endIngesting - startIngesting).count())
					+ ", workflowMetadata: " + workflowMetadata
					+ ", sResponse: " + sResponse
					;
				_logger->info(message);
			}
			else
			{
				string message = __FILEREF__ + "Ingested CutLive workflow response"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
					+ ", ingestingDuration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(endIngesting - startIngesting).count())
					+ ", workflowMetadata: " + workflowMetadata
					+ ", sResponse: " + sResponse
					+ ", responseCode: " + to_string(responseCode)
					;
				_logger->error(message);

				throw runtime_error(message);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_TaskSuccess"
            + ", errorMessage: " + ""
        );
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
        );
	}
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "liveCutThread failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
 
        _logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

		return;
        // throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "liveCutThread failed"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

		return;
        // throw e;
    }
}

void MMSEngineProcessor::changeFileFormatThread(
        shared_ptr<long> processorsThreadsNumber, int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies)

{
    try 
    {
        _logger->info(__FILEREF__ + "ChangeFileFormat"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
                
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured media to be used to changeFileFormat"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        string outputFileFormat;
        {
            string field = "OutputFileFormat";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            outputFileFormat = parametersRoot.get(field, "XXX").asString();
        }

        for(vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>::iterator
				it = dependencies.begin(); 
                it != dependencies.end(); ++it) 
        {
            tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType = *it;

            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;

            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

			int64_t mediaItemKey;
			string mmsSourceAssetPathName;
			string relativePath;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
				mediaItemKey = key;
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(ignore, mmsSourceAssetPathName, ignore, relativePath, ignore, ignore, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsSourceAssetPathName, ignore, relativePath, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;

				bool warningIfMissing = false;
				tuple<int64_t, MMSEngineDBFacade::ContentType, string, string, string, int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
					_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
						workspace->_workspaceKey, key, warningIfMissing);
				tie(mediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore)
					= mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
            }

			/*
			 * 2019-10-11: next code is to create a new MediaItem. I commented it because this is not a new asset,
			 * it is just another variant of the asset 
            {
                string localSourceFileName;
                string changeFileFormatMediaPathName;
                {
                    localSourceFileName = to_string(ingestionJobKey)
                            + "_" + to_string(key)
                            + "_changeFileFormat"
                            + "." + outputFileFormat
                            ;

                    string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
                        workspace);
                    changeFileFormatMediaPathName = workspaceIngestionRepository + "/" 
                            + localSourceFileName;
                }

                FFMpeg ffmpeg (_configuration, _logger);

                ffmpeg.changeFileFormat(
                    ingestionJobKey,
					key,
                    mmsAssetPathName,
                    changeFileFormatMediaPathName);

                _logger->info(__FILEREF__ + "ffmpeg.changeFileFormat done"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", changeFileFormatMediaPathName: " + changeFileFormatMediaPathName
                );

                string title;
				int64_t imageOfVideoMediaItemKey = -1;
				int64_t cutOfVideoMediaItemKey = -1;
				int64_t cutOfAudioMediaItemKey = -1;
				double startTimeInSeconds = 0.0;
				double endTimeInSeconds = 0.0;
                string mediaMetaDataContent = generateMediaMetadataToIngest(
                        ingestionJobKey,
                        outputFileFormat,
                        title,
						imageOfVideoMediaItemKey,
						cutOfVideoMediaItemKey, cutOfAudioMediaItemKey, startTimeInSeconds, endTimeInSeconds,
                        parametersRoot
                );

                {
                    shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                            ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

                    localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
                    localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
                    localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

					localAssetIngestionEvent->setExternalReadOnlyStorage(false);
                    localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
                    localAssetIngestionEvent->setIngestionSourceFileName(localSourceFileName);
                    localAssetIngestionEvent->setMMSSourceFileName(localSourceFileName);
                    localAssetIngestionEvent->setWorkspace(workspace);
                    localAssetIngestionEvent->setIngestionType(MMSEngineDBFacade::IngestionType::AddContent);            
                    localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(
                        it + 1 == dependencies.end() ? true : false);

                    // to manage a ffmpeg bug generating a corrupted/wrong avgFrameRate, we will
                    // force the concat file to have the same avgFrameRate of the source media
                    // Uncomment next statements in case the problem is still present event in case of the ExtractTracks task
                    // if (forcedAvgFrameRate != "" && concatContentType == MMSEngineDBFacade::ContentType::Video)
                    //    localAssetIngestionEvent->setForcedAvgFrameRate(forcedAvgFrameRate);            

                    localAssetIngestionEvent->setMetadataContent(mediaMetaDataContent);

                    shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
                    _multiEventsSet->addEvent(event);

                    _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", getEventKey().first: " + to_string(event->getEventKey().first)
                        + ", getEventKey().second: " + to_string(event->getEventKey().second));
                }
            }
			*/

			// add the new file as a new variant of the MIK
			{
				string changeFormatFileName = to_string(ingestionJobKey)
					+ "_" + to_string(mediaItemKey)
					+ "_changeFileFormat"
					+ "." + outputFileFormat
				;

				string stagingChangeFileFormatAssetPathName;
				{
					bool removeLinuxPathIfExist = true;
					bool neededForTranscoder = false;
					stagingChangeFileFormatAssetPathName = _mmsStorage->getStagingAssetPathName(
						neededForTranscoder,
						workspace->_directoryName,
						to_string(ingestionJobKey),
						"/",
						changeFormatFileName,
						-1, // _encodingItem->_mediaItemKey, not used because encodedFileName is not ""
						-1, // _encodingItem->_physicalPathKey, not used because encodedFileName is not ""
						removeLinuxPathIfExist);
				}

				try
				{
					_logger->info(__FILEREF__ + "Calling ffmpeg.changeFileFormat"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaItemKey: " + to_string(mediaItemKey)
						+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
						+ ", changeFormatFileName: " + changeFormatFileName
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
					);

					FFMpeg ffmpeg (_configuration, _logger);

					ffmpeg.changeFileFormat(
						ingestionJobKey,
						key,
						mmsSourceAssetPathName,
						stagingChangeFileFormatAssetPathName);

					_logger->info(__FILEREF__ + "ffmpeg.changeFileFormat done"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaItemKey: " + to_string(mediaItemKey)
						+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
						+ ", changeFormatFileName: " + changeFormatFileName
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
					);
				}
				catch(runtime_error e)
				{
					_logger->error(__FILEREF__ + "ffmpeg.changeFileFormat failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaItemKey: " + to_string(mediaItemKey)
						+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
						+ ", e.what(): " + e.what()
					);

					throw e;
				}
				catch(exception e)
				{
					_logger->error(__FILEREF__ + "ffmpeg.changeFileFormat failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaItemKey: " + to_string(mediaItemKey)
						+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
						+ ", e.what(): " + e.what()
					);

					throw e;
				}

				/*
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
				*/
				pair<int64_t, long> mediaInfoDetails;
				vector<tuple<int, int64_t, string, string, int, int, string, long>> videoTracks;
				vector<tuple<int, int64_t, string, long, int, long, string>> audioTracks;

				int imageWidth = -1;
				int imageHeight = -1;
				string imageFormat;
				int imageQuality = -1;
				try
				{
					_logger->info(__FILEREF__ + "Calling ffmpeg.getMediaInfo"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
					);
					FFMpeg ffmpeg (_configuration, _logger);
					// tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long> mediaInfo =
					mediaInfoDetails = ffmpeg.getMediaInfo(stagingChangeFileFormatAssetPathName,
							videoTracks, audioTracks);

					// tie(durationInMilliSeconds, bitRate, 
					// 	videoCodecName, videoProfile, videoWidth, videoHeight, videoAvgFrameRate, videoBitRate,
					// 	audioCodecName, audioSampleRate, audioChannels, audioBitRate) = mediaInfo;
				}
				catch(runtime_error e)
				{
					_logger->error(__FILEREF__ + "getMediaInfo failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
						+ ", _workspace->_directoryName: " + workspace->_directoryName
						+ ", e.what(): " + e.what()
					);

					{
						string directoryPathName;
						try
						{
							size_t endOfDirectoryIndex = stagingChangeFileFormatAssetPathName.find_last_of("/");
							if (endOfDirectoryIndex != string::npos)
							{
								directoryPathName = stagingChangeFileFormatAssetPathName.substr(0, endOfDirectoryIndex);

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
								+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
								+ ", directoryPathName: " + directoryPathName
								+ ", exception: " + e.what()
							);
						}
					}

					throw e;
				}
				catch(exception e)
				{
					_logger->error(__FILEREF__ + "getMediaInfo failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
						+ ", workspace->_directoryName: " + workspace->_directoryName
					);

					{
						string directoryPathName;
						try
						{
							size_t endOfDirectoryIndex = stagingChangeFileFormatAssetPathName.find_last_of("/");
							if (endOfDirectoryIndex != string::npos)
							{
								directoryPathName = stagingChangeFileFormatAssetPathName.substr(0, endOfDirectoryIndex);

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
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
								+ ", directoryPathName: " + directoryPathName
								+ ", exception: " + e.what()
							);
						}
					}

					throw e;
				}

				string mmsChangeFileFormatAssetPathName;
				unsigned long mmsPartitionIndexUsed;
				try
				{
					bool partitionIndexToBeCalculated = true;
					bool deliveryRepositoriesToo = true;

					mmsChangeFileFormatAssetPathName = _mmsStorage->moveAssetInMMSRepository(
						stagingChangeFileFormatAssetPathName,
						workspace->_directoryName,
						changeFormatFileName,
						relativePath,

						partitionIndexToBeCalculated,
						&mmsPartitionIndexUsed, // OUT if bIsPartitionIndexToBeCalculated is true, IN is bIsPartitionIndexToBeCalculated is false

						deliveryRepositoriesToo,
						workspace->_territories
					);
				}
				catch(runtime_error e)
				{
					_logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaItemKey: " + to_string(mediaItemKey)
						+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
						+ ", e.what(): " + e.what()
					);

					{
						string directoryPathName;
						try
						{
							size_t endOfDirectoryIndex = stagingChangeFileFormatAssetPathName.find_last_of("/");
							if (endOfDirectoryIndex != string::npos)
							{
								directoryPathName = stagingChangeFileFormatAssetPathName.substr(0, endOfDirectoryIndex);

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
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
								+ ", directoryPathName: " + directoryPathName
								+ ", exception: " + e.what()
							);
						}
					}

					throw e;
				}
				catch(exception e)
				{
					_logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", mediaItemKey: " + to_string(mediaItemKey)
						+ ", mmsSourceAssetPathName: " + mmsSourceAssetPathName
						+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
						+ ", e.what(): " + e.what()
					);

					{
						string directoryPathName;
						try
						{
							size_t endOfDirectoryIndex = stagingChangeFileFormatAssetPathName.find_last_of("/");
							if (endOfDirectoryIndex != string::npos)
							{
								directoryPathName = stagingChangeFileFormatAssetPathName.substr(0, endOfDirectoryIndex);

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
								+ ", _processorIdentifier: " + to_string(_processorIdentifier)
								+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
								+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
								+ ", directoryPathName: " + directoryPathName
								+ ", exception: " + e.what()
							);
						}
					}

					throw e;
				}

				// remove staging directory
				{
					string directoryPathName;
					try
					{
						size_t endOfDirectoryIndex = stagingChangeFileFormatAssetPathName.find_last_of("/");
						if (endOfDirectoryIndex != string::npos)
						{
							directoryPathName = stagingChangeFileFormatAssetPathName.substr(0,
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
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", stagingChangeFileFormatAssetPathName: " + stagingChangeFileFormatAssetPathName
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
						mmsAssetSizeInBytes = FileIO::getFileSizeInBytes(mmsChangeFileFormatAssetPathName,
							inCaseOfLinkHasItToBeRead);   
					}

					bool externalReadOnlyStorage = false;
					string externalDeliveryTechnology;
					string externalDeliveryURL;
					int64_t liveRecordingIngestionJobKey = -1;
					int64_t changeFormatPhysicalPathKey = _mmsEngineDBFacade->saveVariantContentMetadata(
						workspace->_workspaceKey,
						ingestionJobKey,
						liveRecordingIngestionJobKey,
						mediaItemKey,
						externalReadOnlyStorage,
						externalDeliveryTechnology,
						externalDeliveryURL,
						changeFormatFileName,
						relativePath,
						mmsPartitionIndexUsed,
						mmsAssetSizeInBytes,
						-1,	// encodingProfileKey,

						mediaInfoDetails,
						videoTracks,
						audioTracks,
						/*
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
						*/

						imageWidth,
						imageHeight,
						imageFormat,
						imageQuality
					);

					_logger->info(__FILEREF__ + "Saved the Encoded content"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", changeFormatPhysicalPathKey: " + to_string(changeFormatPhysicalPathKey)
					);
				}
				catch(exception e)
				{
					_logger->error(__FILEREF__ + "_mmsEngineDBFacade->saveVariantContentMetadata failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
					);

					if (FileIO::fileExisting(mmsChangeFileFormatAssetPathName))
					{
						_logger->info(__FILEREF__ + "Remove"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", mmsChangeFileFormatAssetPathName: " + mmsChangeFileFormatAssetPathName
						);

						FileIO::remove(mmsChangeFileFormatAssetPathName);
					}

					throw e;
				}
			}
        }

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_TaskSuccess"
            + ", errorMessage: " + ""
        );
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
        );
    }
    catch (runtime_error& e) 
    {
        _logger->error(__FILEREF__ + "ChangeFileFormat failed"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "ChangeFileFormat failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
}

void MMSEngineProcessor::copyContentThread(
        shared_ptr<long> processorsThreadsNumber, int64_t ingestionJobKey, string mmsAssetPathName, 
        string localPath, string localFileName)
{

    try 
    {
        string localPathName = localPath;
        if (localFileName != "")
        {
			string cleanedFileName;
			{
				cleanedFileName.resize(localFileName.size());
				transform(localFileName.begin(), localFileName.end(), cleanedFileName.begin(),
					[](unsigned char c)
						{
							if(c == '/'
								)
								return (int) ' ';
							else
								return (int) c;
						}
				);

				string fileFormat;
				{
					size_t extensionIndex = mmsAssetPathName.find_last_of(".");
					if (extensionIndex == string::npos)
					{
						string errorMessage = __FILEREF__ +
							"No fileFormat (extension of the file) found in mmsAssetPathName"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", mmsAssetPathName: " + mmsAssetPathName
						;
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}
					fileFormat = mmsAssetPathName.substr(extensionIndex + 1);
				}

				string suffix = "." + fileFormat;
				if (cleanedFileName.size() >= suffix.size()
					&& 0 == cleanedFileName.compare(cleanedFileName.size()-suffix.size(),
						suffix.size(), suffix))
					;
				else
					cleanedFileName += suffix;

				string prefix = "MMS ";
				cleanedFileName = prefix + cleanedFileName;
			}

            if (localPathName.back() != '/')
                localPathName += "/";
            localPathName += cleanedFileName;
        }

        _logger->info(__FILEREF__ + "Coping"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", localPath: " + localPath
            + ", localFileName: " + localFileName
            + ", localPathName: " + localPathName
        );

        FileIO::copyFile(mmsAssetPathName, localPathName);
            
        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_TaskSuccess"
            + ", errorMessage: " + ""
        );                            
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
        );
    }
    catch (runtime_error& e) 
    {
        _logger->error(__FILEREF__ + "Coping failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", mmsAssetPathName: " + mmsAssetPathName 
            + ", localPath: " + localPath
            + ", localFileName: " + localFileName
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Coping failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", mmsAssetPathName: " + mmsAssetPathName 
            + ", localPath: " + localPath
            + ", localFileName: " + localFileName
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
}

void MMSEngineProcessor::extractTracksContentThread(
        shared_ptr<long> processorsThreadsNumber, int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies)

{
    try 
    {
        _logger->info(__FILEREF__ + "Extracting Tracks"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
                
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured media to be used to extract a track"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        vector<pair<string,int>> tracksToBeExtracted;
        string outputFileFormat;
        {
            {
                string field = "Tracks";
                Json::Value tracksToot = parametersRoot[field];
                if (tracksToot.size() == 0)
                {
                    string errorMessage = __FILEREF__ + "No correct number of Tracks"
                            + ", tracksToot.size: " + to_string(tracksToot.size());
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                for (int trackIndex = 0; trackIndex < tracksToot.size(); trackIndex++)
                {
                    Json::Value trackRoot = tracksToot[trackIndex];

                    field = "TrackType";
                    if (!JSONUtils::isMetadataPresent(trackRoot, field))
                    {
                        Json::StreamWriterBuilder wbuilder;
                        string sTrackRoot = Json::writeString(wbuilder, trackRoot);

                        string errorMessage = __FILEREF__ + "Field is not present or it is null"
                                + ", Field: " + field
                                + ", sTrackRoot: " + sTrackRoot
                                ;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    string trackType = trackRoot.get(field, "XXX").asString();

                    int trackNumber = 0;
                    field = "TrackNumber";
                    if (JSONUtils::isMetadataPresent(trackRoot, field))
                        trackNumber = JSONUtils::asInt(trackRoot, field, 0);

                    tracksToBeExtracted.push_back(make_pair(trackType, trackNumber));
                }
            }

            string field = "OutputFileFormat";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            outputFileFormat = parametersRoot.get(field, "XXX").asString();
        }

        for(vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>::iterator it = dependencies.begin(); 
                it != dependencies.end(); ++it) 
        {
            tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType = *it;

            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;

            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

			string mmsAssetPathName;
			/*
            int mmsPartitionNumber;
            string workspaceDirectoryName;
            string relativePath;
            string fileName;
            shared_ptr<Workspace> workspace;
			*/
            
            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                int64_t encodingProfileKey = -1;
                
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
				tie(ignore, mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(
                        key, encodingProfileKey);

                int64_t physicalPathKey;
                string deliveryFileName;
                string title;
                int64_t sizeInBytes;
                
                tie(physicalPathKey, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(mmsAssetPathName, ignore, ignore, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;
				/*
                tuple<int64_t,int,shared_ptr<Workspace>,string,string,string,string,int64_t> storageDetails 
                    = _mmsEngineDBFacade->getStorageDetails(key);

                string deliveryFileName;
                string title;
                int64_t sizeInBytes;
                
                tie(ignore, mmsPartitionNumber, workspace, relativePath, fileName, deliveryFileName, title, sizeInBytes) 
                        = storageDetails;
                workspaceDirectoryName = workspace->_directoryName;
				*/
            }

			/*
            _logger->info(__FILEREF__ + "getMMSAssetPathName ..."
                + ", mmsPartitionNumber: " + to_string(mmsPartitionNumber)
                + ", workspaceDirectoryName: " + workspaceDirectoryName
                + ", relativePath: " + relativePath
                + ", fileName: " + fileName
            );
            string mmsAssetPathName = _mmsStorage->getMMSAssetPathName(
                mmsPartitionNumber,
                workspaceDirectoryName,
                relativePath,
                fileName);
			*/
            
            {
                string localSourceFileName;
                string extractTrackMediaPathName;
                {
                    localSourceFileName = to_string(ingestionJobKey)
                            + "_" + to_string(key)
                            + "_extractTrack"
                            + "." + outputFileFormat
                            ;

                    string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
                        workspace);
                    extractTrackMediaPathName = workspaceIngestionRepository + "/" 
                            + localSourceFileName;
                }

                FFMpeg ffmpeg (_configuration, _logger);

                ffmpeg.extractTrackMediaToIngest(
                    ingestionJobKey,
                    mmsAssetPathName,
                    tracksToBeExtracted,
                    extractTrackMediaPathName);

                _logger->info(__FILEREF__ + "extractTrackMediaToIngest done"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", extractTrackMediaPathName: " + extractTrackMediaPathName
                );

                string title;
				int64_t imageOfVideoMediaItemKey = -1;
				int64_t cutOfVideoMediaItemKey = -1;
				int64_t cutOfAudioMediaItemKey = -1;
				double startTimeInSeconds = 0.0;
				double endTimeInSeconds = 0.0;
                string mediaMetaDataContent = generateMediaMetadataToIngest(
					ingestionJobKey,
					outputFileFormat,
					title,
					imageOfVideoMediaItemKey,
					cutOfVideoMediaItemKey, cutOfAudioMediaItemKey, startTimeInSeconds, endTimeInSeconds,
					parametersRoot
				);

                {
                    shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                            ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

                    localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
                    localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
                    localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

					localAssetIngestionEvent->setExternalReadOnlyStorage(false);
                    localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
                    localAssetIngestionEvent->setIngestionSourceFileName(localSourceFileName);
                    localAssetIngestionEvent->setMMSSourceFileName(localSourceFileName);
                    localAssetIngestionEvent->setWorkspace(workspace);
                    localAssetIngestionEvent->setIngestionType(MMSEngineDBFacade::IngestionType::AddContent);            
                    localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(
                        it + 1 == dependencies.end() ? true : false);

                    // to manage a ffmpeg bug generating a corrupted/wrong avgFrameRate, we will
                    // force the concat file to have the same avgFrameRate of the source media
                    // Uncomment next statements in case the problem is still present event in case of the ExtractTracks task
                    // if (forcedAvgFrameRate != "" && concatContentType == MMSEngineDBFacade::ContentType::Video)
                    //    localAssetIngestionEvent->setForcedAvgFrameRate(forcedAvgFrameRate);            

                    localAssetIngestionEvent->setMetadataContent(mediaMetaDataContent);

                    shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
                    _multiEventsSet->addEvent(event);

                    _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", getEventKey().first: " + to_string(event->getEventKey().first)
                        + ", getEventKey().second: " + to_string(event->getEventKey().second));
                }
            }
        }
    }
    catch (runtime_error& e) 
    {
        _logger->error(__FILEREF__ + "Extracting tracks failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Extracting tracks failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
}

void MMSEngineProcessor::handleMultiLocalAssetIngestionEventThread (
		shared_ptr<long> processorsThreadsNumber,
    MultiLocalAssetIngestionEvent multiLocalAssetIngestionEvent)
{
    
    string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
            multiLocalAssetIngestionEvent.getWorkspace());
    vector<string> generatedFramesFileNames;
    
    try
    {
        // get files from file system       
        {
            string generatedFrames_BaseFileName = to_string(multiLocalAssetIngestionEvent.getIngestionJobKey());

            FileIO::DirectoryEntryType_t detDirectoryEntryType;
            shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (workspaceIngestionRepository + "/");

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

                    if (directoryEntry.size() >= generatedFrames_BaseFileName.size() && 0 == directoryEntry.compare(0, generatedFrames_BaseFileName.size(), generatedFrames_BaseFileName))
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
           
        // we have one ingestion job row and one or more generated frames to be ingested
        // One MIK in case of a .mjpeg
        // One or more MIKs in case of .jpg
        // We want to update the ingestion row just once at the end,
        // in case of success or when an error happens.
        // To do this we will add a field in the localAssetIngestionEvent structure (ingestionRowToBeUpdatedAsSuccess)
        // and we will set it to false except for the last frame where we will set to true
        // In case of error, handleLocalAssetIngestionEvent will update ingestion row
        // and we will not call anymore handleLocalAssetIngestionEvent for the next frames
        // When I say 'update the ingestion row', it's not just the update but it is also
        // manageIngestionJobStatusUpdate
        bool generatedFrameIngestionFailed = false;

        for(vector<string>::iterator it = generatedFramesFileNames.begin(); 
                it != generatedFramesFileNames.end(); ++it) 
        {
            string generatedFrameFileName = *it;

            if (generatedFrameIngestionFailed)
            {
                string workspaceIngestionBinaryPathName = workspaceIngestionRepository 
                        + "/"
                        + generatedFrameFileName
                        ;

                _logger->info(__FILEREF__ + "Remove file"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
                    + ", workspaceIngestionBinaryPathName: " + workspaceIngestionBinaryPathName
                );
                FileIO::remove(workspaceIngestionBinaryPathName);
            }
            else
            {
                _logger->info(__FILEREF__ + "Generated Frame to ingest"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
                    + ", generatedFrameFileName: " + generatedFrameFileName
                    // + ", textToBeReplaced: " + textToBeReplaced
                    // + ", textToReplace: " + textToReplace
                );

                string fileFormat;
                size_t extensionIndex = generatedFrameFileName.find_last_of(".");
                if (extensionIndex == string::npos)
                {
                    string errorMessage = __FILEREF__ + "No fileFormat (extension of the file) found in generatedFileName"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
                            + ", generatedFrameFileName: " + generatedFrameFileName
                    ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                fileFormat = generatedFrameFileName.substr(extensionIndex + 1);

    //            if (mmsSourceFileName.find(textToBeReplaced) != string::npos)
    //                mmsSourceFileName.replace(mmsSourceFileName.find(textToBeReplaced), textToBeReplaced.length(), textToReplace);

                _logger->info(__FILEREF__ + "Generated Frame to ingest"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
                    + ", new generatedFrameFileName: " + generatedFrameFileName
                    + ", fileFormat: " + fileFormat
                );

                string title;
                {
                    string field = "Title";
                    if (JSONUtils::isMetadataPresent(multiLocalAssetIngestionEvent.getParametersRoot(), field))
                        title = multiLocalAssetIngestionEvent.getParametersRoot().get(field, "XXX").asString();                    
                    title += (
                            " (" 
                            + to_string(it - generatedFramesFileNames.begin() + 1) 
                            + " / "
                            + to_string(generatedFramesFileNames.size())
                            + ")"
                            );
                }
				int64_t imageOfVideoMediaItemKey = -1;
				int64_t cutOfVideoMediaItemKey = -1;
				int64_t cutOfAudioMediaItemKey = -1;
				double startTimeInSeconds = 0.0;
				double endTimeInSeconds = 0.0;
                string imageMetaDataContent = generateMediaMetadataToIngest(
                        multiLocalAssetIngestionEvent.getIngestionJobKey(),
                        // mjpeg,
                        fileFormat,
                        title,
						imageOfVideoMediaItemKey,
						cutOfVideoMediaItemKey, cutOfAudioMediaItemKey, startTimeInSeconds, endTimeInSeconds,
                        multiLocalAssetIngestionEvent.getParametersRoot()
                );

                {
                    // shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                    //        ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);
                    shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent 
                            = make_shared<LocalAssetIngestionEvent>();

                    localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
                    localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
                    localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

					localAssetIngestionEvent->setExternalReadOnlyStorage(false);
                    localAssetIngestionEvent->setIngestionJobKey(multiLocalAssetIngestionEvent.getIngestionJobKey());
                    localAssetIngestionEvent->setIngestionSourceFileName(generatedFrameFileName);
                    localAssetIngestionEvent->setMMSSourceFileName(generatedFrameFileName);
                    localAssetIngestionEvent->setWorkspace(multiLocalAssetIngestionEvent.getWorkspace());
                    localAssetIngestionEvent->setIngestionType(MMSEngineDBFacade::IngestionType::AddContent);
                    localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(
                        it + 1 == generatedFramesFileNames.end() ? true : false);

                    localAssetIngestionEvent->setMetadataContent(imageMetaDataContent);

                    try
                    {
                        handleLocalAssetIngestionEventThread (
							processorsThreadsNumber, *localAssetIngestionEvent);
                    }
                    catch(runtime_error e)
                    {
                        generatedFrameIngestionFailed = true;

                        _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", exception: " + e.what()
                        );
                    }
                    catch(exception e)
                    {
                        generatedFrameIngestionFailed = true;

                        _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", exception: " + e.what()
                        );
                    }

//                    shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
//                    _multiEventsSet->addEvent(event);
//
//                    _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
//                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
//                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
//                        + ", getEventKey().first: " + to_string(event->getEventKey().first)
//                        + ", getEventKey().second: " + to_string(event->getEventKey().second));
                }
            }
        }
        
        /*
        if (generatedFrameIngestionFailed)
        {
            _logger->info(__FILEREF__ + "updater->updateEncodingJob PunctualError"
                + ", _encodingItem->_encodingJobKey: " + to_string(multiLocalAssetIngestionEvent->getEncodingJobKey())
                + ", _encodingItem->_ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent->getIngestionJobKey())
            );

            int64_t mediaItemKey = -1;
            int64_t encodedPhysicalPathKey = -1;
            // PunctualError is used because, in case it always happens, the encoding will never reach a final state
            int encodingFailureNumber = updater->updateEncodingJob (
                    multiLocalAssetIngestionEvent->getEncodingJobKey(), 
                    MMSEngineDBFacade::EncodingError::PunctualError,    // ErrorBeforeEncoding, 
                    mediaItemKey, encodedPhysicalPathKey,
                    multiLocalAssetIngestionEvent->getIngestionJobKey());

            _logger->info(__FILEREF__ + "updater->updateEncodingJob PunctualError"
                + ", _encodingItem->_encodingJobKey: " + to_string(multiLocalAssetIngestionEvent->getEncodingJobKey())
                + ", _encodingItem->_ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent->getIngestionJobKey())
                + ", encodingFailureNumber: " + to_string(encodingFailureNumber)
            );
        }
        else
        {
            _logger->info(__FILEREF__ + "updater->updateEncodingJob NoError"
                + ", _encodingItem->_encodingJobKey: " + to_string(multiLocalAssetIngestionEvent->getEncodingJobKey())
                + ", _encodingItem->_ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent->getIngestionJobKey())
            );

            int64_t mediaItemKey = -1;
            int64_t encodedPhysicalPathKey = -1;
            updater->updateEncodingJob (
                multiLocalAssetIngestionEvent->getEncodingJobKey(), 
                MMSEngineDBFacade::EncodingError::NoError,
                mediaItemKey, encodedPhysicalPathKey,
                multiLocalAssetIngestionEvent->getIngestionJobKey());
        }
        */
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "handleMultiLocalAssetIngestionEvent failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
            + ", e.what(): " + e.what()
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (multiLocalAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_IngestionFailure,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
				);
		}

        bool exceptionInCaseOfError = false;
        
        for(vector<string>::iterator it = generatedFramesFileNames.begin(); 
                it != generatedFramesFileNames.end(); ++it) 
        {
            string workspaceIngestionBinaryPathName = workspaceIngestionRepository + "/" + *it;
            
            _logger->info(__FILEREF__ + "Remove file"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
                + ", workspaceIngestionBinaryPathName: " + workspaceIngestionBinaryPathName
            );
            FileIO::remove(workspaceIngestionBinaryPathName, exceptionInCaseOfError);
        }
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "handleMultiLocalAssetIngestionEvent failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (multiLocalAssetIngestionEvent.getIngestionJobKey(),
				MMSEngineDBFacade::IngestionStatus::End_IngestionFailure,
				e.what()
			);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
				+ ", errorMessage: " + ex.what()
				);
		}

        bool exceptionInCaseOfError = false;
        
        for(vector<string>::iterator it = generatedFramesFileNames.begin(); 
                it != generatedFramesFileNames.end(); ++it) 
        {
            string workspaceIngestionBinaryPathName = workspaceIngestionRepository + "/" + *it;
            
            _logger->info(__FILEREF__ + "Remove file"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(multiLocalAssetIngestionEvent.getIngestionJobKey())
                + ", workspaceIngestionBinaryPathName: " + workspaceIngestionBinaryPathName
            );
            FileIO::remove(workspaceIngestionBinaryPathName, exceptionInCaseOfError);
        }
        
        throw e;
    }
    
}

// this is to generate one Frame
void MMSEngineProcessor::generateAndIngestFramesThread(
        shared_ptr<long> processorsThreadsNumber, int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        MMSEngineDBFacade::IngestionType ingestionType,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies
)
{
    try
    {
        string field;
        
        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "No video found"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        int periodInSeconds;
        double startTimeInSeconds;
        int maxFramesNumber;
        string videoFilter;
        bool mjpeg;
        int imageWidth;
        int imageHeight;
        int64_t sourcePhysicalPathKey;
        string sourcePhysicalPath;
        int64_t durationInMilliSeconds;
        int64_t sourceVideoMediaItemKey = fillGenerateFramesParameters(
                workspace,
                ingestionJobKey,
                ingestionType,
                parametersRoot,
                dependencies,
                
                periodInSeconds, startTimeInSeconds,
                maxFramesNumber, videoFilter,
                mjpeg, imageWidth, imageHeight,
                sourcePhysicalPathKey, sourcePhysicalPath, durationInMilliSeconds);
        
        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
                workspace);

		pid_t childPid;
        FFMpeg ffmpeg (_configuration, _logger);

        vector<string> generatedFramesFileNames = ffmpeg.generateFramesToIngest(
                ingestionJobKey,
                0,  // encodingJobKey
                workspaceIngestionRepository,
                to_string(ingestionJobKey),    // imageBaseFileName,
                startTimeInSeconds,
                maxFramesNumber,
                videoFilter,
                periodInSeconds,
                mjpeg,
                imageWidth, 
                imageHeight,
                sourcePhysicalPath,
                durationInMilliSeconds,
				&childPid
        );

        _logger->info(__FILEREF__ + "generateFramesToIngest done"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", generatedFramesFileNames.size: " + to_string(generatedFramesFileNames.size())
        );
        
        if (generatedFramesFileNames.size() == 0)
        {
            MMSEngineDBFacade::IngestionStatus newIngestionStatus 
                    = MMSEngineDBFacade::IngestionStatus::End_TaskSuccess;

            string errorMessage;
            string processorMMS;
            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + MMSEngineDBFacade::toString(newIngestionStatus)
                + ", errorMessage: " + errorMessage
                + ", processorMMS: " + processorMMS
            );
            _mmsEngineDBFacade->updateIngestionJob (
                ingestionJobKey, 
                newIngestionStatus, errorMessage);
        }
        else
        {
            // we have one ingestion job row and many generatd frames to be ingested
            // We want to update the ingestion row just once at the end in case of success
            // or when an error happens.
            // To do this we will add a field in the localAssetIngestionEvent structure (ingestionRowToBeUpdatedAsSuccess)
            // and we will set it to false but the last frame that we will set to true
            // In case of error, handleLocalAssetIngestionEvent will update ingestion row
            // and we will not call anymore handleLocalAssetIngestionEvent for the next frames
            // When I say 'update the ingestion row', it's not just the update but it is also
            // manageIngestionJobStatusUpdate
            bool generatedFrameIngestionFailed = false;

            for(vector<string>::iterator it = generatedFramesFileNames.begin(); 
                    it != generatedFramesFileNames.end(); ++it) 
            {
                string generatedFrameFileName = *it;

                if (generatedFrameIngestionFailed)
                {
                    string workspaceIngestionBinaryPathName;

                    workspaceIngestionBinaryPathName = _mmsStorage->getWorkspaceIngestionRepository(workspace);
                    workspaceIngestionBinaryPathName
                            .append("/")
                            .append(generatedFrameFileName)
                            ;

                    _logger->info(__FILEREF__ + "Remove file"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", workspaceIngestionBinaryPathName: " + workspaceIngestionBinaryPathName
                    );
                    FileIO::remove(workspaceIngestionBinaryPathName);
                }
                else
                {
                    _logger->info(__FILEREF__ + "Generated Frame to ingest"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", generatedFrameFileName: " + generatedFrameFileName
                        // + ", textToBeReplaced: " + textToBeReplaced
                        // + ", textToReplace: " + textToReplace
                    );

                    string fileFormat;
                    size_t extensionIndex = generatedFrameFileName.find_last_of(".");
                    if (extensionIndex == string::npos)
                    {
                        string errorMessage = __FILEREF__ + "No fileFormat (extension of the file) found in generatedFileName"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", generatedFrameFileName: " + generatedFrameFileName
                        ;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    fileFormat = generatedFrameFileName.substr(extensionIndex + 1);

        //            if (mmsSourceFileName.find(textToBeReplaced) != string::npos)
        //                mmsSourceFileName.replace(mmsSourceFileName.find(textToBeReplaced), textToBeReplaced.length(), textToReplace);

                    _logger->info(__FILEREF__ + "Generated Frame to ingest"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", new generatedFrameFileName: " + generatedFrameFileName
                        + ", fileFormat: " + fileFormat
                    );

                    string title;
					int64_t imageOfVideoMediaItemKey = sourceVideoMediaItemKey;
					int64_t cutOfVideoMediaItemKey = -1;
					int64_t cutOfAudioMediaItemKey = -1;
					double startTimeInSeconds = 0.0;
					double endTimeInSeconds = 0.0;
                    string imageMetaDataContent = generateMediaMetadataToIngest(
                        ingestionJobKey,
                        fileFormat,
                        title,
						imageOfVideoMediaItemKey,
						cutOfVideoMediaItemKey, cutOfAudioMediaItemKey, startTimeInSeconds, endTimeInSeconds,
                        parametersRoot
                    );

                    {
                        // shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                        //        ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);
                        shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent 
                                = make_shared<LocalAssetIngestionEvent>();

                        localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
                        localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
                        localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

						localAssetIngestionEvent->setExternalReadOnlyStorage(false);
                        localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
                        localAssetIngestionEvent->setIngestionSourceFileName(generatedFrameFileName);
                        // localAssetIngestionEvent->setMMSSourceFileName(mmsSourceFileName);
                        localAssetIngestionEvent->setMMSSourceFileName(generatedFrameFileName);
                        localAssetIngestionEvent->setWorkspace(workspace);
                        localAssetIngestionEvent->setIngestionType(MMSEngineDBFacade::IngestionType::AddContent);
                        localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(
                            it + 1 == generatedFramesFileNames.end() ? true : false);

                        localAssetIngestionEvent->setMetadataContent(imageMetaDataContent);

                        try
                        {
                            handleLocalAssetIngestionEventThread (
									processorsThreadsNumber, *localAssetIngestionEvent);
                        }
                        catch(runtime_error e)
                        {
                            generatedFrameIngestionFailed = true;

                            _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", exception: " + e.what()
                            );
                        }
                        catch(exception e)
                        {
                            generatedFrameIngestionFailed = true;

                            _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", exception: " + e.what()
                            );
                        }

                        /*
                        shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
                        _multiEventsSet->addEvent(event);

                        _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", getEventKey().first: " + to_string(event->getEventKey().first)
                            + ", getEventKey().second: " + to_string(event->getEventKey().second));
                        */
                    }
                }
            }
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "generateAndIngestFrame failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
		// it's a thread, no throw
        // throw e;
        return;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "generateAndIngestFrame failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
		// it's a thread, no throw
        // throw e;
        return;
    }
}

void MMSEngineProcessor::manageGenerateFramesTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        MMSEngineDBFacade::IngestionType ingestionType,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong number of dependencies"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::EncodingPriority encodingPriority;
        string field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = 
                    static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority =
                MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
        }

        int periodInSeconds;
        double startTimeInSeconds;
        int maxFramesNumber;
        string videoFilter;
        bool mjpeg;
        int imageWidth;
        int imageHeight;
        int64_t sourcePhysicalPathKey;
        string sourcePhysicalPath;
        int64_t durationInMilliSeconds;
        fillGenerateFramesParameters(
                workspace,
                ingestionJobKey,
                ingestionType,
                parametersRoot,
                dependencies,
                
                periodInSeconds, startTimeInSeconds,
                maxFramesNumber, videoFilter,
                mjpeg, imageWidth, imageHeight,
                sourcePhysicalPathKey, sourcePhysicalPath,
                durationInMilliSeconds);

        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
                workspace);

        _mmsEngineDBFacade->addEncoding_GenerateFramesJob (
                workspace,
                ingestionJobKey, encodingPriority,
                workspaceIngestionRepository, 
                startTimeInSeconds, maxFramesNumber, 
                videoFilter, periodInSeconds, 
                mjpeg, imageWidth, imageHeight,
                sourcePhysicalPathKey,
                durationInMilliSeconds
                );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageGenerateFramesTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageGenerateFramesTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

int64_t MMSEngineProcessor::fillGenerateFramesParameters(
    shared_ptr<Workspace> workspace,
    int64_t ingestionJobKey,
    MMSEngineDBFacade::IngestionType ingestionType,
    Json::Value parametersRoot,
    vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies,
        
    int& periodInSeconds, double& startTimeInSeconds,
    int& maxFramesNumber, string& videoFilter,
    bool& mjpeg, int& imageWidth, int& imageHeight,
    int64_t& sourcePhysicalPathKey, string& sourcePhysicalPath,
    int64_t& durationInMilliSeconds
)
{
	int64_t		sourceMediaItemKey;

    try
    {
        string field;
        
        periodInSeconds = -1;
        if (ingestionType == MMSEngineDBFacade::IngestionType::Frame)
        {
        }
        else if (ingestionType == MMSEngineDBFacade::IngestionType::PeriodicalFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByPeriodicalFrames)
        {
            field = "PeriodInSeconds";
            if (!JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            periodInSeconds = JSONUtils::asInt(parametersRoot, field, 0);
        }
        else // if (ingestionType == MMSEngineDBFacade::IngestionType::IFrames || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByIFrames)
        {
            
        }
            
        startTimeInSeconds = 0;
        if (ingestionType == MMSEngineDBFacade::IngestionType::Frame)
        {
            field = "InstantInSeconds";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                startTimeInSeconds = JSONUtils::asDouble(parametersRoot, field, 0);
            }
        }
        else if (ingestionType == MMSEngineDBFacade::IngestionType::PeriodicalFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::IFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByPeriodicalFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByIFrames)
        {
            field = "StartTimeInSeconds";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                startTimeInSeconds = JSONUtils::asDouble(parametersRoot, field, 0);
            }
        }

        maxFramesNumber = -1;
        if (ingestionType == MMSEngineDBFacade::IngestionType::Frame)
        {
            maxFramesNumber = 1;
        }
        else if (ingestionType == MMSEngineDBFacade::IngestionType::PeriodicalFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::IFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByPeriodicalFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByIFrames)
        {
            field = "MaxFramesNumber";
            if (JSONUtils::isMetadataPresent(parametersRoot, field))
            {
                maxFramesNumber = JSONUtils::asInt(parametersRoot, field, 0);
            }
        }

        if (ingestionType == MMSEngineDBFacade::IngestionType::Frame)
        {
        }
        else if (ingestionType == MMSEngineDBFacade::IngestionType::PeriodicalFrames)
        {
            videoFilter = "PeriodicFrame";
        }
        else if (ingestionType == MMSEngineDBFacade::IngestionType::IFrames)
        {
            videoFilter = "All-I-Frames";
        }

        if (ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByPeriodicalFrames
                || ingestionType == MMSEngineDBFacade::IngestionType::MotionJPEGByIFrames)
        {
            mjpeg = true;
        }
        else
        {
            mjpeg = false;
        }

        int width = -1;
        field = "Width";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            width = JSONUtils::asInt64(parametersRoot, field, 0);
        }

        int height = -1;
        field = "Height";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            height = JSONUtils::asInt(parametersRoot, field, 0);
        }

        // int64_t sourcePhysicalPathKey;
        // string sourcePhysicalPath;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType = dependencies.back();

        int64_t key;
        MMSEngineDBFacade::ContentType referenceContentType;
        Validator::DependencyType dependencyType;

        tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

        if (dependencyType == Validator::DependencyType::MediaItemKey)
        {
            int64_t encodingProfileKey = -1;
			bool warningIfMissing = false;
			tuple<int64_t, string, int, string, string, int64_t, string>
				physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
				= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
            tie(sourcePhysicalPathKey, sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore)
				= physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;

            sourceMediaItemKey = key;
        }
        else
        {
			tuple<string, int, string, string, int64_t, string>
				physicalPathFileNameSizeInBytesAndDeliveryFileName =
				_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
			tie(sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore)
				= physicalPathFileNameSizeInBytesAndDeliveryFileName;


            sourcePhysicalPathKey = key;
            

            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);
            tie(sourceMediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }
        
        /*
        _logger->info(__FILEREF__ + "fillGenerateFramesParameters. Looking for the media key"
            + ", key: " + to_string(key)
            + ", referenceContentType: " + MMSEngineDBFacade::toString(referenceContentType)
            + ", dependencyType: " + to_string(static_cast<int>(dependencyType))
            + ", key: " + to_string(key)
            + ", sourceMediaItemKey: " + to_string(sourceMediaItemKey)
            + ", sourcePhysicalPathKey: " + to_string(sourcePhysicalPathKey)
            + ", sourcePhysicalPath: " + sourcePhysicalPath
        );
         */

        int videoWidth;
        int videoHeight;
        try
        {
			/*
            long bitRate;
            string videoCodecName;
            string videoProfile;
            string videoAvgFrameRate;
            long videoBitRate;
            string audioCodecName;
            long audioSampleRate;
            int audioChannels;
            long audioBitRate;
        
            tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long>
                videoDetails = _mmsEngineDBFacade->getVideoDetails(sourceMediaItemKey, sourcePhysicalPathKey);
            
            tie(durationInMilliSeconds, bitRate,
                videoCodecName, videoProfile, videoWidth, videoHeight, videoAvgFrameRate, videoBitRate,
                audioCodecName, audioSampleRate, audioChannels, audioBitRate) = videoDetails;
			*/
			vector<tuple<int64_t, int, int64_t, int, int, string, string, long, string>> videoTracks;
			vector<tuple<int64_t, int, int64_t, long, string, long, int, string>> audioTracks;

			_mmsEngineDBFacade->getVideoDetails(
				sourceMediaItemKey, sourcePhysicalPathKey, videoTracks, audioTracks);
			if (videoTracks.size() == 0)
			{
				string errorMessage = __FILEREF__ + "No video track are present"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				;

				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			tuple<int64_t, int, int64_t, int, int, string, string, long, string> videoTrack = videoTracks[0];

			tie(ignore, ignore, ignore, videoWidth, videoHeight, ignore, ignore, ignore, ignore) = videoTrack;
        }
        catch(runtime_error e)
        {
            string errorMessage = __FILEREF__ + "_mmsEngineDBFacade->getVideoDetails failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", e.what(): " + e.what()
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        catch(exception e)
        {
            string errorMessage = __FILEREF__ + "_mmsEngineDBFacade->getVideoDetails failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", e.what(): " + e.what()
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        imageWidth = width == -1 ? videoWidth : width;
        imageHeight = height == -1 ? videoHeight : height;

        if (durationInMilliSeconds < startTimeInSeconds * 1000)
        {
            string errorMessage = __FILEREF__ + "Frame was not generated because instantInSeconds is bigger than the video duration"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", video sourceMediaItemKey: " + to_string(sourceMediaItemKey)
                    + ", startTimeInSeconds: " + to_string(startTimeInSeconds)
                    + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
            ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        /*
        string sourceFileName;
        field = "SourceFileName";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            sourceFileName = parametersRoot.get(field, "XXX").asString();
        }

        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
                workspace);
        */

        {
            // imageFileName = to_string(ingestionJobKey) + /* "_source" + */ ".jpg";
        }    
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "fillGenerateFramesParameters failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "fillGenerateFramesParameters failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        throw e;
    }

	return sourceMediaItemKey;
}

void MMSEngineProcessor::manageSlideShowTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No images found"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        MMSEngineDBFacade::EncodingPriority encodingPriority;
        string field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = 
                    static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority =
                MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
        }

        MMSEngineDBFacade::ContentType slideshowContentType;
        bool slideshowContentTypeInitialized = false;
        vector<string> sourcePhysicalPaths;
        
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType: dependencies)
        {
            // int64_t encodingProfileKey = -1;
            // string sourcePhysicalPath = _mmsStorage->getPhysicalPath(keyAndDependencyType.first, encodingProfileKey);

            int64_t sourceMediaItemKey;
            int64_t sourcePhysicalPathKey;
            string sourcePhysicalPath;

            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;

            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;
        
            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
				int64_t encodingProfileKey = -1;
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
                tie(sourcePhysicalPathKey, sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore) =
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;

                sourceMediaItemKey = key;
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;

                sourcePhysicalPathKey = key;

                bool warningIfMissing = false;
                tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                    _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                        workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);

                tie(sourceMediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore)
                        = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
            }
            
            sourcePhysicalPaths.push_back(sourcePhysicalPath);
            
            bool warningIfMissing = false;
            
            tuple<MMSEngineDBFacade::ContentType,string,string,string, int64_t, int64_t>
				contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey 
                    = _mmsEngineDBFacade->getMediaItemKeyDetails(
					workspace->_workspaceKey, sourceMediaItemKey, warningIfMissing);
           
            MMSEngineDBFacade::ContentType contentType;
            string localTitle;
            string userData;
            string ingestionDate;
			int64_t localIngestionJobKey;
            tie(contentType, localTitle, userData, ingestionDate, ignore, localIngestionJobKey)
				= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
            
            if (!slideshowContentTypeInitialized)
            {
                slideshowContentType = contentType;
                if (slideshowContentType != MMSEngineDBFacade::ContentType::Image)
                {
                    string errorMessage = __FILEREF__ + "It is not possible to build a slideshow with a media that is not an Image"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", slideshowContentType: " + MMSEngineDBFacade::toString(slideshowContentType)
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
            else
            {
                if (slideshowContentType != contentType)
                {
                    string errorMessage = __FILEREF__ + "Not all the References have the same ContentType"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", contentType: " + MMSEngineDBFacade::toString(contentType)
                            + ", slideshowContentType: " + MMSEngineDBFacade::toString(slideshowContentType)
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
        }

        double durationOfEachSlideInSeconds = 2;
        field = "DurationOfEachSlideInSeconds";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            durationOfEachSlideInSeconds = JSONUtils::asDouble(parametersRoot, field, 0);
        }

        int outputFrameRate = 25;
        
        _mmsEngineDBFacade->addEncoding_SlideShowJob(workspace, ingestionJobKey,
                sourcePhysicalPaths, durationOfEachSlideInSeconds, 
                outputFrameRate, encodingPriority);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageSlideShowTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageSlideShowTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::generateAndIngestConcatenationThread(
        shared_ptr<long> processorsThreadsNumber, int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies
)
{
    try
    {
        _logger->info(__FILEREF__ + "generateAndIngestConcatenationThread"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );

        if (dependencies.size() < 1)
        {
            string errorMessage = __FILEREF__ + "No enough media to be concatenated"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::ContentType concatContentType;
        bool concatContentTypeInitialized = false;
        vector<string> sourcePhysicalPaths;
        string forcedAvgFrameRate;
        
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType: dependencies)
        {
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;

            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

            int64_t sourceMediaItemKey;
            int64_t sourcePhysicalPathKey;
            string sourcePhysicalPath;
            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
				int64_t encodingProfileKey = -1;
				bool warningIfMissing = false;
				tuple<int64_t, string, int, string, string, int64_t, string>
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
					= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
                tie(sourcePhysicalPathKey, sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore) =
					physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;

                sourceMediaItemKey = key;
            }
            else
            {
				tuple<string, int, string, string, int64_t, string>
					physicalPathFileNameSizeInBytesAndDeliveryFileName =
					_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
				tie(sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore)
					= physicalPathFileNameSizeInBytesAndDeliveryFileName;

                sourcePhysicalPathKey = key;

                bool warningIfMissing = false;
                tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                    _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                        workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);

                tie(sourceMediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore)
                        = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
            }

            sourcePhysicalPaths.push_back(sourcePhysicalPath);
            
            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName
				= _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                        workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);
            
            MMSEngineDBFacade::ContentType contentType;
            {
                int64_t localMediaItemKey;
                string localTitle;
                string userData;
                string ingestionDate;
				int64_t localIngestionJobKey;
                tie(localMediaItemKey, contentType, localTitle, userData, ingestionDate,
						localIngestionJobKey, ignore)
					= mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
            }
            
            if (!concatContentTypeInitialized)
            {
                concatContentType = contentType;
                if (concatContentType != MMSEngineDBFacade::ContentType::Video
                        && concatContentType != MMSEngineDBFacade::ContentType::Audio)
                {
                    string errorMessage = __FILEREF__ + "It is not possible to concatenate a media that is not video or audio"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", concatContentType: " + MMSEngineDBFacade::toString(concatContentType)
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
            else
            {
                if (concatContentType != contentType)
                {
                    string errorMessage = __FILEREF__ + "Not all the References have the same ContentType"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", contentType: " + MMSEngineDBFacade::toString(contentType)
                            + ", concatContentType: " + MMSEngineDBFacade::toString(concatContentType)
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
            
            // to manage a ffmpeg bug generating a corrupted/wrong avgFrameRate, we will
            // force the concat file to have the same avgFrameRate of the source media
            if (concatContentType == MMSEngineDBFacade::ContentType::Video
                    && forcedAvgFrameRate == "")
            {
				/*
                tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long> videoDetails 
                    = _mmsEngineDBFacade->getVideoDetails(sourceMediaItemKey, sourcePhysicalPathKey);

                tie(ignore, ignore, ignore,
                    ignore, ignore, ignore, forcedAvgFrameRate,
                    ignore, ignore, ignore, ignore, ignore)
                    = videoDetails;
				*/
				vector<tuple<int64_t, int, int64_t, int, int, string, string, long, string>> videoTracks;
				vector<tuple<int64_t, int, int64_t, long, string, long, int, string>> audioTracks;

				_mmsEngineDBFacade->getVideoDetails(
					sourceMediaItemKey, sourcePhysicalPathKey, videoTracks, audioTracks);
				if (videoTracks.size() == 0)
				{
					string errorMessage = __FILEREF__ + "No video track are present"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					;

					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}

				tuple<int64_t, int, int64_t, int, int, string, string, long, string> videoTrack = videoTracks[0];

				tie(ignore, ignore, ignore, ignore, ignore, forcedAvgFrameRate, ignore, ignore, ignore) = videoTrack;
            }
        }

        // this is a concat, so destination file name shall have the same
        // extension as the source file name
        string fileFormat;
        size_t extensionIndex = sourcePhysicalPaths.front().find_last_of(".");
        if (extensionIndex == string::npos)
        {
            string errorMessage = __FILEREF__ + "No fileFormat (extension of the file) found in sourcePhysicalPath"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", sourcePhysicalPaths.front(): " + sourcePhysicalPaths.front()
            ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        fileFormat = sourcePhysicalPaths.front().substr(extensionIndex + 1);

        string localSourceFileName = to_string(ingestionJobKey)
                + "_concat"
                + "." + fileFormat // + "_source"
                ;

        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
            workspace);
        string concatenatedMediaPathName = workspaceIngestionRepository + "/" 
                + localSourceFileName;
        
        if (sourcePhysicalPaths.size() == 1)
        {
            string sourcePhysicalPath = sourcePhysicalPaths.at(0);
            _logger->info(__FILEREF__ + "Coping"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", sourcePhysicalPath: " + sourcePhysicalPath
                + ", concatenatedMediaPathName: " + concatenatedMediaPathName
            );

            FileIO::copyFile(sourcePhysicalPath, concatenatedMediaPathName);
        }
        else
        {
            FFMpeg ffmpeg (_configuration, _logger);
            ffmpeg.generateConcatMediaToIngest(ingestionJobKey, sourcePhysicalPaths, concatenatedMediaPathName);
        }

        _logger->info(__FILEREF__ + "generateConcatMediaToIngest done"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", concatenatedMediaPathName: " + concatenatedMediaPathName
        );

        double maxDurationInSeconds = 0.0;
        double extraSecondsToCutWhenMaxDurationIsReached = 0.0;
        string field = "MaxDurationInSeconds";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
		{
			maxDurationInSeconds = JSONUtils::asDouble(parametersRoot, field, 0.0);

			field = "ExtraSecondsToCutWhenMaxDurationIsReached";
			if (JSONUtils::isMetadataPresent(parametersRoot, field))
			{
				extraSecondsToCutWhenMaxDurationIsReached = JSONUtils::asDouble(parametersRoot, field, 0.0);

				if (extraSecondsToCutWhenMaxDurationIsReached >= abs(maxDurationInSeconds))
					extraSecondsToCutWhenMaxDurationIsReached = 0.0;
			}
		}
		_logger->info(__FILEREF__ + "duration check"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", maxDurationInSeconds: " + to_string(maxDurationInSeconds)
			+ ", extraSecondsToCutWhenMaxDurationIsReached: " + to_string(extraSecondsToCutWhenMaxDurationIsReached)
		);
		if (maxDurationInSeconds != 0.0)
		{
			pair<int64_t, long> mediaInfoDetails;
			vector<tuple<int, int64_t, string, string, int, int, string, long>> videoTracks;
			vector<tuple<int, int64_t, string, long, int, long, string>> audioTracks;
			int64_t durationInMilliSeconds;


			_logger->info(__FILEREF__ + "Calling ffmpeg.getMediaInfo"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", concatenatedMediaPathName: " + concatenatedMediaPathName
			);
			FFMpeg ffmpeg (_configuration, _logger);
			// tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long> mediaInfo =
			mediaInfoDetails = ffmpeg.getMediaInfo(concatenatedMediaPathName, videoTracks, audioTracks);

			//tie(durationInMilliSeconds, ignore,
			//	ignore, ignore, ignore, ignore, ignore, ignore,
			//	ignore, ignore, ignore, ignore) = mediaInfo;
			tie(durationInMilliSeconds, ignore) = mediaInfoDetails;

			_logger->info(__FILEREF__ + "duration check"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
				+ ", maxDurationInSeconds: " + to_string(maxDurationInSeconds)
				+ ", extraSecondsToCutWhenMaxDurationIsReached: " + to_string(extraSecondsToCutWhenMaxDurationIsReached)
			);
			if (durationInMilliSeconds > abs(maxDurationInSeconds) * 1000)
			{
				string localCutSourceFileName = to_string(ingestionJobKey)
					+ "_concat_cut"
					+ "." + fileFormat // + "_source"
				;

				string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
					workspace);
				string cutMediaPathName = workspaceIngestionRepository + "/" 
					+ localCutSourceFileName;

				bool keyFrameSeeking = false;
				double startTimeInSeconds;
				double endTimeInSeconds;
				if (maxDurationInSeconds < 0.0)
				{
					startTimeInSeconds = ((durationInMilliSeconds / 1000) -
							(abs(maxDurationInSeconds) - extraSecondsToCutWhenMaxDurationIsReached));
					endTimeInSeconds = durationInMilliSeconds / 1000;
				}
				else
				{
					startTimeInSeconds = 0.0;
					endTimeInSeconds = maxDurationInSeconds - extraSecondsToCutWhenMaxDurationIsReached;
				}
				int framesNumber = -1;

				_logger->info(__FILEREF__ + "Calling ffmpeg.generateCutMediaToIngest"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", concatenatedMediaPathName: " + concatenatedMediaPathName
					+ ", keyFrameSeeking: " + to_string(keyFrameSeeking)
					+ ", startTimeInSeconds: " + to_string(startTimeInSeconds)
					+ ", endTimeInSeconds: " + to_string(endTimeInSeconds)
					+ ", framesNumber: " + to_string(framesNumber)
				);

				ffmpeg.generateCutMediaToIngest(ingestionJobKey, concatenatedMediaPathName, 
					keyFrameSeeking, startTimeInSeconds, endTimeInSeconds, framesNumber,
					cutMediaPathName);

				_logger->info(__FILEREF__ + "generateCutMediaToIngest done"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", cutMediaPathName: " + cutMediaPathName
				);

				localSourceFileName = localCutSourceFileName;

				_logger->info(__FILEREF__ + "Remove file"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", concatenatedMediaPathName: " + concatenatedMediaPathName
				);

				bool exceptionInCaseOfError = false;
				FileIO::remove(concatenatedMediaPathName, exceptionInCaseOfError);
			}
		}

		{
			string title;
			int64_t imageOfVideoMediaItemKey = -1;
			int64_t cutOfVideoMediaItemKey = -1;
			int64_t cutOfAudioMediaItemKey = -1;
			double startTimeInSeconds = 0.0;
			double endTimeInSeconds = 0.0;
			string mediaMetaDataContent = generateMediaMetadataToIngest(
				ingestionJobKey,
				// concatContentType == MMSEngineDBFacade::ContentType::Video ? true : false,
				fileFormat,
				title,
				imageOfVideoMediaItemKey,
				cutOfVideoMediaItemKey, cutOfAudioMediaItemKey, startTimeInSeconds, endTimeInSeconds,
				parametersRoot
			);

			{
				shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
					->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

				localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
				localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
				localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

				localAssetIngestionEvent->setExternalReadOnlyStorage(false);
				localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
				localAssetIngestionEvent->setIngestionSourceFileName(localSourceFileName);
				localAssetIngestionEvent->setMMSSourceFileName(localSourceFileName);
				localAssetIngestionEvent->setWorkspace(workspace);
				localAssetIngestionEvent->setIngestionType(MMSEngineDBFacade::IngestionType::AddContent);            
				localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(true);

				// to manage a ffmpeg bug generating a corrupted/wrong avgFrameRate, we will
				// force the concat file to have the same avgFrameRate of the source media
				if (forcedAvgFrameRate != "" && concatContentType == MMSEngineDBFacade::ContentType::Video)
					localAssetIngestionEvent->setForcedAvgFrameRate(forcedAvgFrameRate);            


				localAssetIngestionEvent->setMetadataContent(mediaMetaDataContent);

				shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
				_multiEventsSet->addEvent(event);

				_logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", getEventKey().first: " + to_string(event->getEventKey().first)
					+ ", getEventKey().second: " + to_string(event->getEventKey().second));
			}
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "generateAndIngestConcatenationThread failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
		// it's a thread, no throw
        // throw e;
        return;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "generateAndIngestConcatenationThread failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
		// it's a thread, no throw
        // throw e;
        return;
    }
}

void MMSEngineProcessor::generateAndIngestCutMediaThread(
        shared_ptr<long> processorsThreadsNumber,
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>> dependencies
)
{
    try
    {
        _logger->info(__FILEREF__ + "generateAndIngestCutMediaThread"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );

        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong number of media to be cut"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        int64_t sourceMediaItemKey;
        int64_t sourcePhysicalPathKey;
        string sourcePhysicalPath;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType = dependencies.back();

        int64_t key;
        MMSEngineDBFacade::ContentType referenceContentType;
        Validator::DependencyType dependencyType;

        tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

        if (dependencyType == Validator::DependencyType::MediaItemKey)
        {
			int64_t encodingProfileKey = -1;
			bool warningIfMissing = false;
			tuple<int64_t, string, int, string, string, int64_t, string>
				physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName
				= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key, encodingProfileKey, warningIfMissing);
			tie(sourcePhysicalPathKey, sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore) =
				physicalPathKeyPhysicalPathFileNameSizeInBytesAndDeliveryFileName;

            sourceMediaItemKey = key;
        }
        else
        {
			tuple<string, int, string, string, int64_t, string>
				physicalPathFileNameSizeInBytesAndDeliveryFileName =
				_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, key);
			tie(sourcePhysicalPath, ignore, ignore, ignore, ignore, ignore)
				= physicalPathFileNameSizeInBytesAndDeliveryFileName;

            sourcePhysicalPathKey = key;

            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);
            tie(sourceMediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }

        bool warningIfMissing = false;

        tuple<MMSEngineDBFacade::ContentType,string,string,string, int64_t, int64_t>
			contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey
                = _mmsEngineDBFacade->getMediaItemKeyDetails(
				workspace->_workspaceKey, sourceMediaItemKey, warningIfMissing);
        
        MMSEngineDBFacade::ContentType contentType;
        string localTitle;
        string userData;
        string ingestionDate;
		int64_t localIngestionJobKey;
        tie(contentType, localTitle, userData, ingestionDate, ignore, localIngestionJobKey)
			= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
        
        if (contentType != MMSEngineDBFacade::ContentType::Video
                && contentType != MMSEngineDBFacade::ContentType::Audio)
        {
            string errorMessage = __FILEREF__ + "It is not possible to cut a media that is not video or audio"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", contentType: " + MMSEngineDBFacade::toString(contentType)
                    ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

		bool keyFrameSeeking = true;
        string field = "KeyFrameSeeking";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			keyFrameSeeking = JSONUtils::asBool(parametersRoot, field, true);
        }

        string outputFileFormat;
        field = "OutputFileFormat";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            outputFileFormat = parametersRoot.get(field, "XXX").asString();
        }

        // to manage a ffmpeg bug generating a corrupted/wrong avgFrameRate, we will
        // force the cut file to have the same avgFrameRate of the source media
        string forcedAvgFrameRate;
        int64_t durationInMilliSeconds;
        try
        {
            if (contentType == MMSEngineDBFacade::ContentType::Video
				|| contentType == MMSEngineDBFacade::ContentType::Audio)
			{
				durationInMilliSeconds =
					_mmsEngineDBFacade->getMediaDurationInMilliseconds(
					sourceMediaItemKey, sourcePhysicalPathKey);
			}

            if (contentType == MMSEngineDBFacade::ContentType::Video)
            {
				/*
                int videoWidth;
                int videoHeight;
                long bitRate;
                string videoCodecName;
                string videoProfile;
                long videoBitRate;
                string audioCodecName;
                long audioSampleRate;
                int audioChannels;
                long audioBitRate;

                tuple<int64_t,long,string,string,int,int,string,long,string,long,int,long>
                    videoDetails = _mmsEngineDBFacade->getVideoDetails(sourceMediaItemKey, sourcePhysicalPathKey);

                tie(durationInMilliSeconds, bitRate,
                    videoCodecName, videoProfile, videoWidth, videoHeight, forcedAvgFrameRate, videoBitRate,
                    audioCodecName, audioSampleRate, audioChannels, audioBitRate) = videoDetails;
				*/

				vector<tuple<int64_t, int, int64_t, int, int, string, string, long, string>> videoTracks;
				vector<tuple<int64_t, int, int64_t, long, string, long, int, string>> audioTracks;

				_mmsEngineDBFacade->getVideoDetails(
					sourceMediaItemKey, sourcePhysicalPathKey, videoTracks, audioTracks);
				if (videoTracks.size() == 0)
				{
					string errorMessage = __FILEREF__ + "No video track are present"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					;

					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}

				tuple<int64_t, int, int64_t, int, int, string, string, long, string> videoTrack = videoTracks[0];

				tie(ignore, ignore, ignore, ignore, ignore, forcedAvgFrameRate, ignore, ignore, ignore) = videoTrack;
            }
			/*
            else if (contentType == MMSEngineDBFacade::ContentType::Audio)
            {
                string codecName;
                long bitRate;
                long sampleRate;
                int channels;

                tuple<int64_t,string,long,long,int> audioDetails = _mmsEngineDBFacade->getAudioDetails(
                    sourceMediaItemKey, sourcePhysicalPathKey);

                tie(durationInMilliSeconds, codecName, bitRate, sampleRate, channels) 
                        = audioDetails;
            }
			*/
        }
        catch(runtime_error e)
        {
            string errorMessage = __FILEREF__ + "_mmsEngineDBFacade->getVideoDetails failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", e.what(): " + e.what()
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        catch(exception e)
        {
            string errorMessage = __FILEREF__ + "_mmsEngineDBFacade->getVideoDetails failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", e.what(): " + e.what()
            ;

            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        double startTimeInSeconds;
        field = "StartTimeInSeconds";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        startTimeInSeconds = JSONUtils::asDouble(parametersRoot, field, 0.0);

        double endTimeInSeconds = 0.0;
        field = "EndTimeInSeconds";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			endTimeInSeconds = JSONUtils::asDouble(parametersRoot, field, 0.0);
        }

        int framesNumber = -1;
        field = "FramesNumber";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            framesNumber = JSONUtils::asInt(parametersRoot, field, 0);
        }

		/*
        if (endTimeInSeconds == -1 && framesNumber == -1)
        {
            string errorMessage = __FILEREF__ + "Both 'EndTimeInSeconds' and 'FramesNumber' fields are not present or it is null"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
		*/

		if (framesNumber == -1)
		{
			if (endTimeInSeconds < 0)
			{
				// if negative, it has to be subtract by the durationInMilliSeconds
				double newEndTimeInSeconds  = (durationInMilliSeconds - (endTimeInSeconds * -1000)) / 1000;

				_logger->error(__FILEREF__ + "endTimeInSeconds was changed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", video sourceMediaItemKey: " + to_string(sourceMediaItemKey)
                    + ", startTimeInSeconds: " + to_string(startTimeInSeconds)
                    + ", endTimeInSeconds: " + to_string(endTimeInSeconds)
                    + ", newEndTimeInSeconds: " + to_string(newEndTimeInSeconds)
                    + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
				);

				endTimeInSeconds = newEndTimeInSeconds;
			}
		}

        if (startTimeInSeconds > endTimeInSeconds)
        {
            string errorMessage = __FILEREF__ + "Cut was not done because startTimeInSeconds is bigger than endTimeInSeconds"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", video sourceMediaItemKey: " + to_string(sourceMediaItemKey)
                    + ", startTimeInSeconds: " + to_string(startTimeInSeconds)
                    + ", endTimeInSeconds: " + to_string(endTimeInSeconds)
                    + ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
            ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
		else
		{
			if (framesNumber == -1)
			{
				if (durationInMilliSeconds < endTimeInSeconds * 1000)
				{
					string errorMessage = __FILEREF__ + "Cut was not done because endTimeInSeconds is bigger than durationInMilliSeconds"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", video sourceMediaItemKey: " + to_string(sourceMediaItemKey)
						+ ", startTimeInSeconds: " + to_string(startTimeInSeconds)
						+ ", endTimeInSeconds: " + to_string(endTimeInSeconds)
						+ ", durationInMilliSeconds: " + to_string(durationInMilliSeconds)
					;
					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}
			}
		}

        // this is a cut so destination file name shall have the same
        // extension as the source file name
        string fileFormat;
        if (outputFileFormat == "")
        {
            size_t extensionIndex = sourcePhysicalPath.find_last_of(".");
            if (extensionIndex == string::npos)
            {
                string errorMessage = __FILEREF__ + "No fileFormat (extension of the file) found in sourcePhysicalPath"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", sourcePhysicalPath: " + sourcePhysicalPath
                ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            fileFormat = sourcePhysicalPath.substr(extensionIndex + 1);
        }
        else
        {
            fileFormat = outputFileFormat;
        }

        string localSourceFileName = to_string(ingestionJobKey)
                + "_cut"
                + "." + fileFormat // + "_source"
                ;
        
        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(
                workspace);
        string cutMediaPathName = workspaceIngestionRepository + "/"
                + localSourceFileName;
        
        FFMpeg ffmpeg (_configuration, _logger);
        ffmpeg.generateCutMediaToIngest(ingestionJobKey, sourcePhysicalPath, 
                keyFrameSeeking, startTimeInSeconds, endTimeInSeconds, framesNumber,
                cutMediaPathName);

        _logger->info(__FILEREF__ + "generateCutMediaToIngest done"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", cutMediaPathName: " + cutMediaPathName
        );
        
        string title;
		int64_t imageOfVideoMediaItemKey = -1;
		int64_t cutOfVideoMediaItemKey = -1;
		int64_t cutOfAudioMediaItemKey = -1;
		if (contentType == MMSEngineDBFacade::ContentType::Video)
			cutOfVideoMediaItemKey = sourceMediaItemKey;
		else if (contentType == MMSEngineDBFacade::ContentType::Audio)
			cutOfAudioMediaItemKey = sourceMediaItemKey;
        string mediaMetaDataContent = generateMediaMetadataToIngest(
			ingestionJobKey,
			fileFormat,
			title,
			imageOfVideoMediaItemKey,
			cutOfVideoMediaItemKey, cutOfAudioMediaItemKey, startTimeInSeconds, endTimeInSeconds,
			parametersRoot
        );

        {
            shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                    ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

            localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
            localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
            localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

			localAssetIngestionEvent->setExternalReadOnlyStorage(false);
            localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
            localAssetIngestionEvent->setIngestionSourceFileName(localSourceFileName);
            localAssetIngestionEvent->setMMSSourceFileName(localSourceFileName);
            localAssetIngestionEvent->setWorkspace(workspace);
            localAssetIngestionEvent->setIngestionType(MMSEngineDBFacade::IngestionType::AddContent);
            localAssetIngestionEvent->setIngestionRowToBeUpdatedAsSuccess(true);
            // to manage a ffmpeg bug generating a corrupted/wrong avgFrameRate, we will
            // force the concat file to have the same avgFrameRate of the source media
            if (forcedAvgFrameRate != "" && contentType == MMSEngineDBFacade::ContentType::Video)
                localAssetIngestionEvent->setForcedAvgFrameRate(forcedAvgFrameRate);            

            localAssetIngestionEvent->setMetadataContent(mediaMetaDataContent);

            shared_ptr<Event2>    event = dynamic_pointer_cast<Event2>(localAssetIngestionEvent);
            _multiEventsSet->addEvent(event);

            _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (LOCALASSETINGESTIONEVENT)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", getEventKey().first: " + to_string(event->getEventKey().first)
                + ", getEventKey().second: " + to_string(event->getEventKey().second));
        }
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "generateAndIngestCutMediaThread failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
		// it's a thread, no throw
        // throw e;
        return;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "generateAndIngestCutMediaThread failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}
        
		// it's a thread, no throw
        // throw e;
        return;
    }
}

void MMSEngineProcessor::manageEncodeTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>&
			dependencies
)
{
    try
    {        
        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong media number to be encoded"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::EncodingPriority encodingPriority;
        string field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = static_cast<MMSEngineDBFacade::EncodingPriority>(
				workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority = MMSEngineDBFacade::toEncodingPriority(
					parametersRoot.get(field, "XXX").asString());
        }

		// it is not possible to manage more than one encode because:
		// 1. inside _mmsEngineDBFacade->addEncodingJob, the ingestionJob is updated to encodingQueue
		//		and the second call will fail (because the update of the ingestion was already done
		//	2. The ingestionJob mantains the status of the encoding, how would be managed
		//		the status in case of more than one encoding?
        // for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
		// 		keyAndDependencyType: dependencies)
        MMSEngineDBFacade::ContentType referenceContentType;
		int64_t sourceMediaItemKey;
		int64_t sourcePhysicalPathKey;
        {
            int64_t key;
            Validator::DependencyType dependencyType;
            
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType	= dependencies[0];
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

			if (dependencyType == Validator::DependencyType::MediaItemKey)
			{
				sourceMediaItemKey = key;

				sourcePhysicalPathKey = -1;
			}
			else
			{
				sourcePhysicalPathKey = key;
            
				bool warningIfMissing = false;
				tuple<int64_t,MMSEngineDBFacade::ContentType,string,string, string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
					_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
					workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);

				MMSEngineDBFacade::ContentType localContentType;
				string localTitle;
				string userData;
                string ingestionDate;
				int64_t localIngestionJobKey;
				tie(sourceMediaItemKey,localContentType, localTitle, userData, ingestionDate,
						localIngestionJobKey, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
			}
		}

        // This task shall contain EncodingProfileKey or EncodingProfileLabel.
        // We cannot have EncodingProfilesSetKey because we replaced it with a GroupOfTasks
        //  having just EncodingProfileKey        
        
        string keyField = "EncodingProfileKey";
        int64_t encodingProfileKey = -1;
        string labelField = "EncodingProfileLabel";
        if (JSONUtils::isMetadataPresent(parametersRoot, keyField))
        {
            encodingProfileKey = JSONUtils::asInt64(parametersRoot, keyField, 0);

			// check if the profile is already present for the source content
			{
				try
				{
					bool warningIfMissing = true;
					int64_t localPhysicalPathKey = _mmsEngineDBFacade->getPhysicalPathDetails(
						sourceMediaItemKey, encodingProfileKey, warningIfMissing);

					string errorMessage = __FILEREF__ + "Content profile is already present"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", sourceMediaItemKey: " + to_string(sourceMediaItemKey)
						+ ", encodingProfileKey: " + to_string(encodingProfileKey)
						;
					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}
				catch(MediaItemKeyNotFound e)
				{
				}
			}
        }
        else if (JSONUtils::isMetadataPresent(parametersRoot, labelField))
        {
			string encodingProfileLabel = parametersRoot.get(labelField, "XXX").asString();

			encodingProfileKey = _mmsEngineDBFacade->getEncodingProfileKeyByLabel(
				workspace, referenceContentType, encodingProfileLabel);

			// check if the profile is already present for the source content
			{
				try
				{
					bool warningIfMissing = true;
					int64_t localPhysicalPathKey = _mmsEngineDBFacade->getPhysicalPathDetails(
						sourceMediaItemKey, encodingProfileKey, warningIfMissing);

					string errorMessage = __FILEREF__ + "Content profile is already present"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", sourceMediaItemKey: " + to_string(sourceMediaItemKey)
						+ ", encodingProfileKey: " + to_string(encodingProfileKey)
						;
					_logger->error(errorMessage);

					throw runtime_error(errorMessage);
				}
				catch(MediaItemKeyNotFound e)
				{
				}
			}
        }
        else
        {
            string errorMessage = __FILEREF__ + "Both fields are not present or it is null"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", Field: " + keyField
                    + ", Field: " + labelField
                    ;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

		_mmsEngineDBFacade->addEncodingJob (workspace, ingestionJobKey,
			encodingProfileKey, sourceMediaItemKey, sourcePhysicalPathKey,
			encodingPriority);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageEncodeTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageEncodeTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageVideoSpeedTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>&
			dependencies
)
{
    try
    {
        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong media number to be encoded"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::VideoSpeedType videoSpeedType;
        string field = "SpeedType";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            videoSpeedType = MMSEngineDBFacade::VideoSpeedType::SlowDown;
        }
        else
        {
            videoSpeedType = MMSEngineDBFacade::toVideoSpeedType(
					parametersRoot.get(field, "SlowDown").asString());
        }

		int videoSpeedSize = 3;
        field = "SpeedSize";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            videoSpeedSize = JSONUtils::asInt(parametersRoot, field, 3);
        }

        MMSEngineDBFacade::EncodingPriority encodingPriority;
        field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = static_cast<MMSEngineDBFacade::EncodingPriority>(
				workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority = MMSEngineDBFacade::toEncodingPriority(
					parametersRoot.get(field, "XXX").asString());
        }

		// Since it was a copy anc past, next commant has to be checked.
		// It is not possible to manage more than one encode because:
		// 1. inside _mmsEngineDBFacade->addEncodingJob, the ingestionJob is updated to encodingQueue
		//		and the second call will fail (because the update of the ingestion was already done
		//	2. The ingestionJob mantains the status of the encoding, how would be managed
		//		the status in case of more than one encoding?
        // for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
		// 		keyAndDependencyType: dependencies)
        MMSEngineDBFacade::ContentType referenceContentType;
		int64_t sourceMediaItemKey;
		int64_t sourcePhysicalPathKey;
        {
            int64_t key;
            Validator::DependencyType dependencyType;
            
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType	= dependencies[0];
            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

			if (dependencyType == Validator::DependencyType::MediaItemKey)
			{
				sourceMediaItemKey = key;

				sourcePhysicalPathKey = -1;
			}
			else
			{
				sourcePhysicalPathKey = key;
            
				bool warningIfMissing = false;
				tuple<int64_t,MMSEngineDBFacade::ContentType,string,string, string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
					_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
					workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);

				MMSEngineDBFacade::ContentType localContentType;
				string localTitle;
				string userData;
                string ingestionDate;
				int64_t localIngestionJobKey;
				tie(sourceMediaItemKey,localContentType, localTitle, userData, ingestionDate,
						localIngestionJobKey, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
			}
		}

		_mmsEngineDBFacade->addEncoding_VideoSpeed (workspace, ingestionJobKey,
			sourceMediaItemKey, sourcePhysicalPathKey,
			videoSpeedType, videoSpeedSize, encodingPriority);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageVideoSpeedTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageVideoSpeedTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::managePictureInPictureTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() != 2)
        {
            string errorMessage = __FILEREF__ + "Wrong number of dependencies"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::EncodingPriority encodingPriority;
        string field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = 
                    static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority =
                MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
        }

        bool secondVideoOverlayedOnFirst;
        field = "SecondVideoOverlayedOnFirst";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			secondVideoOverlayedOnFirst = true;
        }
		else
			secondVideoOverlayedOnFirst = JSONUtils::asBool(parametersRoot, field, false);

        bool soundOfFirstVideo;
        field = "SoundOfFirstVideo";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			soundOfFirstVideo = true;
        }
		else
			soundOfFirstVideo = JSONUtils::asBool(parametersRoot, field, false);

        string overlayPosition_X_InPixel;
        field = "OverlayPosition_X_InPixel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			overlayPosition_X_InPixel = "0";
        }
		else
			overlayPosition_X_InPixel = parametersRoot.get(field, "XXX").asString();

        string overlayPosition_Y_InPixel;
        field = "OverlayPosition_Y_InPixel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			overlayPosition_Y_InPixel = "0";
        }
		else
			overlayPosition_Y_InPixel = parametersRoot.get(field, "XXX").asString();

        string overlay_Width_InPixel;
        field = "Overlay_Width_InPixel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			overlay_Width_InPixel = "100";
        }
		else
			overlay_Width_InPixel = parametersRoot.get(field, "XXX").asString();

        string overlay_Height_InPixel;
        field = "Overlay_Height_InPixel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			overlay_Height_InPixel = "100";
        }
		else
			overlay_Height_InPixel = parametersRoot.get(field, "XXX").asString();

        int64_t sourceMediaItemKey_1;
        int64_t sourcePhysicalPathKey_1;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType_1 = dependencies[0];

        int64_t key_1;
        MMSEngineDBFacade::ContentType referenceContentType_1;
        Validator::DependencyType dependencyType_1;

        tie(key_1, referenceContentType_1, dependencyType_1) = keyAndDependencyType_1;

        if (dependencyType_1 == Validator::DependencyType::MediaItemKey)
        {
            sourceMediaItemKey_1 = key_1;

            sourcePhysicalPathKey_1 = -1;
        }
        else if (dependencyType_1 == Validator::DependencyType::PhysicalPathKey)
        {
            sourcePhysicalPathKey_1 = key_1;
            
            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string, string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey_1, warningIfMissing);

            tie(sourceMediaItemKey_1, ignore, ignore, ignore, ignore, ignore, ignore)
				= mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }
		else
        {
            string errorMessage = __FILEREF__ + "Wrong dependencyType"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", dependencyType_1: " + to_string(static_cast<int>(dependencyType_1));
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t sourceMediaItemKey_2;
        int64_t sourcePhysicalPathKey_2;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType_2 = dependencies[1];

        int64_t key_2;
        MMSEngineDBFacade::ContentType referenceContentType_2;
        Validator::DependencyType dependencyType_2;

        tie(key_2, referenceContentType_2, dependencyType_2) = keyAndDependencyType_2;

        if (dependencyType_2 == Validator::DependencyType::MediaItemKey)
        {
            sourceMediaItemKey_2 = key_2;

            sourcePhysicalPathKey_2 = -1;
        }
        else if (dependencyType_2 == Validator::DependencyType::PhysicalPathKey)
        {
            sourcePhysicalPathKey_2 = key_2;
            
            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string, string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey_2, warningIfMissing);

            tie(sourceMediaItemKey_2, ignore, ignore, ignore, ignore, ignore, ignore)
				= mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }
		else
        {
            string errorMessage = __FILEREF__ + "Wrong dependencyType"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", dependencyType_2: " + to_string(static_cast<int>(dependencyType_2));
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

		int64_t mainMediaItemKey;
		int64_t mainPhysicalPathKey;
		int64_t overlayMediaItemKey;
		int64_t overlayPhysicalPathKey;

		bool soundOfMain;
        if (secondVideoOverlayedOnFirst)
		{
			mainMediaItemKey			= sourceMediaItemKey_1;
			mainPhysicalPathKey			= sourcePhysicalPathKey_1;
			overlayMediaItemKey			= sourceMediaItemKey_2;
			overlayPhysicalPathKey		= sourcePhysicalPathKey_2;

			if (soundOfFirstVideo)
				soundOfMain = true;
			else
				soundOfMain = false;
		}
		else
		{
			mainMediaItemKey			= sourceMediaItemKey_2;
			mainPhysicalPathKey			= sourcePhysicalPathKey_2;
			overlayMediaItemKey			= sourceMediaItemKey_1;
			overlayPhysicalPathKey		= sourcePhysicalPathKey_1;

			if (soundOfFirstVideo)
				soundOfMain = false;
			else
				soundOfMain = true;
		}

		_mmsEngineDBFacade->addEncoding_PictureInPictureJob (workspace, ingestionJobKey,
			mainMediaItemKey, mainPhysicalPathKey,
			overlayMediaItemKey, overlayPhysicalPathKey,
			overlayPosition_X_InPixel, overlayPosition_Y_InPixel,
			overlay_Width_InPixel, overlay_Height_InPixel,
			soundOfMain, encodingPriority);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "managePictureInPictureTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "managePictureInPictureTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageOverlayImageOnVideoTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() != 2)
        {
            string errorMessage = __FILEREF__ + "Wrong number of dependencies"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::EncodingPriority encodingPriority;
        string field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = 
                    static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority =
                MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
        }

        string imagePosition_X_InPixel;
        field = "ImagePosition_X_InPixel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			/*
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
			*/
			imagePosition_X_InPixel = "0";
        }
		else
			imagePosition_X_InPixel = parametersRoot.get(field, "XXX").asString();

        string imagePosition_Y_InPixel;
        field = "ImagePosition_Y_InPixel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
			/*
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
			*/

			imagePosition_Y_InPixel = "0";
        }
		else
			imagePosition_Y_InPixel = parametersRoot.get(field, "XXX").asString();

        int64_t sourceMediaItemKey_1;
        int64_t sourcePhysicalPathKey_1;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType_1 = dependencies[0];

        int64_t key_1;
        MMSEngineDBFacade::ContentType referenceContentType_1;
        Validator::DependencyType dependencyType_1;

        tie(key_1, referenceContentType_1, dependencyType_1) = keyAndDependencyType_1;

        if (dependencyType_1 == Validator::DependencyType::MediaItemKey)
        {
            sourceMediaItemKey_1 = key_1;

            sourcePhysicalPathKey_1 = -1;
        }
        else if (dependencyType_1 == Validator::DependencyType::PhysicalPathKey)
        {
            sourcePhysicalPathKey_1 = key_1;
            
            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string, string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey_1, warningIfMissing);

            MMSEngineDBFacade::ContentType localContentType;
            string localTitle;
            string userData;
            string ingestionDate;
			int64_t localIngestionJobKey;
            tie(sourceMediaItemKey_1,localContentType, localTitle, userData, ingestionDate,
					localIngestionJobKey, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }
		else
        {
            string errorMessage = __FILEREF__ + "Wrong dependencyType"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", dependencyType_1: " + to_string(static_cast<int>(dependencyType_1));
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        int64_t sourceMediaItemKey_2;
        int64_t sourcePhysicalPathKey_2;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType_2 = dependencies[1];

        int64_t key_2;
        MMSEngineDBFacade::ContentType referenceContentType_2;
        Validator::DependencyType dependencyType_2;

        tie(key_2, referenceContentType_2, dependencyType_2) = keyAndDependencyType_2;

        if (dependencyType_2 == Validator::DependencyType::MediaItemKey)
        {
            sourceMediaItemKey_2 = key_2;

            sourcePhysicalPathKey_2 = -1;
        }
        else if (dependencyType_2 == Validator::DependencyType::PhysicalPathKey)
        {
            sourcePhysicalPathKey_2 = key_2;
            
            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey_2, warningIfMissing);

            MMSEngineDBFacade::ContentType localContentType;
            string localTitle;
            string userData;
            string ingestionDate;
			int64_t localIngestionJobKey;
            tie(sourceMediaItemKey_2,localContentType, localTitle, userData, ingestionDate,
					localIngestionJobKey, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }
		else
        {
            string errorMessage = __FILEREF__ + "Wrong dependencyType"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", dependencyType_2: " + to_string(static_cast<int>(dependencyType_2));
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        _mmsEngineDBFacade->addEncoding_OverlayImageOnVideoJob (workspace, ingestionJobKey,
                sourceMediaItemKey_1, sourcePhysicalPathKey_1,
                sourceMediaItemKey_2, sourcePhysicalPathKey_2,
                imagePosition_X_InPixel, imagePosition_Y_InPixel,
                encodingPriority);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageOverlayImageOnVideoTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageOverlayImageOnVideoTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageOverlayTextOnVideoTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {

        if (dependencies.size() != 1)
        {
            string errorMessage = __FILEREF__ + "Wrong number of dependencies"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        MMSEngineDBFacade::EncodingPriority encodingPriority;
        string field = "EncodingPriority";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            encodingPriority = 
                    static_cast<MMSEngineDBFacade::EncodingPriority>(workspace->_maxEncodingPriority);
        }
        else
        {
            encodingPriority =
                MMSEngineDBFacade::toEncodingPriority(parametersRoot.get(field, "XXX").asString());
        }

        field = "Text";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        string text = parametersRoot.get(field, "XXX").asString();

        string textPosition_X_InPixel;
        field = "TextPosition_X_InPixel";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            textPosition_X_InPixel = parametersRoot.get(field, "XXX").asString();
        }

        string textPosition_Y_InPixel;
        field = "TextPosition_Y_InPixel";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            textPosition_Y_InPixel = parametersRoot.get(field, "XXX").asString();
        }

        string fontType;
        field = "FontType";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            fontType = parametersRoot.get(field, "XXX").asString();
        }

        int fontSize = -1;
        field = "FontSize";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            fontSize = JSONUtils::asInt(parametersRoot, field, -1);
        }

        string fontColor;
        field = "FontColor";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            fontColor = parametersRoot.get(field, "XXX").asString();
        }

        int textPercentageOpacity = -1;
        field = "TextPercentageOpacity";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            textPercentageOpacity = JSONUtils::asInt64(parametersRoot, field, -1);
        }

        bool boxEnable = false;
        field = "BoxEnable";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            boxEnable = JSONUtils::asBool(parametersRoot, field, false);
        }

        string boxColor;
        field = "BoxColor";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            boxColor = parametersRoot.get(field, "XXX").asString();
        }

        int boxPercentageOpacity = -1;
        field = "BoxPercentageOpacity";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            boxPercentageOpacity = JSONUtils::asInt64(parametersRoot, field, -1);
        }

        int64_t sourceMediaItemKey;
        int64_t sourcePhysicalPathKey;
        tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType = dependencies[0];

        int64_t key;
        MMSEngineDBFacade::ContentType referenceContentType;
        Validator::DependencyType dependencyType;

        tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

        if (dependencyType == Validator::DependencyType::MediaItemKey)
        {
            sourceMediaItemKey = key;

            sourcePhysicalPathKey = -1;
        }
        else
        {
            sourcePhysicalPathKey = key;
            
            bool warningIfMissing = false;
            tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
				mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    workspace->_workspaceKey, sourcePhysicalPathKey, warningIfMissing);

            tie(sourceMediaItemKey, ignore, ignore, ignore, ignore, ignore, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        }

		_logger->info(__FILEREF__ + "addEncoding_OverlayTextOnVideoJob"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", encodingPriority: " + MMSEngineDBFacade::toString(encodingPriority)
				+ ", sourceMediaItemKey: " + to_string(sourceMediaItemKey)
				+ ", sourcePhysicalPathKey: " + to_string(sourcePhysicalPathKey)
				+ ", text: " + text
				+ ", textPosition_X_InPixel: " + textPosition_X_InPixel
				+ ", textPosition_Y_InPixel: " + textPosition_Y_InPixel
				+ ", fontType: " + fontType
				+ ", fontSize: " + to_string(fontSize)
				+ ", fontColor: " + fontColor
				+ ", textPercentageOpacity: " + to_string(textPercentageOpacity)
				+ ", boxEnable: " + to_string(boxEnable)
				+ ", boxColor: " + boxColor
				+ ", boxPercentageOpacity: " + to_string(boxPercentageOpacity)
			);
        _mmsEngineDBFacade->addEncoding_OverlayTextOnVideoJob (
                workspace, ingestionJobKey, encodingPriority,
                
                sourceMediaItemKey, sourcePhysicalPathKey,
                text,
                textPosition_X_InPixel, textPosition_Y_InPixel,
                fontType, fontSize, fontColor, textPercentageOpacity,
                boxEnable, boxColor, boxPercentageOpacity
                );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageOverlayTextOnVideoTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageOverlayTextOnVideoTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method

        throw e;
    }
}

void MMSEngineProcessor::manageEmailNotificationTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
		/*
        if (dependencies.size() == 0)
        {
            string errorMessage = __FILEREF__ + "No configured any IngestionJobKey in order to send an email"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
		*/
        
        string sIngestionJobKeyDependency;
        for (tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType: dependencies)
        {
            int64_t key;
            MMSEngineDBFacade::ContentType referenceContentType;
            Validator::DependencyType dependencyType;

            tie(key, referenceContentType, dependencyType) = keyAndDependencyType;
        
            if (sIngestionJobKeyDependency == "")
                sIngestionJobKeyDependency = to_string(key);
            else
                sIngestionJobKeyDependency += (", " + to_string(key));
        }
        
		string firstTitle;
		int64_t mediaItemKey;
		if (dependencies.size() > 0)
		{
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>& keyAndDependencyType
				= dependencies[0];

        	int64_t key;
        	MMSEngineDBFacade::ContentType referenceContentType;
        	Validator::DependencyType dependencyType;

        	tie(key, referenceContentType, dependencyType) = keyAndDependencyType;

        	if (dependencyType == Validator::DependencyType::MediaItemKey)
        	{
        		bool warningIfMissing = false;

				mediaItemKey = key;

        		tuple<MMSEngineDBFacade::ContentType,string,string,string, int64_t, int64_t>
					contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey
                	= _mmsEngineDBFacade->getMediaItemKeyDetails(
					workspace->_workspaceKey, key, warningIfMissing);
        
        		MMSEngineDBFacade::ContentType contentType;
        		string userData;
                string ingestionDate;
				int64_t localIngestionJobKey;
        		tie(contentType, firstTitle, userData, ingestionDate, ignore, localIngestionJobKey)
					= contentTypeTitleUserDataIngestionDateRemovedInAndIngestionJobKey;
        	}
        	else
        	{
            	bool warningIfMissing = false;
            	tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                	_mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                    	workspace->_workspaceKey, key, warningIfMissing);

            	MMSEngineDBFacade::ContentType localContentType;
            	string userData;
                string ingestionDate;
				int64_t localIngestionJobKey;
            	tie(mediaItemKey,localContentType, firstTitle, userData, ingestionDate,
						localIngestionJobKey, ignore)
                    = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
        	}
		}

		int64_t referenceIngestionJobKey = 0;
		string referenceLabel;
		string referenceErrorMessage;
		// if (dependencies.size() == 0)
		{
			// since we do not have dependency, that means we had an error
			// and here we will get the error message
			// Parameters will be something like:
			// { "ConfigurationLabel" : "Error", "References" : [ { "ReferenceIngestionJobKey" : 203471 } ] }
			// We will retrieve the error associated to ReferenceIngestionJobKey

			string field = "References";
			if (JSONUtils::isMetadataPresent(parametersRoot, field))
			{
				Json::Value referencesRoot = parametersRoot[field];
				for (int referenceIndex = 0; referenceIndex < referencesRoot.size(); referenceIndex++)
				{
					Json::Value referenceRoot = referencesRoot[referenceIndex];
					field = "ReferenceIngestionJobKey";
					if (JSONUtils::isMetadataPresent(referenceRoot, field))
					{
						MMSEngineDBFacade::IngestionType ingestionType;

						referenceIngestionJobKey = JSONUtils::asInt64(referenceRoot, field, 0);

						tuple<string, MMSEngineDBFacade::IngestionType, MMSEngineDBFacade::IngestionStatus,
							string, string> labelIngestionTypeAndErrorMessage =
							_mmsEngineDBFacade->getIngestionJobDetails(referenceIngestionJobKey);
						tie(referenceLabel, ingestionType, ignore, ignore, referenceErrorMessage);

						break;
					}
				}
			}
		}

        string field = "ConfigurationLabel";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        string configurationLabel = parametersRoot.get(field, "XXX").asString();

		string emailAddresses;
		string subject;
		string message;
        tuple<string, string, string> email = _mmsEngineDBFacade->getEMailByConfigurationLabel(
                workspace->_workspaceKey, configurationLabel);            
        tie(emailAddresses, subject, message) = email;

        {
            string strToBeReplaced = "{IngestionJobKey}";
            string strToReplace = to_string(ingestionJobKey);
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }
        {
            string strToBeReplaced = "{IngestionJobKeyDependency}";
            string strToReplace = sIngestionJobKeyDependency;
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }
        {
            string strToBeReplaced = "{FirstTitle}";
            string strToReplace = firstTitle;
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }
        {
            string strToBeReplaced = "{MediaItemKey}";
            string strToReplace = to_string(mediaItemKey);
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }
        {
            string strToBeReplaced = "{ReferenceIngestionJobKey}";
            string strToReplace = to_string(referenceIngestionJobKey);
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }
        {
            string strToBeReplaced = "{ReferenceLabel}";
            string strToReplace = referenceLabel;
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }
        {
            string strToBeReplaced = "{ReferenceErrorMessage}";
            string strToReplace = referenceErrorMessage;
            if (subject.find(strToBeReplaced) != string::npos)
                subject.replace(subject.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
            if (message.find(strToBeReplaced) != string::npos)
                message.replace(message.find(strToBeReplaced), strToBeReplaced.length(), strToReplace);
        }

        vector<string> emailBody;
        emailBody.push_back(message);

        EMailSender emailSender(_logger, _configuration);
        emailSender.sendEmail(emailAddresses, subject, emailBody);

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_TaskSuccess"
            + ", errorMessage: " + ""
        );
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
        );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "sendEmail failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "sendEmail failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method
        
        throw e;
    }
}

void MMSEngineProcessor::manageMediaCrossReferenceTask(
        int64_t ingestionJobKey,
        shared_ptr<Workspace> workspace,
        Json::Value parametersRoot,
        vector<tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>>& dependencies
)
{
    try
    {
        if (dependencies.size() != 2)
        {
            string errorMessage = __FILEREF__ + "No configured Two Media in order to create the Cross Reference"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size());
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        string field = "Type";
        if (!JSONUtils::isMetadataPresent(parametersRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        MMSEngineDBFacade::CrossReferenceType crossReferenceType =
			MMSEngineDBFacade::toCrossReferenceType(parametersRoot.get(field, "").asString());
		if (crossReferenceType == MMSEngineDBFacade::CrossReferenceType::VideoOfImage)
			crossReferenceType == MMSEngineDBFacade::CrossReferenceType::ImageOfVideo;
		else if (crossReferenceType == MMSEngineDBFacade::CrossReferenceType::AudioOfImage)
			crossReferenceType == MMSEngineDBFacade::CrossReferenceType::ImageOfAudio;

        MMSEngineDBFacade::ContentType firstContentType;
		int64_t firstMediaItemKey;
        {
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType = dependencies[0];

            int64_t key;
            Validator::DependencyType dependencyType;

            tie(key, firstContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                firstMediaItemKey = key;
            }
            else
            {
                int64_t physicalPathKey = key;

                bool warningIfMissing = false;
                tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                    _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                        workspace->_workspaceKey, physicalPathKey, warningIfMissing);

                tie(firstMediaItemKey,ignore, ignore, ignore, ignore, ignore, ignore)
                        = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
            }
		}

        MMSEngineDBFacade::ContentType secondContentType;
		int64_t secondMediaItemKey;
        {
			tuple<int64_t,MMSEngineDBFacade::ContentType,Validator::DependencyType>&
				keyAndDependencyType = dependencies[1];

            int64_t key;
            Validator::DependencyType dependencyType;

            tie(key, secondContentType, dependencyType) = keyAndDependencyType;

            if (dependencyType == Validator::DependencyType::MediaItemKey)
            {
                secondMediaItemKey = key;
            }
            else
            {
                int64_t physicalPathKey = key;

                bool warningIfMissing = false;
                tuple<int64_t,MMSEngineDBFacade::ContentType,string,string,string,int64_t, string>
					mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName =
                    _mmsEngineDBFacade->getMediaItemKeyDetailsByPhysicalPathKey(
                        workspace->_workspaceKey, physicalPathKey, warningIfMissing);

                tie(secondMediaItemKey,ignore, ignore, ignore, ignore, ignore, ignore)
                        = mediaItemKeyContentTypeTitleUserDataIngestionDateIngestionJobKeyAndFileName;
            }
		}

		if (crossReferenceType == MMSEngineDBFacade::CrossReferenceType::ImageOfVideo
				|| crossReferenceType == MMSEngineDBFacade::CrossReferenceType::FaceOfVideo
				)
		{
			Json::Value crossReferenceParametersRoot;

			if (firstContentType == MMSEngineDBFacade::ContentType::Video
				&& secondContentType == MMSEngineDBFacade::ContentType::Image)
			{
				_logger->info(__FILEREF__ + "Add Cross Reference"
					+ ", sourceMediaItemKey: " + to_string(secondMediaItemKey)
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
					+ ", targetMediaItemKey: " + to_string(firstMediaItemKey)
				);
				_mmsEngineDBFacade->addCrossReference(
                        secondMediaItemKey, crossReferenceType, firstMediaItemKey,
						crossReferenceParametersRoot);
			}
			else if (firstContentType == MMSEngineDBFacade::ContentType::Image
				&& secondContentType == MMSEngineDBFacade::ContentType::Video)
			{
				_logger->info(__FILEREF__ + "Add Cross Reference"
					+ ", sourceMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
					+ ", targetMediaItemKey: " + to_string(secondMediaItemKey)
				);
				_mmsEngineDBFacade->addCrossReference(
					firstMediaItemKey, crossReferenceType, secondMediaItemKey,
					crossReferenceParametersRoot);
			}
			else
			{
				string errorMessage = __FILEREF__ + "Wrong content type"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size())
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                    + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                    + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                    + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
		}
		else if (crossReferenceType == MMSEngineDBFacade::CrossReferenceType::ImageOfAudio)
		{
			Json::Value crossReferenceParametersRoot;

			if (firstContentType == MMSEngineDBFacade::ContentType::Audio
				&& secondContentType == MMSEngineDBFacade::ContentType::Image)
			{
				_logger->info(__FILEREF__ + "Add Cross Reference"
					+ ", sourceMediaItemKey: " + to_string(secondMediaItemKey)
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
					+ ", targetMediaItemKey: " + to_string(firstMediaItemKey)
				);
				_mmsEngineDBFacade->addCrossReference(
                        secondMediaItemKey, crossReferenceType, firstMediaItemKey,
						crossReferenceParametersRoot);
			}
			else if (firstContentType == MMSEngineDBFacade::ContentType::Image
				&& secondContentType == MMSEngineDBFacade::ContentType::Audio)
			{
				_logger->info(__FILEREF__ + "Add Cross Reference"
					+ ", sourceMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
					+ ", targetMediaItemKey: " + to_string(secondMediaItemKey)
				);
				_mmsEngineDBFacade->addCrossReference(
					firstMediaItemKey, crossReferenceType, secondMediaItemKey,
					crossReferenceParametersRoot);
			}
			else
			{
				string errorMessage = __FILEREF__ + "Wrong content type"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size())
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                    + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                    + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                    + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
		}
		else if (crossReferenceType == MMSEngineDBFacade::CrossReferenceType::CutOfVideo)
		{
			if (firstContentType != MMSEngineDBFacade::ContentType::Video
				|| secondContentType != MMSEngineDBFacade::ContentType::Video)
			{
				string errorMessage = __FILEREF__ + "Wrong content type"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size())
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                    + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                    + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                    + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			field = "Parameters";
			if (!JSONUtils::isMetadataPresent(parametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Cross Reference Parameters are not present"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size())
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                    + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                    + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                    + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			Json::Value crossReferenceParametersRoot = parametersRoot[field];

			_mmsEngineDBFacade->addCrossReference(
				firstMediaItemKey, crossReferenceType, secondMediaItemKey, crossReferenceParametersRoot);
		}
		else if (crossReferenceType == MMSEngineDBFacade::CrossReferenceType::CutOfAudio)
		{
			if (firstContentType != MMSEngineDBFacade::ContentType::Audio
				|| secondContentType != MMSEngineDBFacade::ContentType::Audio)
			{
				string errorMessage = __FILEREF__ + "Wrong content type"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size())
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                    + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                    + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                    + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			field = "Parameters";
			if (!JSONUtils::isMetadataPresent(parametersRoot, field))
			{
				string errorMessage = __FILEREF__ + "Cross Reference Parameters are not present"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", dependencies.size: " + to_string(dependencies.size())
                    + ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                    + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                    + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                    + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                    + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			Json::Value crossReferenceParametersRoot = parametersRoot[field];

			_mmsEngineDBFacade->addCrossReference(
				firstMediaItemKey, crossReferenceType, secondMediaItemKey, crossReferenceParametersRoot);
		}
		else
		{
			string errorMessage = __FILEREF__ + "Wrong type"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", dependencies.size: " + to_string(dependencies.size())
				+ ", crossReferenceType: " + MMSEngineDBFacade::toString(crossReferenceType)
                + ", firstContentType: " + MMSEngineDBFacade::toString(firstContentType)
                + ", secondContentType: " + MMSEngineDBFacade::toString(secondContentType)
                + ", firstMediaItemKey: " + to_string(firstMediaItemKey)
                + ", secondMediaItemKey: " + to_string(secondMediaItemKey)
			;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_TaskSuccess"
            + ", errorMessage: " + ""
        );                            
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
        );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "manageMediaCrossReferenceTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", e.what(): " + e.what()
        );
        
        // Update IngestionJob done in the calling method
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "manageMediaCrossReferenceTask failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
        );
        
        // Update IngestionJob done in the calling method
        
        throw e;
    }
}

string MMSEngineProcessor::generateMediaMetadataToIngest(
        int64_t ingestionJobKey,
        string fileFormat,
        string title,
		int64_t imageOfVideoMediaItemKey,
		int64_t cutOfVideoMediaItemKey, int64_t cutOfAudioMediaItemKey, double startTimeInSeconds, double endTimeInSeconds,
        Json::Value parametersRoot
)
{
    string field = "FileFormat";
    if (JSONUtils::isMetadataPresent(parametersRoot, field))
    {
        string fileFormatSpecifiedByUser = parametersRoot.get(field, "XXX").asString();
        if (fileFormatSpecifiedByUser != fileFormat)
        {
            string errorMessage = string("Wrong fileFormat")
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", fileFormatSpecifiedByUser: " + fileFormatSpecifiedByUser
                + ", fileFormat: " + fileFormat
            ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }
    }
    else
    {
        parametersRoot[field] = fileFormat;
    }
    
	if (imageOfVideoMediaItemKey != -1)
	{
		MMSEngineDBFacade::CrossReferenceType   crossReferenceType =
			MMSEngineDBFacade::CrossReferenceType::ImageOfVideo;

        Json::Value crossReferenceRoot;

		field = "Type";
		crossReferenceRoot[field] =
			MMSEngineDBFacade::toString(crossReferenceType);

		field = "MediaItemKey";
		crossReferenceRoot[field] = imageOfVideoMediaItemKey;

		field = "CrossReference";
		parametersRoot[field] = crossReferenceRoot;
	}
	else if (cutOfVideoMediaItemKey != -1)
	{
		MMSEngineDBFacade::CrossReferenceType   crossReferenceType =
			MMSEngineDBFacade::CrossReferenceType::CutOfVideo;

        Json::Value crossReferenceRoot;

		field = "Type";
		crossReferenceRoot[field] =
			MMSEngineDBFacade::toString(crossReferenceType);

		field = "MediaItemKey";
		crossReferenceRoot[field] = cutOfVideoMediaItemKey;

        Json::Value crossReferenceParametersRoot;
		{
			field = "StartTimeInSeconds";
			crossReferenceParametersRoot[field] = startTimeInSeconds;

			field = "EndTimeInSeconds";
			crossReferenceParametersRoot[field] = endTimeInSeconds;

			field = "Parameters";
			crossReferenceRoot[field] = crossReferenceParametersRoot;
		}

		field = "CrossReference";
		parametersRoot[field] = crossReferenceRoot;
	}
	else if (cutOfAudioMediaItemKey != -1)
	{
		MMSEngineDBFacade::CrossReferenceType   crossReferenceType =
			MMSEngineDBFacade::CrossReferenceType::CutOfAudio;

        Json::Value crossReferenceRoot;

		field = "Type";
		crossReferenceRoot[field] =
			MMSEngineDBFacade::toString(crossReferenceType);

		field = "MediaItemKey";
		crossReferenceRoot[field] = cutOfAudioMediaItemKey;

        Json::Value crossReferenceParametersRoot;
		{
			field = "StartTimeInSeconds";
			crossReferenceParametersRoot[field] = startTimeInSeconds;

			field = "EndTimeInSeconds";
			crossReferenceParametersRoot[field] = endTimeInSeconds;

			field = "Parameters";
			crossReferenceRoot[field] = crossReferenceParametersRoot;
		}

		field = "CrossReference";
		parametersRoot[field] = crossReferenceRoot;
	}

    field = "Title";
    if (title != "")
        parametersRoot[field] = title;

    // this scenario is for example for the Cut or Concat-Demux or Periodical-Frames
    // that generate a new content (or contents in case of Periodical-Frames)
    // and the Parameters json will contain the parameters
    // for the new content.
    // It will contain also parameters for the Cut or Concat-Demux or Periodical-Frames or ...,
    // we will leave there even because we know they will not be used by the
    // Add-Content task
    
    string mediaMetadata;
    {
        Json::StreamWriterBuilder wbuilder;
        mediaMetadata = Json::writeString(wbuilder, parametersRoot);
    }
                        
    _logger->info(__FILEREF__ + "Media metadata generated"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
        + ", ingestionJobKey: " + to_string(ingestionJobKey)
        + ", mediaMetadata: " + mediaMetadata
            );

    return mediaMetadata;
}

void MMSEngineProcessor::handleCheckEncodingEvent ()
{
	try
	{
		_logger->info(__FILEREF__ + "Received handleCheckEncodingEvent"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
		);

		vector<shared_ptr<MMSEngineDBFacade::EncodingItem>> encodingItems;
        
		_mmsEngineDBFacade->getEncodingJobs(_processorMMS, encodingItems,
				_maxEncodingJobsPerEvent);

		_logger->info(__FILEREF__ + "_pActiveEncodingsManager->addEncodingItems"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", encodingItems.size: " + to_string(encodingItems.size())
		);

		_pActiveEncodingsManager->addEncodingItems(encodingItems);

		_logger->info(__FILEREF__ + "getEncodingJobs result"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", encodingItems.size: " + to_string(encodingItems.size())
		);
	}
	catch(AlreadyLocked e)
	{
		_logger->warn(__FILEREF__ + "getEncodingJobs was not done"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", exception: " + e.what()
		);

		return;
		// throw e;
	}
	catch(runtime_error e)
	{
		_logger->error(__FILEREF__ + "getEncodingJobs failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", exception: " + e.what()
		);

		throw e;
	}
	catch(exception e)
	{
		_logger->error(__FILEREF__ + "getEncodingJobs failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", exception: " + e.what()
		);

		throw e;
	}
}

void MMSEngineProcessor::handleContentRetentionEventThread (
        shared_ptr<long> processorsThreadsNumber)
{

	chrono::system_clock::time_point start = chrono::system_clock::now();

    {
        _logger->info(__FILEREF__ + "Content Retention started"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
        );

        vector<pair<shared_ptr<Workspace>,int64_t>> mediaItemKeyToBeRemoved;
        bool moreRemoveToBeDone = true;

        while (moreRemoveToBeDone)
        {
            try
            {
                int maxMediaItemKeysNumber = 100;

                mediaItemKeyToBeRemoved.clear();
                _mmsEngineDBFacade->getExpiredMediaItemKeysCheckingDependencies(
                    _processorMMS, mediaItemKeyToBeRemoved, maxMediaItemKeysNumber);

                if (mediaItemKeyToBeRemoved.size() == 0)
                    moreRemoveToBeDone = false;
            }
            catch(runtime_error e)
            {
                _logger->error(__FILEREF__ + "getExpiredMediaItemKeysCheckingDependencies failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                );

                // no throw since it is running in a detached thread
                // throw e;
                break;
            }
            catch(exception e)
            {
                _logger->error(__FILEREF__ + "getExpiredMediaItemKeysCheckingDependencies failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", exception: " + e.what()
                );

                // no throw since it is running in a detached thread
                // throw e;
                break;
            }

            for (pair<shared_ptr<Workspace>,int64_t> workspaceAndMediaItemKey: mediaItemKeyToBeRemoved)
            {
                _logger->info(__FILEREF__ + "Removing because of ContentRetention"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", workspace->_workspaceKey: "
						+ to_string(workspaceAndMediaItemKey.first->_workspaceKey)
                    + ", workspace->_name: " + workspaceAndMediaItemKey.first->_name
                    + ", mediaItemKeyToBeRemoved: " + to_string(workspaceAndMediaItemKey.second)
                );

                try
                {
                    _mmsStorage->removeMediaItem(_mmsEngineDBFacade, workspaceAndMediaItemKey.second);
                }
                catch(runtime_error e)
                {
                    _logger->error(__FILEREF__ + "_mmsStorage->removeMediaItem failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", workspace->_workspaceKey: "
							+ to_string(workspaceAndMediaItemKey.first->_workspaceKey)
                        + ", workspace->_name: " + workspaceAndMediaItemKey.first->_name
                        + ", mediaItemKeyToBeRemoved: " + to_string(workspaceAndMediaItemKey.second)
                        + ", exception: " + e.what()
                    );

					try
					{
						string processorMMSForRetention = "";
						_mmsEngineDBFacade->updateMediaItem(workspaceAndMediaItemKey.second,
							processorMMSForRetention);
					}
					catch(runtime_error e)
					{
						_logger->error(__FILEREF__ + "updateMediaItem failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", mediaItemKey: " + to_string(workspaceAndMediaItemKey.second)
							+ ", exception: " + e.what()
						);
					}
					catch(exception e)
					{
						_logger->error(__FILEREF__ + "updateMediaItem failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", mediaItemKey: " + to_string(workspaceAndMediaItemKey.second)
							+ ", exception: " + e.what()
						);
					}

					// one remove failed, procedure has to go ahead to try all the other removes
                    // moreRemoveToBeDone = false;
                    // break;

					continue;
                    // no throw since it is running in a detached thread
                    // throw e;
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "_mmsStorage->removeMediaItem failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", workspace->_workspaceKey: "
							+ to_string(workspaceAndMediaItemKey.first->_workspaceKey)
                        + ", workspace->_name: " + workspaceAndMediaItemKey.first->_name
                        + ", mediaItemKeyToBeRemoved: " + to_string(workspaceAndMediaItemKey.second)
                    );

					try
					{
						string processorMMSForRetention = "";
						_mmsEngineDBFacade->updateMediaItem(workspaceAndMediaItemKey.second,
							processorMMSForRetention);
					}
					catch(runtime_error e)
					{
						_logger->error(__FILEREF__ + "updateMediaItem failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", mediaItemKey: " + to_string(workspaceAndMediaItemKey.second)
							+ ", exception: " + e.what()
						);
					}
					catch(exception e)
					{
						_logger->error(__FILEREF__ + "updateMediaItem failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", mediaItemKey: " + to_string(workspaceAndMediaItemKey.second)
							+ ", exception: " + e.what()
						);
					}

					// one remove failed, procedure has to go ahead to try all the other removes
                    // moreRemoveToBeDone = false;
                    // break;

					continue;
                    // no throw since it is running in a detached thread
                    // throw e;
                }
            }
        }

		chrono::system_clock::time_point end = chrono::system_clock::now();
		_logger->info(__FILEREF__ + "Content retention finished"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", duration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(end - start).count())
		);
    }

	/* Already done by the crontab script
    {
        _logger->info(__FILEREF__ + "Staging Retention started"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", _mmsStorage->getStagingRootRepository(): " + _mmsStorage->getStagingRootRepository()
        );

        try
        {
            chrono::system_clock::time_point tpNow = chrono::system_clock::now();
    
            FileIO::DirectoryEntryType_t detDirectoryEntryType;
            shared_ptr<FileIO::Directory> directory = FileIO::openDirectory (_mmsStorage->getStagingRootRepository());

            bool scanDirectoryFinished = false;
            while (!scanDirectoryFinished)
            {
                string directoryEntry;
                try
                {
                    string directoryEntry = FileIO::readDirectory (directory,
                        &detDirectoryEntryType);

//                    if (detDirectoryEntryType != FileIO::TOOLS_FILEIO_REGULARFILE)
//                        continue;

                    string pathName = _mmsStorage->getStagingRootRepository()
                            + directoryEntry;
                    chrono::system_clock::time_point tpLastModification =
                            FileIO:: getFileTime (pathName);
                    
                    int elapsedInHours = chrono::duration_cast<chrono::hours>(tpNow - tpLastModification).count();
                    double elapsedInDays =  elapsedInHours / 24;
                    if (elapsedInDays >= _stagingRetentionInDays)
                    {
                        if (detDirectoryEntryType == FileIO:: TOOLS_FILEIO_DIRECTORY) 
                        {
                            _logger->info(__FILEREF__ + "Removing staging directory because of Retention"
                                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", pathName: " + pathName
                                + ", elapsedInDays: " + to_string(elapsedInDays)
                                + ", _stagingRetentionInDays: " + to_string(_stagingRetentionInDays)
                            );
                            
                            try
                            {
                                bool removeRecursively = true;

                                FileIO::removeDirectory(pathName, removeRecursively);
                            }
                            catch(runtime_error e)
                            {
                                _logger->warn(__FILEREF__ + "Error removing staging directory because of Retention"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", pathName: " + pathName
                                    + ", elapsedInDays: " + to_string(elapsedInDays)
                                    + ", _stagingRetentionInDays: " + to_string(_stagingRetentionInDays)
                                    + ", e.what(): " + e.what()
                                );
                            }
                            catch(exception e)
                            {
                                _logger->warn(__FILEREF__ + "Error removing staging directory because of Retention"
                                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                    + ", pathName: " + pathName
                                    + ", elapsedInDays: " + to_string(elapsedInDays)
                                    + ", _stagingRetentionInDays: " + to_string(_stagingRetentionInDays)
                                    + ", e.what(): " + e.what()
                                );
                            }
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Removing staging file because of Retention"
                                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", pathName: " + pathName
                                + ", elapsedInDays: " + to_string(elapsedInDays)
                                + ", _stagingRetentionInDays: " + to_string(_stagingRetentionInDays)
                            );
                            
                            bool exceptionInCaseOfError = false;

                            FileIO::remove(pathName, exceptionInCaseOfError);
                        }
                    }
                }
                catch(DirectoryListFinished e)
                {
                    scanDirectoryFinished = true;
                }
                catch(runtime_error e)
                {
                    string errorMessage = __FILEREF__ + "listing directory failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                           + ", e.what(): " + e.what()
                    ;
                    _logger->error(errorMessage);

                    throw e;
                }
                catch(exception e)
                {
                    string errorMessage = __FILEREF__ + "listing directory failed"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
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
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", e.what(): " + e.what()
            );
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "removeHavingPrefixFileName failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            );
        }

        _logger->info(__FILEREF__ + "Staging Retention finished"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
        );
    }
	*/

}

void MMSEngineProcessor::handleDBDataRetentionEventThread()
{

    {
		chrono::system_clock::time_point start = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "Ingestion Data Retention started"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
        );

		try
		{
			_mmsEngineDBFacade->retentionOfIngestionData();
		}
		catch(runtime_error e)
		{
			_logger->error(__FILEREF__ + "retentionOfIngestionData failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}
		catch(exception e)
		{
			_logger->error(__FILEREF__ + "retentionOfIngestionData failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}

		chrono::system_clock::time_point end = chrono::system_clock::now();
		_logger->info(__FILEREF__ + "Ingestion Data retention finished"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", duration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(end - start).count())
		);
    }

    {
		chrono::system_clock::time_point start = chrono::system_clock::now();

        _logger->info(__FILEREF__ + "Delivery Autorization Retention started"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
        );

		try
		{
			_mmsEngineDBFacade->retentionOfDeliveryAuthorization();
		}
		catch(runtime_error e)
		{
			_logger->error(__FILEREF__ + "retentionOfDeliveryAuthorization failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}
		catch(exception e)
		{
			_logger->error(__FILEREF__ + "retentionOfDeliveryAuthorization failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}

		chrono::system_clock::time_point end = chrono::system_clock::now();
		_logger->info(__FILEREF__ + "Delivery Authorization retention finished"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", duration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(end - start).count())
		);
    }
}

void MMSEngineProcessor::handleCheckRefreshPartitionFreeSizeEventThread ()
{
	chrono::system_clock::time_point start = chrono::system_clock::now();

    {
        _logger->info(__FILEREF__ + "Check Refresh Partition Free Size started"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
        );

		try
		{
			_mmsStorage->refreshPartitionsFreeSizes();
		}
		catch(runtime_error e)
		{
			_logger->error(__FILEREF__ + "refreshPartitionsFreeSizes failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}
		catch(exception e)
		{
			_logger->error(__FILEREF__ + "refreshPartitionsFreeSizes failed"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}

		chrono::system_clock::time_point end = chrono::system_clock::now();
        _logger->info(__FILEREF__ + "Check Refresh Partition Free Size finished"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", duration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(end - start).count())
		);
    }
}

void MMSEngineProcessor::handleMainAndBackupOfRunnungLiveRecordingHA (
        shared_ptr<long> processorsThreadsNumber)
{
    
	chrono::system_clock::time_point start = chrono::system_clock::now();

    {
        _logger->info(__FILEREF__ + "Received MainAndBackupOfRunnungLiveRecordingHA event"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
        );

		try
		{
			_mmsEngineDBFacade->manageMainAndBackupOfRunnungLiveRecordingHA(_processorMMS);
		}
        catch(AlreadyLocked e)
        {
            _logger->warn(__FILEREF__ + "manageMainAndBackupOfRunnungLiveRecordingHA failed"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", exception: " + e.what()
            );

			// no throw since it is running in a detached thread
            // throw e;
        }
		catch(runtime_error e)
		{
			_logger->error(__FILEREF__ + "manageMainAndBackupOfRunnungLiveRecordingHA failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}
		catch(exception e)
		{
			_logger->error(__FILEREF__ + "manageMainAndBackupOfRunnungLiveRecordingHA failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}

		chrono::system_clock::time_point end = chrono::system_clock::now();
        _logger->info(__FILEREF__ + "Live Recording HA finished"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", duration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(end - start).count())
        );
    }
}

tuple<MMSEngineDBFacade::IngestionStatus, string, string, string, int, bool>
	MMSEngineProcessor::getMediaSourceDetails(
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace,
		MMSEngineDBFacade::IngestionType ingestionType, Json::Value parametersRoot)        
{
	// only in case of externalReadOnlyStorage, nextIngestionStatus does not change and we do not need it
	// So I set it just to a state
    MMSEngineDBFacade::IngestionStatus nextIngestionStatus = MMSEngineDBFacade::IngestionStatus::Start_TaskQueued;
    string mediaSourceURL;
    string mediaFileFormat;
	bool externalReadOnlyStorage;
    
    string field;
    if (ingestionType != MMSEngineDBFacade::IngestionType::AddContent)
    {
        string errorMessage = __FILEREF__ + "ingestionType is wrong"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", ingestionType: " + MMSEngineDBFacade::toString(ingestionType);
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

	externalReadOnlyStorage = false;
    {
        field = "SourceURL";
        if (JSONUtils::isMetadataPresent(parametersRoot, field))
            mediaSourceURL = parametersRoot.get(field, "").asString();

        field = "FileFormat";
        mediaFileFormat = parametersRoot.get(field, "").asString();

        string httpPrefix ("http://");
        string httpsPrefix ("https://");
        string ftpPrefix ("ftp://");
        string ftpsPrefix ("ftps://");
        string movePrefix("move://");   // move:///dir1/dir2/.../file
        string copyPrefix("copy://");
        string externalStoragePrefix("externalStorage://");
        if ((mediaSourceURL.size() >= httpPrefix.size()
					&& 0 == mediaSourceURL.compare(0, httpPrefix.size(), httpPrefix))
                || (mediaSourceURL.size() >= httpsPrefix.size()
					&& 0 == mediaSourceURL.compare(0, httpsPrefix.size(), httpsPrefix))
                || (mediaSourceURL.size() >= ftpPrefix.size()
					&& 0 == mediaSourceURL.compare(0, ftpPrefix.size(), ftpPrefix))
                || (mediaSourceURL.size() >= ftpsPrefix.size()
					&& 0 == mediaSourceURL.compare(0, ftpsPrefix.size(), ftpsPrefix))
                )
        {
            nextIngestionStatus = MMSEngineDBFacade::IngestionStatus::SourceDownloadingInProgress;
        }
        else if (mediaSourceURL.size() >= movePrefix.size()
				&& 0 == mediaSourceURL.compare(0, movePrefix.size(), movePrefix))
        {
            nextIngestionStatus = MMSEngineDBFacade::IngestionStatus::SourceMovingInProgress;            
        }
        else if (mediaSourceURL.size() >= copyPrefix.size()
				&& 0 == mediaSourceURL.compare(0, copyPrefix.size(), copyPrefix))
        {
            nextIngestionStatus = MMSEngineDBFacade::IngestionStatus::SourceCopingInProgress;
        }
        else if (mediaSourceURL.size() >= externalStoragePrefix.size()
				&& 0 == mediaSourceURL.compare(0, externalStoragePrefix.size(), externalStoragePrefix))
        {
			externalReadOnlyStorage = true;
        }
        else
        {
            nextIngestionStatus = MMSEngineDBFacade::IngestionStatus::SourceUploadingInProgress;
        }
    }

    string md5FileCheckSum;
    field = "MD5FileCheckSum";
    if (JSONUtils::isMetadataPresent(parametersRoot, field))
    {
        MD5         md5;
        char        md5RealDigest [32 + 1];

        md5FileCheckSum = parametersRoot.get(field, "XXX").asString();
    }

    int fileSizeInBytes = -1;
    field = "FileSizeInBytes";
    if (JSONUtils::isMetadataPresent(parametersRoot, field))
        fileSizeInBytes = JSONUtils::asInt(parametersRoot, field, 3);

	/*
    tuple<MMSEngineDBFacade::IngestionStatus, string, string, string, int> mediaSourceDetails;
    get<0>(mediaSourceDetails) = nextIngestionStatus;
    get<1>(mediaSourceDetails) = mediaSourceURL;
    get<2>(mediaSourceDetails) = mediaFileFormat;
    get<3>(mediaSourceDetails) = md5FileCheckSum;
    get<4>(mediaSourceDetails) = fileSizeInBytes;
	*/

    _logger->info(__FILEREF__ + "media source details"
		+ ", _processorIdentifier: " + to_string(_processorIdentifier)
		+ ", ingestionJobKey: " + to_string(ingestionJobKey)
		+ ", nextIngestionStatus: " + MMSEngineDBFacade::toString(nextIngestionStatus)
		+ ", mediaSourceURL: " + mediaSourceURL
		+ ", mediaFileFormat: " + mediaFileFormat
		+ ", md5FileCheckSum: " + md5FileCheckSum
		+ ", fileSizeInBytes: " + to_string(fileSizeInBytes)
		+ ", externalReadOnlyStorage: " + to_string(externalReadOnlyStorage)
	);

    return make_tuple(nextIngestionStatus, mediaSourceURL, mediaFileFormat,
			md5FileCheckSum, fileSizeInBytes, externalReadOnlyStorage);
}

void MMSEngineProcessor::validateMediaSourceFile (int64_t ingestionJobKey,
        string mediaSourcePathName, string mediaFileFormat,
        string md5FileCheckSum, int fileSizeInBytes)
{

	if (mediaFileFormat == "m3u8")
	{
		// in this case it is a directory with segments inside
		if (!FileIO::directoryExisting(mediaSourcePathName))
		{
			string errorMessage = __FILEREF__ + "Media Source directory does not exist (it was not uploaded yet)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", mediaSourcePathName: " + mediaSourcePathName;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
	}
	else
	{
		// we added the following two parameters for the FileIO::fileExisting method
		// because, in the scenario where still MMS generates the file to be ingested
		// (i.e.: generate frames task and other tasks), and the NFS is used, we saw sometimes
		// FileIO::fileExisting returns false even if the file is there. This is due because of NFS 
		// delay to present the file 
		long maxMillisecondsToWait = 5000;
		long milliSecondsWaitingBetweenChecks = 100;
		if (!FileIO::fileExisting(mediaSourcePathName,
			maxMillisecondsToWait, milliSecondsWaitingBetweenChecks))
		{
			string errorMessage = __FILEREF__ + "Media Source file does not exist (it was not uploaded yet)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", mediaSourcePathName: " + mediaSourcePathName;
			_logger->error(errorMessage);

			throw runtime_error(errorMessage);
		}
	}

	// we just simplify and md5FileCheck is not done in case of segments
    if (mediaFileFormat != "m3u8" && md5FileCheckSum != "")
    {
        MD5         md5;
        char        md5RealDigest [32 + 1];

        strcpy (md5RealDigest, md5.digestFile((char *) mediaSourcePathName.c_str()));

        if (md5FileCheckSum != md5RealDigest)
        {
            string errorMessage = __FILEREF__ + "MD5 check failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", mediaSourcePathName: " + mediaSourcePathName
                + ", md5FileCheckSum: " + md5FileCheckSum
                + ", md5RealDigest: " + md5RealDigest
                    ;
            _logger->error(errorMessage);
            throw runtime_error(errorMessage);
        }
    }
    
	// we just simplify and file size check is not done in case of segments
    if (mediaFileFormat != "m3u8" && fileSizeInBytes != -1)
    {
        bool inCaseOfLinkHasItToBeRead = false;
        unsigned long downloadedFileSizeInBytes = 
            FileIO:: getFileSizeInBytes (mediaSourcePathName, inCaseOfLinkHasItToBeRead);

        if (fileSizeInBytes != downloadedFileSizeInBytes)
        {
            string errorMessage = __FILEREF__ + "FileSize check failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", mediaSourcePathName: " + mediaSourcePathName
                + ", metadataFileSizeInBytes: " + to_string(fileSizeInBytes)
                + ", downloadedFileSizeInBytes: " + to_string(downloadedFileSizeInBytes)
            ;
            _logger->error(errorMessage);
            throw runtime_error(errorMessage);
        }
    }    
}

size_t curlDownloadCallback(char* ptr, size_t size, size_t nmemb, void *f)
{
    MMSEngineProcessor::CurlDownloadData* curlDownloadData = (MMSEngineProcessor::CurlDownloadData*) f;
    
    auto logger = spdlog::get("mmsEngineService");

    if (curlDownloadData->currentChunkNumber == 0)
    {
        (curlDownloadData->mediaSourceFileStream).open(
                curlDownloadData->destBinaryPathName, ofstream::binary | ofstream::trunc);
        curlDownloadData->currentChunkNumber += 1;
        
        logger->info(__FILEREF__ + "Opening binary file"
             + ", curlDownloadData -> destBinaryPathName: " + curlDownloadData -> destBinaryPathName
             + ", curlDownloadData->currentChunkNumber: " + to_string(curlDownloadData->currentChunkNumber)
             + ", curlDownloadData->currentTotalSize: " + to_string(curlDownloadData->currentTotalSize)
             + ", curlDownloadData->maxChunkFileSize: " + to_string(curlDownloadData->maxChunkFileSize)
        );
    }
    else if (curlDownloadData->currentTotalSize >= 
            curlDownloadData->currentChunkNumber * curlDownloadData->maxChunkFileSize)
    {
        (curlDownloadData->mediaSourceFileStream).close();

        /*
        string localPathFileName = curlDownloadData->workspaceIngestionBinaryPathName
                // + ".new"
                ;
        if (curlDownloadData->currentChunkNumber >= 2)
        {
            try
            {
                bool removeSrcFileAfterConcat = true;

                logger->info(__FILEREF__ + "Concat file"
                    + ", localPathFileName: " + localPathFileName
                    + ", curlDownloadData->workspaceIngestionBinaryPathName: " + curlDownloadData->workspaceIngestionBinaryPathName
                    + ", removeSrcFileAfterConcat: " + to_string(removeSrcFileAfterConcat)
                );

                FileIO::concatFile(curlDownloadData->workspaceIngestionBinaryPathName, localPathFileName, removeSrcFileAfterConcat);
            }
            catch(runtime_error e)
            {
                string errorMessage = string("Error to concat file")
                    + ", localPathFileName: " + localPathFileName
                    + ", curlDownloadData->workspaceIngestionBinaryPathName: " + curlDownloadData->workspaceIngestionBinaryPathName
                        + ", e.what(): " + e.what()
                ;
                logger->error(__FILEREF__ + errorMessage);

                throw runtime_error(errorMessage);            
            }
            catch(exception e)
            {
                string errorMessage = string("Error to concat file")
                    + ", localPathFileName: " + localPathFileName
                    + ", curlDownloadData->workspaceIngestionBinaryPathName: " + curlDownloadData->workspaceIngestionBinaryPathName
                ;
                logger->error(__FILEREF__ + errorMessage);

                throw runtime_error(errorMessage);            
            }
        }
         */
        // (curlDownloadData->mediaSourceFileStream).open(localPathFileName, ios::binary | ios::out | ios::trunc);
        (curlDownloadData->mediaSourceFileStream).open(curlDownloadData->destBinaryPathName, ofstream::binary | ofstream::app);
        curlDownloadData->currentChunkNumber += 1;

        logger->info(__FILEREF__ + "Opening binary file"
             + ", curlDownloadData->destBinaryPathName: " + curlDownloadData->destBinaryPathName
             + ", curlDownloadData->currentChunkNumber: " + to_string(curlDownloadData->currentChunkNumber)
             + ", curlDownloadData->currentTotalSize: " + to_string(curlDownloadData->currentTotalSize)
             + ", curlDownloadData->maxChunkFileSize: " + to_string(curlDownloadData->maxChunkFileSize)
        );
    }
    
    curlDownloadData->mediaSourceFileStream.write(ptr, size * nmemb);
    curlDownloadData->currentTotalSize += (size * nmemb);
    

    return size * nmemb;        
};

void MMSEngineProcessor::downloadMediaSourceFileThread(
        shared_ptr<long> processorsThreadsNumber, string sourceReferenceURL, bool segmentedContent,
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace)
{
    bool downloadingCompleted = false;

/*
    - aggiungere un timeout nel caso nessun pacchetto è ricevuto entro XXXX seconds
    - per il resume:
        l'apertura dello stream of dovrà essere fatta in append in questo caso
        usare l'opzione CURLOPT_RESUME_FROM o CURLOPT_RESUME_FROM_LARGE (>2GB) per dire da dove ripartire
    per ftp vedere https://raw.githubusercontent.com/curl/curl/master/docs/examples/ftpuploadresume.c
 
RESUMING FILE TRANSFERS 
  
 To continue a file transfer where it was previously aborted, curl supports 
 resume on http(s) downloads as well as ftp uploads and downloads. 
  
 Continue downloading a document: 
  
        curl -C - -o file ftp://ftp.server.com/path/file 
  
 Continue uploading a document(*1): 
  
        curl -C - -T file ftp://ftp.server.com/path/file 
  
 Continue downloading a document from a web server(*2): 
  
        curl -C - -o file http://www.server.com/ 
  
 (*1) = This requires that the ftp server supports the non-standard command 
        SIZE. If it doesn't, curl will say so. 
  
 (*2) = This requires that the web server supports at least HTTP/1.1. If it 
        doesn't, curl will say so. 
 */    

	string localSourceReferenceURL = sourceReferenceURL;
	bool localSegmentedContent = segmentedContent;
	// in case of youtube url, the real URL to be used has to be calcolated
	{
		string youTubePrefix1 ("https://www.youtube.com/");
		string youTubePrefix2 ("https://youtu.be/");
		if (
			(sourceReferenceURL.size() >= youTubePrefix1.size()
				&& 0 == sourceReferenceURL.compare(0, youTubePrefix1.size(), youTubePrefix1))
			||
			(sourceReferenceURL.size() >= youTubePrefix2.size()
				&& 0 == sourceReferenceURL.compare(0, youTubePrefix2.size(), youTubePrefix2))
			)
		{
			try
			{
				FFMpeg ffmpeg (_configuration, _logger);
				pair<string, string> streamingURLDetails =
					ffmpeg.retrieveStreamingYouTubeURL(
					ingestionJobKey, -1,
					sourceReferenceURL);

				string streamingYouTubeURL;
				tie(streamingYouTubeURL, ignore) = streamingURLDetails;

				_logger->info(__FILEREF__ + "downloadMediaSourceFileThread. YouTube URL calculation"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", _ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", initial YouTube URL: " + sourceReferenceURL
					+ ", streaming YouTube URL: " + streamingYouTubeURL
				);

				localSourceReferenceURL = streamingYouTubeURL;

				// for sure localSegmentedContent has to be false
				localSegmentedContent = false;
			}
			catch(runtime_error e)
			{
				string errorMessage = __FILEREF__ + "ffmpeg.retrieveStreamingYouTubeURL failed"
					+ ", may be the YouTube URL is not available anymore"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", YouTube URL: " + sourceReferenceURL
					+ ", e.what(): " + e.what()
				;
				_logger->error(errorMessage);
                    
				_logger->info(__FILEREF__ + "Update IngestionJob"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", IngestionStatus: " + "End_IngestionFailure"
					+ ", errorMessage: " + errorMessage
				);
				try
				{
					_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
						MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
						errorMessage);
				}
				catch(runtime_error& re)
				{
					_logger->info(__FILEREF__ + "Update IngestionJob failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", errorMessage: " + re.what()
					);
				}
				catch(exception ex)
				{
					_logger->info(__FILEREF__ + "Update IngestionJob failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey)
						+ ", errorMessage: " + ex.what()
					);
				}

				return;
			}
		}
	}

	string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(workspace);
	string destBinaryPathName =
		workspaceIngestionRepository
		+ "/"
		+ to_string(ingestionJobKey)
		+ "_source";
	if (localSegmentedContent)
		destBinaryPathName = destBinaryPathName + ".tar.gz";


        
    for (int attemptIndex = 0; attemptIndex < _maxDownloadAttemptNumber && !downloadingCompleted; attemptIndex++)
    {
        bool downloadingStoppedByUser = false;
        
        try 
        {
            _logger->info(__FILEREF__ + "Downloading"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", localSourceReferenceURL: " + localSourceReferenceURL
                + ", attempt: " + to_string(attemptIndex + 1)
                + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
            );
            
            if (attemptIndex == 0)
            {
                CurlDownloadData curlDownloadData;
                curlDownloadData.currentChunkNumber = 0;
                curlDownloadData.currentTotalSize = 0;
                curlDownloadData.destBinaryPathName   = destBinaryPathName;
                curlDownloadData.maxChunkFileSize    = _downloadChunkSizeInMegaBytes * 1000000;
                
                // fstream mediaSourceFileStream(destBinaryPathName, ios::binary | ios::out);
                // mediaSourceFileStream.exceptions(ios::badbit | ios::failbit);   // setting the exception mask
                // FILE *mediaSourceFileStream = fopen(destBinaryPathName.c_str(), "wb");

                curlpp::Cleanup cleaner;
                curlpp::Easy request;

                // Set the writer callback to enable cURL 
                // to write result in a memory area
                // request.setOpt(new curlpp::options::WriteStream(&mediaSourceFileStream));
                
                curlpp::options::WriteFunctionCurlFunction curlDownloadCallbackFunction(curlDownloadCallback);
                curlpp::OptionTrait<void *, CURLOPT_WRITEDATA> curlDownloadDataData(&curlDownloadData);
                request.setOpt(curlDownloadCallbackFunction);
                request.setOpt(curlDownloadDataData);

                // Setting the URL to retrive.
                request.setOpt(new curlpp::options::Url(localSourceReferenceURL));
                string httpsPrefix("https");
                if (localSourceReferenceURL.size() >= httpsPrefix.size()
						&& 0 == localSourceReferenceURL.compare(0, httpsPrefix.size(), httpsPrefix))
                {
                    // disconnect if we can't validate server's cert
                    bool bSslVerifyPeer = false;
                    curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                    request.setOpt(sslVerifyPeer);

                    curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                    request.setOpt(sslVerifyHost);
                }

                chrono::system_clock::time_point lastProgressUpdate = chrono::system_clock::now();
                double lastPercentageUpdated = -1.0;
                curlpp::types::ProgressFunctionFunctor functor = bind(&MMSEngineProcessor::progressDownloadCallback, this,
                        ingestionJobKey, lastProgressUpdate, lastPercentageUpdated, downloadingStoppedByUser,
                        placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
                request.setOpt(new curlpp::options::ProgressFunction(curlpp::types::ProgressFunctionFunctor(functor)));
                request.setOpt(new curlpp::options::NoProgress(0L));
                
                _logger->info(__FILEREF__ + "Downloading media file"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", localSourceReferenceURL: " + localSourceReferenceURL
                );
                request.perform();
                
                (curlDownloadData.mediaSourceFileStream).close();

                /*
                string localPathFileName = curlDownloadData.destBinaryPathName
                        + ".new";
                if (curlDownloadData.currentChunkNumber >= 2)
                {
                    try
                    {
                        bool removeSrcFileAfterConcat = true;

                        _logger->info(__FILEREF__ + "Concat file"
                            + ", localPathFileName: " + localPathFileName
                            + ", curlDownloadData.destBinaryPathName: " + curlDownloadData.destBinaryPathName
                            + ", removeSrcFileAfterConcat: " + to_string(removeSrcFileAfterConcat)
                        );

                        FileIO::concatFile(curlDownloadData.destBinaryPathName, localPathFileName, removeSrcFileAfterConcat);
                    }
                    catch(runtime_error e)
                    {
                        string errorMessage = string("Error to concat file")
                            + ", localPathFileName: " + localPathFileName
                            + ", curlDownloadData.destBinaryPathName: " + curlDownloadData.destBinaryPathName
                                + ", e.what(): " + e.what()
                        ;
                        _logger->error(__FILEREF__ + errorMessage);

                        throw runtime_error(errorMessage);            
                    }
                    catch(exception e)
                    {
                        string errorMessage = string("Error to concat file")
                            + ", localPathFileName: " + localPathFileName
                            + ", curlDownloadData.destBinaryPathName: " + curlDownloadData.destBinaryPathName
                        ;
                        _logger->error(__FILEREF__ + errorMessage);

                        throw runtime_error(errorMessage);            
                    }
                }
                */
            }
            else
            {
                _logger->warn(__FILEREF__ + "Coming from a download failure, trying to Resume"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                );
                
                // FILE *mediaSourceFileStream = fopen(destBinaryPathName.c_str(), "wb+");
                long long fileSize;
                {
                    ofstream mediaSourceFileStream(destBinaryPathName, ofstream::binary | ofstream::app);
                    fileSize = mediaSourceFileStream.tellp();
                    mediaSourceFileStream.close();
                }

                CurlDownloadData curlDownloadData;
                curlDownloadData.destBinaryPathName   = destBinaryPathName;
                curlDownloadData.maxChunkFileSize    = _downloadChunkSizeInMegaBytes * 1000000;

                curlDownloadData.currentChunkNumber = fileSize % curlDownloadData.maxChunkFileSize;
                // fileSize = curlDownloadData.currentChunkNumber * curlDownloadData.maxChunkFileSize;
                curlDownloadData.currentTotalSize = fileSize;

                curlpp::Cleanup cleaner;
                curlpp::Easy request;

                // Set the writer callback to enable cURL 
                // to write result in a memory area
                // request.setOpt(new curlpp::options::WriteStream(&mediaSourceFileStream));

                curlpp::options::WriteFunctionCurlFunction curlDownloadCallbackFunction(curlDownloadCallback);
                curlpp::OptionTrait<void *, CURLOPT_WRITEDATA> curlDownloadDataData(&curlDownloadData);
                request.setOpt(curlDownloadCallbackFunction);
                request.setOpt(curlDownloadDataData);

                // Setting the URL to retrive.
                request.setOpt(new curlpp::options::Url(localSourceReferenceURL));
                string httpsPrefix("https");
                if (localSourceReferenceURL.size() >= httpsPrefix.size()
						&& 0 == localSourceReferenceURL.compare(0, httpsPrefix.size(), httpsPrefix))
                {
                    _logger->info(__FILEREF__ + "Setting SslEngineDefault"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    );
                    request.setOpt(new curlpp::options::SslEngineDefault());
                }

                chrono::system_clock::time_point lastTimeProgressUpdate = chrono::system_clock::now();
                double lastPercentageUpdated = -1.0;
                curlpp::types::ProgressFunctionFunctor functor = bind(&MMSEngineProcessor::progressDownloadCallback, this,
                        ingestionJobKey, lastTimeProgressUpdate, lastPercentageUpdated, downloadingStoppedByUser,
                        placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
                request.setOpt(new curlpp::options::ProgressFunction(curlpp::types::ProgressFunctionFunctor(functor)));
                request.setOpt(new curlpp::options::NoProgress(0L));
                
                if (fileSize > 2 * 1000 * 1000 * 1000)
                    request.setOpt(new curlpp::options::ResumeFromLarge(fileSize));
                else
                    request.setOpt(new curlpp::options::ResumeFrom(fileSize));
                
                _logger->info(__FILEREF__ + "Resume Download media file"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", localSourceReferenceURL: " + localSourceReferenceURL
                    + ", resuming from fileSize: " + to_string(fileSize)
                );
                request.perform();
                
                (curlDownloadData.mediaSourceFileStream).close();

                /*
                string localPathFileName = curlDownloadData.destBinaryPathName
                        + ".new";
                if (curlDownloadData.currentChunkNumber >= 2)
                {
                    try
                    {
                        bool removeSrcFileAfterConcat = true;

                        _logger->info(__FILEREF__ + "Concat file"
                            + ", localPathFileName: " + localPathFileName
                            + ", curlDownloadData.destBinaryPathName: " + curlDownloadData.destBinaryPathName
                            + ", removeSrcFileAfterConcat: " + to_string(removeSrcFileAfterConcat)
                        );

                        FileIO::concatFile(curlDownloadData.destBinaryPathName, localPathFileName, removeSrcFileAfterConcat);
                    }
                    catch(runtime_error e)
                    {
                        string errorMessage = string("Error to concat file")
                            + ", localPathFileName: " + localPathFileName
                            + ", curlDownloadData.destBinaryPathName: " + curlDownloadData.destBinaryPathName
                                + ", e.what(): " + e.what()
                        ;
                        _logger->error(__FILEREF__ + errorMessage);

                        throw runtime_error(errorMessage);            
                    }
                    catch(exception e)
                    {
                        string errorMessage = string("Error to concat file")
                            + ", localPathFileName: " + localPathFileName
                            + ", curlDownloadData.destBinaryPathName: " + curlDownloadData.destBinaryPathName
                        ;
                        _logger->error(__FILEREF__ + errorMessage);

                        throw runtime_error(errorMessage);            
                    }
                }
                 */
            }

			if (localSegmentedContent)
			{
				try
				{
					// by a convention, the directory inside the tar file has to be named as 'content'
					string sourcePathName = "/content.tar.gz";

					_logger->info(__FILEREF__ + "Calling manageTarFileInCaseOfIngestionOfSegments "
						+ ", destBinaryPathName: " + destBinaryPathName
						+ ", workspaceIngestionRepository: " + workspaceIngestionRepository
						+ ", sourcePathName: " + sourcePathName
					);
					manageTarFileInCaseOfIngestionOfSegments(ingestionJobKey,
						destBinaryPathName, workspaceIngestionRepository,
						sourcePathName);
				}
				catch(runtime_error e)
				{
					string errorMessage = string("manageTarFileInCaseOfIngestionOfSegments failed")
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
						+ ", localSourceReferenceURL: " + localSourceReferenceURL 
					;
           
					_logger->error(__FILEREF__ + errorMessage);
           
					throw runtime_error(errorMessage);
				}
			}

            downloadingCompleted = true;

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", destBinaryPathName: " + destBinaryPathName
                + ", downloadingCompleted: " + to_string(downloadingCompleted)
            );
            _mmsEngineDBFacade->updateIngestionJobSourceBinaryTransferred (
                ingestionJobKey, downloadingCompleted);
        }
        catch (curlpp::LogicError & e) 
        {
            _logger->error(__FILEREF__ + "Download failed (LogicError)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", localSourceReferenceURL: " + localSourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->error(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
					try
					{
						_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                            MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                            e.what());
					}
					catch(runtime_error& re)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + re.what()
							);
					}
					catch(exception ex)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + ex.what()
							);
					}

                    return;
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", localSourceReferenceURL: " + localSourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
        catch (curlpp::RuntimeError & e) 
        {
            _logger->error(__FILEREF__ + "Download failed (RuntimeError)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", localSourceReferenceURL: " + localSourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->info(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
					try
					{
						_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                            MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                            e.what());
					}
					catch(runtime_error& re)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + re.what()
							);
					}
					catch(exception ex)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + ex.what()
							);
					}

                    return;
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", localSourceReferenceURL: " + localSourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
        catch (runtime_error e)
        {
            _logger->error(__FILEREF__ + "Download failed (runtime_error)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", localSourceReferenceURL: " + localSourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->info(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
					try
					{
						_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                            MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                            e.what());
					}
					catch(runtime_error& re)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + re.what()
							);
					}
					catch(exception ex)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + ex.what()
							);
					}
                    
                    return;
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", localSourceReferenceURL: " + localSourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
        catch (exception e)
        {
            _logger->error(__FILEREF__ + "Download failed (exception)"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", localSourceReferenceURL: " + localSourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->info(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
					try
					{
						_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                            MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                            e.what());
					}
					catch(runtime_error& re)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + re.what()
							);
					}
					catch(exception ex)
					{
						_logger->info(__FILEREF__ + "Update IngestionJob failed"
							+ ", _processorIdentifier: " + to_string(_processorIdentifier)
							+ ", ingestionJobKey: " + to_string(ingestionJobKey)
							+ ", errorMessage: " + ex.what()
							);
					}
                    
                    return;
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", localSourceReferenceURL: " + localSourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
    }
}

void MMSEngineProcessor::ftpUploadMediaSourceThread(
        shared_ptr<long> processorsThreadsNumber,
        string mmsAssetPathName, string fileName, int64_t sizeInBytes,
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace,
		int64_t mediaItemKey, int64_t physicalPathKey,
        string ftpServer, int ftpPort, string ftpUserName, string ftpPassword, 
        string ftpRemoteDirectory, string ftpRemoteFileName,
		bool updateIngestionJobToBeDone)
{

    // curl -T localfile.ext ftp://username:password@ftp.server.com/remotedir/remotefile.zip


    try 
    {
        string ftpUrl = string("ftp://") + ftpUserName + ":" + ftpPassword + "@" 
                + ftpServer 
                + ":" + to_string(ftpPort) 
                + ftpRemoteDirectory;
        
        if (ftpRemoteDirectory.size() == 0 || ftpRemoteDirectory.back() != '/')
            ftpUrl  += "/";

        if (ftpRemoteFileName == "")
            ftpUrl  += fileName;
        else
            ftpUrl += ftpRemoteFileName;

        _logger->info(__FILEREF__ + "FTP Uploading"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", sizeInBytes: " + to_string(sizeInBytes)
            + ", ftpUrl: " + ftpUrl
        );

        ifstream mmsAssetStream(mmsAssetPathName, ifstream::binary);
        // FILE *mediaSourceFileStream = fopen(workspaceIngestionBinaryPathName.c_str(), "wb");

        // 1. PORT-mode FTP (Active) - NO Firewall friendly
        //  - FTP client: Sends a request to open a command channel from its TCP port (i.e.: 6000) to the FTP server’s TCP port 21
        //  - FTP client: Sends a data request (PORT command) to the FTP server. The FTP client includes in the PORT command the data port number 
        //      it opened to receive data. In this example, the FTP client has opened TCP port 6001 to receive the data.
        //  - FTP server opens a new inbound connection to the FTP client on the port indicated by the FTP client in the PORT command. 
        //      The FTP server source port is TCP port 20. In this example, the FTP server sends data from its own TCP port 20 to the FTP client’s TCP port 6001.
        //  In this conversation, two connections were established: an outbound connection initiated by the FTP client and an inbound connection established by the FTP server.
        // 2. PASV-mode FTP (Passive) - Firewall friendly
        //  - FTP client sends a request to open a command channel from its TCP port (i.e.: 6000) to the FTP server’s TCP port 21
        //  - FTP client sends a PASV command requesting that the FTP server open a port number that the FTP client can connect to establish the data channel.
        //      FTP serve sends over the command channel the TCP port number that the FTP client can initiate a connection to establish the data channel (i.e.: 7000)
        //  - FTP client opens a new connection from its own response port TCP 6001 to the FTP server’s data channel 7000. Data transfer takes place through this channel.
        
        // Active/Passive... see the next URL, section 'FTP Peculiarities We Need'
        // https://curl.haxx.se/libcurl/c/libcurl-tutorial.html

        // https://curl.haxx.se/libcurl/c/ftpupload.html
        curlpp::Cleanup cleaner;
        curlpp::Easy request;

        request.setOpt(new curlpp::options::Url(ftpUrl));
        request.setOpt(new curlpp::options::Verbose(false)); 
        request.setOpt(new curlpp::options::Upload(true)); 
        
        request.setOpt(new curlpp::options::ReadStream(&mmsAssetStream));
        request.setOpt(new curlpp::options::InfileSizeLarge((curl_off_t) sizeInBytes));
        
        
        bool bFtpUseEpsv = false;
        curlpp::OptionTrait<bool, CURLOPT_FTP_USE_EPSV> ftpUseEpsv(bFtpUseEpsv);
        request.setOpt(ftpUseEpsv);

        // curl will default to binary transfer mode for FTP, 
        // and you ask for ascii mode instead with -B, --use-ascii or 
        // by making sure the URL ends with ;type=A.
        
        // timeout (CURLOPT_FTP_RESPONSE_TIMEOUT)
        
        bool bCreatingMissingDir = true;
        curlpp::OptionTrait<bool, CURLOPT_FTP_CREATE_MISSING_DIRS> creatingMissingDir(bCreatingMissingDir);
        request.setOpt(creatingMissingDir);

        string ftpsPrefix("ftps");
        if (ftpUrl.size() >= ftpsPrefix.size() && 0 == ftpUrl.compare(0, ftpsPrefix.size(), ftpsPrefix))
        {
            /* Next statements is in case we want ftp protocol to use SSL or TLS
             * google CURLOPT_FTPSSLAUTH and CURLOPT_FTP_SSL

            // disconnect if we can't validate server's cert
            bool bSslVerifyPeer = false;
            curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
            request.setOpt(sslVerifyPeer);

            curlpp::OptionTrait<curl_ftpssl, CURLOPT_FTP_SSL> ftpSsl(CURLFTPSSL_TRY);
            request.setOpt(ftpSsl);

            curlpp::OptionTrait<curl_ftpauth, CURLOPT_FTPSSLAUTH> ftpSslAuth(CURLFTPAUTH_TLS);
            request.setOpt(ftpSslAuth);
             */
        }

        // FTP progress works only in case of FTP Passive
        chrono::system_clock::time_point lastProgressUpdate = chrono::system_clock::now();
        double lastPercentageUpdated = -1.0;
        bool uploadingStoppedByUser = false;
        curlpp::types::ProgressFunctionFunctor functor = bind(&MMSEngineProcessor::progressUploadCallback, this,
                ingestionJobKey, lastProgressUpdate, lastPercentageUpdated, uploadingStoppedByUser,
                placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
        request.setOpt(new curlpp::options::ProgressFunction(curlpp::types::ProgressFunctionFunctor(functor)));
        request.setOpt(new curlpp::options::NoProgress(0L));

        _logger->info(__FILEREF__ + "FTP Uploading media file"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", sizeInBytes: " + to_string(sizeInBytes)
        );
        request.perform();

		// FTP-Delivery just forward the MIK to the next Task
		{
			_logger->info(__FILEREF__ + "addIngestionJobOutput"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", mediaItemKey: " + to_string(mediaItemKey)
				+ ", physicalPathKey: " + to_string(physicalPathKey)
			);
			int64_t liveRecordingIngestionJobKey = -1;
			_mmsEngineDBFacade->addIngestionJobOutput(ingestionJobKey,
				mediaItemKey, physicalPathKey, liveRecordingIngestionJobKey);
		}

		if (updateIngestionJobToBeDone)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_TaskSuccess"
				+ ", errorMessage: " + ""
			);
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
			);
		}
    }
    catch (curlpp::LogicError & e) 
    {
        _logger->error(__FILEREF__ + "Download failed (LogicError)"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", mmsAssetPathName: " + mmsAssetPathName 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (curlpp::RuntimeError & e) 
    {
        _logger->error(__FILEREF__ + "Download failed (RuntimeError)"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", mmsAssetPathName: " + mmsAssetPathName 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Download failed (exception)"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", mmsAssetPathName: " + mmsAssetPathName 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
}

size_t curlUploadVideoOnFacebookCallback(char* ptr, size_t size, size_t nmemb, void *f)
{
    MMSEngineProcessor::CurlUploadFacebookData* curlUploadData = (MMSEngineProcessor::CurlUploadFacebookData*) f;
    
    auto logger = spdlog::get("mmsEngineService");


    if (!curlUploadData->bodyFirstPartSent)
    {
        if (curlUploadData->bodyFirstPart.size() > size * nmemb)
        {
            logger->error(__FILEREF__ + "Not enougth memory!!!"
                + ", curlUploadData->bodyFirstPartSent: " + to_string(curlUploadData->bodyFirstPartSent)
                + ", curlUploadData->bodyFirstPart: " + curlUploadData->bodyFirstPart
                + ", curlUploadData->bodyLastPartSent: " + to_string(curlUploadData->bodyLastPartSent)
                + ", curlUploadData->bodyLastPart: " + curlUploadData->bodyLastPart
                + ", curlUploadData->startOffset: " + to_string(curlUploadData->startOffset)
                + ", curlUploadData->endOffset: " + to_string(curlUploadData->endOffset)
                + ", curlUploadData->currentOffset: " + to_string(curlUploadData->currentOffset)
                + ", curlUploadData->bodyFirstPart.size(): " + to_string(curlUploadData->bodyFirstPart.size())
                + ", size * nmemb: " + to_string(size * nmemb)
            );

            return CURL_READFUNC_ABORT;
        }
        
        strcpy(ptr, curlUploadData->bodyFirstPart.c_str());
        
        curlUploadData->bodyFirstPartSent = true;

        logger->info(__FILEREF__ + "First read"
             + ", curlUploadData->bodyFirstPartSent: " + to_string(curlUploadData->bodyFirstPartSent)
             + ", curlUploadData->bodyFirstPart: " + curlUploadData->bodyFirstPart
             + ", curlUploadData->bodyLastPartSent: " + to_string(curlUploadData->bodyLastPartSent)
             + ", curlUploadData->bodyLastPart: " + curlUploadData->bodyLastPart
             + ", curlUploadData->startOffset: " + to_string(curlUploadData->startOffset)
             + ", curlUploadData->endOffset: " + to_string(curlUploadData->endOffset)
             + ", curlUploadData->currentOffset: " + to_string(curlUploadData->currentOffset)
        );
        
        return curlUploadData->bodyFirstPart.size();
    }
    else if (curlUploadData->currentOffset == curlUploadData->endOffset)
    {
        if (!curlUploadData->bodyLastPartSent)
        {
            if (curlUploadData->bodyLastPart.size() > size * nmemb)
            {
                logger->error(__FILEREF__ + "Not enougth memory!!!"
                    + ", curlUploadData->bodyFirstPartSent: " + to_string(curlUploadData->bodyFirstPartSent)
                    + ", curlUploadData->bodyFirstPart: " + curlUploadData->bodyFirstPart
                    + ", curlUploadData->bodyLastPartSent: " + to_string(curlUploadData->bodyLastPartSent)
                    + ", curlUploadData->bodyLastPart: " + curlUploadData->bodyLastPart
                    + ", curlUploadData->startOffset: " + to_string(curlUploadData->startOffset)
                    + ", curlUploadData->endOffset: " + to_string(curlUploadData->endOffset)
                    + ", curlUploadData->currentOffset: " + to_string(curlUploadData->currentOffset)
                    + ", curlUploadData->bodyLastPart.size(): " + to_string(curlUploadData->bodyLastPart.size())
                    + ", size * nmemb: " + to_string(size * nmemb)
                );

                return CURL_READFUNC_ABORT;
            }

            strcpy(ptr, curlUploadData->bodyLastPart.c_str());

            curlUploadData->bodyLastPartSent = true;

            logger->info(__FILEREF__ + "Last read"
                + ", curlUploadData->bodyFirstPartSent: " + to_string(curlUploadData->bodyFirstPartSent)
                + ", curlUploadData->bodyFirstPart: " + curlUploadData->bodyFirstPart
                + ", curlUploadData->bodyLastPartSent: " + to_string(curlUploadData->bodyLastPartSent)
                + ", curlUploadData->bodyLastPart: " + curlUploadData->bodyLastPart
                + ", curlUploadData->startOffset: " + to_string(curlUploadData->startOffset)
                + ", curlUploadData->endOffset: " + to_string(curlUploadData->endOffset)
                + ", curlUploadData->currentOffset: " + to_string(curlUploadData->currentOffset)
            );

            return curlUploadData->bodyLastPart.size();
        }
        else
        {
            logger->error(__FILEREF__ + "This scenario should never happen because Content-Length was set"
                + ", curlUploadData->bodyFirstPartSent: " + to_string(curlUploadData->bodyFirstPartSent)
                + ", curlUploadData->bodyFirstPart: " + curlUploadData->bodyFirstPart
                + ", curlUploadData->bodyLastPartSent: " + to_string(curlUploadData->bodyLastPartSent)
                + ", curlUploadData->bodyLastPart: " + curlUploadData->bodyLastPart
                + ", curlUploadData->startOffset: " + to_string(curlUploadData->startOffset)
                + ", curlUploadData->endOffset: " + to_string(curlUploadData->endOffset)
                + ", curlUploadData->currentOffset: " + to_string(curlUploadData->currentOffset)
            );

            return CURL_READFUNC_ABORT;
        }
    }

    if(curlUploadData->currentOffset + (size * nmemb) <= curlUploadData->endOffset)
        curlUploadData->mediaSourceFileStream.read(ptr, size * nmemb);
    else
        curlUploadData->mediaSourceFileStream.read(ptr, curlUploadData->endOffset - curlUploadData->currentOffset);

    int64_t charsRead = curlUploadData->mediaSourceFileStream.gcount();
    
    curlUploadData->currentOffset += charsRead;

    return charsRead;        
};

void MMSEngineProcessor::postVideoOnFacebookThread(
        shared_ptr<long> processorsThreadsNumber,
        string mmsAssetPathName, int64_t sizeInBytes,
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace,
        string facebookNodeId, string facebookConfigurationLabel
        )
{
            
    string facebookURL;
    string sResponse;
    
    try
    {
        _logger->info(__FILEREF__ + "postVideoOnFacebookThread"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", sizeInBytes: " + to_string(sizeInBytes)
            + ", facebookNodeId: " + facebookNodeId
            + ", facebookConfigurationLabel: " + facebookConfigurationLabel
        );
        
        string facebookPageToken = _mmsEngineDBFacade->getFacebookPageTokenByConfigurationLabel(
                workspace->_workspaceKey, facebookConfigurationLabel);            
        
        string fileFormat;
        {
            size_t extensionIndex = mmsAssetPathName.find_last_of(".");
            if (extensionIndex == string::npos)
            {
                string errorMessage = __FILEREF__ + "No fileFormat (extension of the file) found in mmsAssetPathName"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", mmsAssetPathName: " + mmsAssetPathName
                ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            fileFormat = mmsAssetPathName.substr(extensionIndex + 1);
        }
        
        /*
            curl \
                -X POST "https://graph-video.facebook.com/v2.3/1533641336884006/videos"  \
                -F "access_token=XXXXXXXXX" \
                -F "upload_phase=start" \
                -F "file_size=152043520"

                {"upload_session_id":"1564747013773438","video_id":"1564747010440105","start_offset":"0","end_offset":"52428800"}
        */
        string uploadSessionId;
        string videoId;
        int64_t startOffset;
        int64_t endOffset;
        // start
        {
            string facebookURI = string("/") + _facebookGraphAPIVersion + "/" + facebookNodeId + "/videos";
            
            facebookURL = _facebookGraphAPIProtocol
                + "://"
                + _facebookGraphAPIHostName
                + ":" + to_string(_facebookGraphAPIPort)
                + facebookURI;
            
            // we could apply md5 to utc time
            string boundary = to_string(chrono::system_clock::to_time_t(chrono::system_clock::now()));
            string endOfLine = "\r\n";
            string body =
                    "--" + boundary + endOfLine                    
                    + "Content-Disposition: form-data; name=\"access_token\"" + endOfLine + endOfLine
                    + facebookPageToken + endOfLine
                    
                    + "--" + boundary + endOfLine
                    + "Content-Disposition: form-data; name=\"upload_phase\"" + endOfLine + endOfLine
                    + "start" + endOfLine
                    
                    + "--" + boundary + endOfLine
                    + "Content-Disposition: form-data; name=\"file_size\"" + endOfLine + endOfLine
                    + to_string(sizeInBytes) + endOfLine

                    + "--" + boundary + "--" + endOfLine + endOfLine
                    ;

            list<string> header;
            string contentTypeHeader = "Content-Type: multipart/form-data; boundary=\"" + boundary + "\"";
            header.push_back(contentTypeHeader);

            curlpp::Cleanup cleaner;
            curlpp::Easy request;

            request.setOpt(new curlpp::options::PostFields(body));
            request.setOpt(new curlpp::options::PostFieldSize(body.length()));

            request.setOpt(new curlpp::options::Url(facebookURL));
            request.setOpt(new curlpp::options::Timeout(_facebookGraphAPITimeoutInSeconds));

            if (_facebookGraphAPIProtocol == "https")
            {
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
    //                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
    //                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


                // cert is stored PEM coded in file... 
                // since PEM is default, we needn't set it for PEM 
                // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
                // equest.setOpt(sslCertType);

                // set the cert for client authentication
                // "testcert.pem"
                // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
                // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
                // request.setOpt(sslCert);

                // sorry, for engine we must set the passphrase
                //   (if the key has one...)
                // const char *pPassphrase = NULL;
                // if(pPassphrase)
                //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

                // if we use a key stored in a crypto engine,
                //   we must set the key type to "ENG"
                // pKeyType  = "PEM";
                // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

                // set the private key (file or ID in engine)
                // pKeyName  = "testkey.pem";
                // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

                // set the file with the certs vaildating the server
                // *pCACertFile = "cacert.pem";
                // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

                // disconnect if we can't validate server's cert
                bool bSslVerifyPeer = false;
                curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                request.setOpt(sslVerifyPeer);

                curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                request.setOpt(sslVerifyHost);

                // request.setOpt(new curlpp::options::SslEngineDefault());                                              

            }
            request.setOpt(new curlpp::options::HttpHeader(header));

            ostringstream response;
            request.setOpt(new curlpp::options::WriteStream(&response));

            _logger->info(__FILEREF__ + "Calling facebook"
                    + ", facebookURL: " + facebookURL
                    + ", _facebookGraphAPIProtocol: " + _facebookGraphAPIProtocol
                    + ", _facebookGraphAPIHostName: " + _facebookGraphAPIHostName
                    + ", _facebookGraphAPIPort: " + to_string(_facebookGraphAPIPort)
                    + ", facebookURI: " + facebookURI
                    + ", contentTypeHeader: " + contentTypeHeader
                    + ", body: " + body
            );
            request.perform();

            sResponse = response.str();
            _logger->info(__FILEREF__ + "Called facebook"
                    + ", facebookURL: " + facebookURL
                    + ", contentTypeHeader: " + contentTypeHeader
                    + ", body: " + body
                    + ", sResponse: " + sResponse
            );
            
            Json::Value facebookResponseRoot;
            try
            {
                Json::CharReaderBuilder builder;
                Json::CharReader* reader = builder.newCharReader();
                string errors;

                bool parsingSuccessful = reader->parse(sResponse.c_str(),
                        sResponse.c_str() + sResponse.size(), 
                        &facebookResponseRoot, &errors);
                delete reader;

                if (!parsingSuccessful)
                {
                    string errorMessage = __FILEREF__ + "failed to parse the facebook response"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", errors: " + errors
                            + ", sResponse: " + sResponse
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
            catch(...)
            {
                string errorMessage = string("facebook json response is not well format")
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(__FILEREF__ + errorMessage);

                throw runtime_error(errorMessage);
            }
            
            string field = "upload_session_id";
            if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field into the response is not present or it is null"
                        + ", Field: " + field
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            uploadSessionId = facebookResponseRoot.get(field, "XXX").asString();

            field = "video_id";
            if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field into the response is not present or it is null"
                        + ", Field: " + field
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            videoId = facebookResponseRoot.get(field, "XXX").asString();
            
            field = "start_offset";
            if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field into the response is not present or it is null"
                        + ", Field: " + field
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string sStartOffset = facebookResponseRoot.get(field, "XXX").asString();
            startOffset = stoll(sStartOffset);
            
            field = "end_offset";
            if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field into the response is not present or it is null"
                        + ", Field: " + field
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string sEndOffset = facebookResponseRoot.get(field, "XXX").asString();
            endOffset = stoll(sEndOffset);
        }
        
        while (startOffset < endOffset)
        {
            /*
                curl \
                    -X POST "https://graph-video.facebook.com/v2.3/1533641336884006/videos"  \
                    -F "access_token=XXXXXXX" \
                    -F "upload_phase=transfer" \
                    -F “start_offset=0" \
                    -F "upload_session_id=1564747013773438" \
                    -F "video_file_chunk=@chunk1.mp4"
            */
            // transfer
            {
                string facebookURI = string("/") + _facebookGraphAPIVersion + "/" + facebookNodeId + "/videos";

                facebookURL = _facebookGraphAPIProtocol
                    + "://"
                    + _facebookGraphAPIHostName
                    + ":" + to_string(_facebookGraphAPIPort)
                    + facebookURI;

                string mediaContentType = string("video") + "/" + fileFormat;                    
                
                // we could apply md5 to utc time
                string boundary = to_string(chrono::system_clock::to_time_t(chrono::system_clock::now()));
                string endOfLine = "\r\n";
                string bodyFirstPart =
                        "--" + boundary + endOfLine
                        + "Content-Disposition: form-data; name=\"access_token\"" + endOfLine + endOfLine
                        + facebookPageToken + endOfLine

                        + "--" + boundary + endOfLine
                        + "Content-Disposition: form-data; name=\"upload_phase\"" + endOfLine + endOfLine
                        + "transfer" + endOfLine

                        + "--" + boundary + endOfLine
                        + "Content-Disposition: form-data; name=\"start_offset\"" + endOfLine + endOfLine
                        + to_string(startOffset) + endOfLine

                        + "--" + boundary + endOfLine
                        + "Content-Disposition: form-data; name=\"upload_session_id\"" + endOfLine + endOfLine
                        + uploadSessionId + endOfLine

                        + "--" + boundary + endOfLine
                        + "Content-Disposition: form-data; name=\"video_file_chunk\"" + endOfLine
                        + "Content-Type: " + mediaContentType
                        + "Content-Length: " + (to_string(endOffset - startOffset)) + endOfLine + endOfLine
                        ;

                string bodyLastPart =
                        endOfLine + "--" + boundary + "--" + endOfLine + endOfLine
                        ;

                list<string> header;
                string contentTypeHeader = "Content-Type: multipart/form-data; boundary=\"" + boundary + "\"";
                header.push_back(contentTypeHeader);

                curlpp::Cleanup cleaner;
                curlpp::Easy request;

                CurlUploadFacebookData curlUploadData;
                {
                    curlUploadData.mediaSourceFileStream.open(mmsAssetPathName);

                    curlUploadData.bodyFirstPartSent    = false;
                    curlUploadData.bodyFirstPart        = bodyFirstPart;
                    
                    curlUploadData.bodyLastPartSent     = false;
                    curlUploadData.bodyLastPart         = bodyLastPart;

                    curlUploadData.currentOffset        = startOffset;

                    curlUploadData.startOffset          = startOffset;
                    curlUploadData.endOffset            = endOffset;

                    curlpp::options::ReadFunctionCurlFunction curlUploadCallbackFunction(curlUploadVideoOnFacebookCallback);
                    curlpp::OptionTrait<void *, CURLOPT_READDATA> curlUploadDataData(&curlUploadData);
                    request.setOpt(curlUploadCallbackFunction);
                    request.setOpt(curlUploadDataData);

                    bool upload = true;
                    request.setOpt(new curlpp::options::Upload(upload));
                }

                request.setOpt(new curlpp::options::Url(facebookURL));
                request.setOpt(new curlpp::options::Timeout(_facebookGraphAPITimeoutInSeconds));

                if (_facebookGraphAPIProtocol == "https")
                {
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
        //                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
        //                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


                    // cert is stored PEM coded in file... 
                    // since PEM is default, we needn't set it for PEM 
                    // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                    // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
                    // equest.setOpt(sslCertType);

                    // set the cert for client authentication
                    // "testcert.pem"
                    // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
                    // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
                    // request.setOpt(sslCert);

                    // sorry, for engine we must set the passphrase
                    //   (if the key has one...)
                    // const char *pPassphrase = NULL;
                    // if(pPassphrase)
                    //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

                    // if we use a key stored in a crypto engine,
                    //   we must set the key type to "ENG"
                    // pKeyType  = "PEM";
                    // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

                    // set the private key (file or ID in engine)
                    // pKeyName  = "testkey.pem";
                    // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

                    // set the file with the certs vaildating the server
                    // *pCACertFile = "cacert.pem";
                    // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

                    // disconnect if we can't validate server's cert
                    bool bSslVerifyPeer = false;
                    curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                    request.setOpt(sslVerifyPeer);

                    curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                    request.setOpt(sslVerifyHost);

                    // request.setOpt(new curlpp::options::SslEngineDefault());                                              

                }
                request.setOpt(new curlpp::options::HttpHeader(header));

                ostringstream response;
                request.setOpt(new curlpp::options::WriteStream(&response));

                _logger->info(__FILEREF__ + "Calling facebook"
                        + ", facebookURL: " + facebookURL
                        + ", _facebookGraphAPIProtocol: " + _facebookGraphAPIProtocol
                        + ", _facebookGraphAPIHostName: " + _facebookGraphAPIHostName
                        + ", _facebookGraphAPIPort: " + to_string(_facebookGraphAPIPort)
                        + ", facebookURI: " + facebookURI
                        + ", bodyFirstPart: " + bodyFirstPart
                );
                request.perform();

                sResponse = response.str();
                _logger->info(__FILEREF__ + "Called facebook"
                        + ", facebookURL: " + facebookURL
                        + ", bodyFirstPart: " + bodyFirstPart
                        + ", sResponse: " + sResponse
                );

                Json::Value facebookResponseRoot;
                try
                {
                    Json::CharReaderBuilder builder;
                    Json::CharReader* reader = builder.newCharReader();
                    string errors;

                    bool parsingSuccessful = reader->parse(sResponse.c_str(),
                            sResponse.c_str() + sResponse.size(), 
                            &facebookResponseRoot, &errors);
                    delete reader;

                    if (!parsingSuccessful)
                    {
                        string errorMessage = __FILEREF__ + "failed to parse the facebook response"
                            + ", _processorIdentifier: " + to_string(_processorIdentifier)
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", errors: " + errors
                                + ", sResponse: " + sResponse
                                ;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                }
                catch(...)
                {
                    string errorMessage = string("facebook json response is not well format")
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", sResponse: " + sResponse
                            ;
                    _logger->error(__FILEREF__ + errorMessage);

                    throw runtime_error(errorMessage);
                }

                string field = "start_offset";
                if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
                {
                    string errorMessage = __FILEREF__ + "Field is not present or it is null"
                            + ", Field: " + field
                            + ", sResponse: " + sResponse
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                string sStartOffset = facebookResponseRoot.get(field, "XXX").asString();
                startOffset = stoll(sStartOffset);

                field = "end_offset";
                if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
                {
                    string errorMessage = __FILEREF__ + "Field is not present or it is null"
                            + ", Field: " + field
                            + ", sResponse: " + sResponse
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                string sEndOffset = facebookResponseRoot.get(field, "XXX").asString();
                endOffset = stoll(sEndOffset);
            }
        }
        
        /*
            curl \
                -X POST "https://graph-video.facebook.com/v2.3/1533641336884006/videos"  \
                -F "access_token=XXXXXXXX" \
                -F "upload_phase=finish" \
                -F "upload_session_id=1564747013773438" 

            {"success":true}
        */
        // finish: pubblica il video e mettilo in coda per la codifica asincrona
        bool success;
        {
            string facebookURI = string("/") + _facebookGraphAPIVersion + "/" + facebookNodeId + "/videos";
            
            facebookURL = _facebookGraphAPIProtocol
                + "://"
                + _facebookGraphAPIHostName
                + ":" + to_string(_facebookGraphAPIPort)
                + facebookURI;
            
            // we could apply md5 to utc time
            string boundary = to_string(chrono::system_clock::to_time_t(chrono::system_clock::now()));
            string endOfLine = "\r\n";
            string body =
                    "--" + boundary + endOfLine                    
                    + "Content-Disposition: form-data; name=\"access_token\"" + endOfLine + endOfLine
                    + facebookPageToken + endOfLine
                    
                    + "--" + boundary + endOfLine                    
                    + "Content-Disposition: form-data; name=\"upload_phase\"" + endOfLine + endOfLine
                    + "finish" + endOfLine
                    
                    + "--" + boundary + endOfLine                    
                    + "Content-Disposition: form-data; name=\"upload_session_id\"" + endOfLine + endOfLine
                    + uploadSessionId + endOfLine

                    + "--" + boundary + "--" + endOfLine + endOfLine
                    ;

            list<string> header;
            string contentTypeHeader = "Content-Type: multipart/form-data; boundary=\"" + boundary + "\"";
            header.push_back(contentTypeHeader);

            curlpp::Cleanup cleaner;
            curlpp::Easy request;

            request.setOpt(new curlpp::options::PostFields(body));
            request.setOpt(new curlpp::options::PostFieldSize(body.length()));

            request.setOpt(new curlpp::options::Url(facebookURL));
            request.setOpt(new curlpp::options::Timeout(_facebookGraphAPITimeoutInSeconds));

            if (_facebookGraphAPIProtocol == "https")
            {
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
    //                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
    //                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


                // cert is stored PEM coded in file... 
                // since PEM is default, we needn't set it for PEM 
                // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
                // equest.setOpt(sslCertType);

                // set the cert for client authentication
                // "testcert.pem"
                // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
                // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
                // request.setOpt(sslCert);

                // sorry, for engine we must set the passphrase
                //   (if the key has one...)
                // const char *pPassphrase = NULL;
                // if(pPassphrase)
                //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

                // if we use a key stored in a crypto engine,
                //   we must set the key type to "ENG"
                // pKeyType  = "PEM";
                // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

                // set the private key (file or ID in engine)
                // pKeyName  = "testkey.pem";
                // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

                // set the file with the certs vaildating the server
                // *pCACertFile = "cacert.pem";
                // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

                // disconnect if we can't validate server's cert
                bool bSslVerifyPeer = false;
                curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                request.setOpt(sslVerifyPeer);

                curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                request.setOpt(sslVerifyHost);

                // request.setOpt(new curlpp::options::SslEngineDefault());                                              

            }
            request.setOpt(new curlpp::options::HttpHeader(header));

            ostringstream response;
            request.setOpt(new curlpp::options::WriteStream(&response));

            _logger->info(__FILEREF__ + "Calling facebook"
                    + ", facebookURL: " + facebookURL
                    + ", _facebookGraphAPIProtocol: " + _facebookGraphAPIProtocol
                    + ", _facebookGraphAPIHostName: " + _facebookGraphAPIHostName
                    + ", _facebookGraphAPIPort: " + to_string(_facebookGraphAPIPort)
                    + ", facebookURI: " + facebookURI
                    + ", body: " + body
            );
            request.perform();

            sResponse = response.str();
            _logger->info(__FILEREF__ + "Called facebook"
                    + ", facebookURL: " + facebookURL
                    + ", body: " + body
                    + ", sResponse: " + sResponse
            );
            
            Json::Value facebookResponseRoot;
            try
            {
                Json::CharReaderBuilder builder;
                Json::CharReader* reader = builder.newCharReader();
                string errors;

                bool parsingSuccessful = reader->parse(sResponse.c_str(),
                        sResponse.c_str() + sResponse.size(), 
                        &facebookResponseRoot, &errors);
                delete reader;

                if (!parsingSuccessful)
                {
                    string errorMessage = __FILEREF__ + "failed to parse the facebook response"
                        + ", _processorIdentifier: " + to_string(_processorIdentifier)
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", errors: " + errors
                            + ", sResponse: " + sResponse
                            ;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
            catch(...)
            {
                string errorMessage = string("facebook json response is not well format")
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(__FILEREF__ + errorMessage);

                throw runtime_error(errorMessage);
            }
            
            string field = "success";
            if (!JSONUtils::isMetadataPresent(facebookResponseRoot, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                        + ", Field: " + field
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            success = JSONUtils::asBool(facebookResponseRoot, field, false);

            if (!success)
            {
                string errorMessage = __FILEREF__ + "Post Video on Facebook failed"
                        + ", Field: " + field
                        + ", success: " + to_string(success)
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }        
        
        {
            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + "End_TaskSuccess"
                + ", errorMessage: " + ""
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                    MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                    "" // errorMessage
            );
        }
    }
    catch (curlpp::LogicError & e) 
    {
        _logger->error(__FILEREF__ + "Post video on Facebook failed (LogicError)"
            + ", facebookURL: " + facebookURL
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (curlpp::RuntimeError & e) 
    {
        _logger->error(__FILEREF__ + "Post video on Facebook failed (RuntimeError)"
            + ", facebookURL: " + facebookURL
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (runtime_error e)
    {
        _logger->error(__FILEREF__ + "Post Video on Facebook failed (runtime_error)"
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Post Video on Facebook failed (exception)"
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
}

size_t curlUploadVideoOnYouTubeCallback(char* ptr, size_t size, size_t nmemb, void *f)
{
    MMSEngineProcessor::CurlUploadYouTubeData* curlUploadData = (MMSEngineProcessor::CurlUploadYouTubeData*) f;
    
    auto logger = spdlog::get("mmsEngineService");

    int64_t currentFilePosition = curlUploadData->mediaSourceFileStream.tellg();

    /*    
    logger->info(__FILEREF__ + "curlUploadVideoOnYouTubeCallback"
        + ", currentFilePosition: " + to_string(currentFilePosition)
        + ", size: " + to_string(size)
        + ", nmemb: " + to_string(nmemb)
        + ", curlUploadData->fileSizeInBytes: " + to_string(curlUploadData->fileSizeInBytes)
    );
    */

    if(currentFilePosition + (size * nmemb) <= curlUploadData->fileSizeInBytes)
        curlUploadData->mediaSourceFileStream.read(ptr, size * nmemb);
    else
        curlUploadData->mediaSourceFileStream.read(ptr, curlUploadData->fileSizeInBytes - currentFilePosition);

    int64_t charsRead = curlUploadData->mediaSourceFileStream.gcount();
    
    return charsRead;        
};

void MMSEngineProcessor::postVideoOnYouTubeThread(
        shared_ptr<long> processorsThreadsNumber,
        string mmsAssetPathName, int64_t sizeInBytes,
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace,
        string youTubeConfigurationLabel, string youTubeTitle,
        string youTubeDescription, Json::Value youTubeTags,
        int youTubeCategoryId, string youTubePrivacy)
{

    string youTubeURL;
    string youTubeUploadURL;
    string sResponse;
    
    try
    {
        _logger->info(__FILEREF__ + "postVideoOnYouTubeThread"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", mmsAssetPathName: " + mmsAssetPathName
            + ", sizeInBytes: " + to_string(sizeInBytes)
            + ", youTubeConfigurationLabel: " + youTubeConfigurationLabel
            + ", youTubeTitle: " + youTubeTitle
            + ", youTubeDescription: " + youTubeDescription
            + ", youTubeCategoryId: " + to_string(youTubeCategoryId)
        );
        
        string youTubeAccessToken = getYouTubeAccessTokenByConfigurationLabel(
            workspace, youTubeConfigurationLabel);

        string fileFormat;
        {
            size_t extensionIndex = mmsAssetPathName.find_last_of(".");
            if (extensionIndex == string::npos)
            {
                string errorMessage = __FILEREF__ + "No fileFormat (extension of the file) found in mmsAssetPathName"
                    + ", _processorIdentifier: " + to_string(_processorIdentifier)
                    + ", ingestionJobKey: " + to_string(ingestionJobKey)
                    + ", mmsAssetPathName: " + mmsAssetPathName
                ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            fileFormat = mmsAssetPathName.substr(extensionIndex + 1);
        }
        
        /*
            POST /upload/youtube/v3/videos?uploadType=resumable&part=snippet,status,contentDetails HTTP/1.1
            Host: www.googleapis.com
            Authorization: Bearer AUTH_TOKEN
            Content-Length: 278
            Content-Type: application/json; charset=UTF-8
            X-Upload-Content-Length: 3000000
            X-Upload-Content-Type: video/*

            {
              "snippet": {
                "title": "My video title",
                "description": "This is a description of my video",
                "tags": ["cool", "video", "more keywords"],
                "categoryId": 22
              },
              "status": {
                "privacyStatus": "public",
                "embeddable": True,
                "license": "youtube"
              }
            }

            HTTP/1.1 200 OK
            Location: https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&upload_id=xa298sd_f&part=snippet,status,contentDetails
            Content-Length: 0
        */
        string videoContentType = "video/*";
        {
            youTubeURL = _youTubeDataAPIProtocol
                + "://"
                + _youTubeDataAPIHostName
                + ":" + to_string(_youTubeDataAPIPort)
                + _youTubeDataAPIUploadVideoURI;
    
            string body;
            {
                Json::Value bodyRoot;
                Json::Value snippetRoot;

                string field = "title";
                snippetRoot[field] = youTubeTitle;

                if (youTubeDescription != "")
                {
                    field = "description";
                    snippetRoot[field] = youTubeDescription;
                }

                if (youTubeTags != Json::nullValue)
                {
                    field = "tags";
                    snippetRoot[field] = youTubeTags;
                }

                if (youTubeCategoryId != -1)
                {
                    field = "categoryId";
                    snippetRoot[field] = youTubeCategoryId;
                }
                
                field = "snippet";
                bodyRoot[field] = snippetRoot;
                

                Json::Value statusRoot;

                field = "privacyStatus";
                statusRoot[field] = youTubePrivacy;

                field = "embeddable";
                statusRoot[field] = true;

                field = "license";
                statusRoot[field] = "youtube";

                field = "status";
                bodyRoot[field] = statusRoot;

                {
                    Json::StreamWriterBuilder wbuilder;
                    
                    body = Json::writeString(wbuilder, bodyRoot);
                }
            }

            list<string> headerList;

            {
                string header = "Authorization: Bearer " + youTubeAccessToken;
                headerList.push_back(header);

                header = "Content-Length: " + to_string(body.length());
                headerList.push_back(header);
                
                header = "Content-Type: application/json; charset=UTF-8";
                headerList.push_back(header);

                header = "X-Upload-Content-Length: " + to_string(sizeInBytes);
                headerList.push_back(header);
                
                header = string("X-Upload-Content-Type: ") + videoContentType;
                headerList.push_back(header);
            }                    

            curlpp::Cleanup cleaner;
            curlpp::Easy request;

            request.setOpt(new curlpp::options::PostFields(body));
            request.setOpt(new curlpp::options::PostFieldSize(body.length()));

            request.setOpt(new curlpp::options::Url(youTubeURL));
            request.setOpt(new curlpp::options::Timeout(_youTubeDataAPITimeoutInSeconds));

            if (_youTubeDataAPIProtocol == "https")
            {
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
    //                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
    //                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
    //                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


                // cert is stored PEM coded in file... 
                // since PEM is default, we needn't set it for PEM 
                // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
                // equest.setOpt(sslCertType);

                // set the cert for client authentication
                // "testcert.pem"
                // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
                // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
                // request.setOpt(sslCert);

                // sorry, for engine we must set the passphrase
                //   (if the key has one...)
                // const char *pPassphrase = NULL;
                // if(pPassphrase)
                //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

                // if we use a key stored in a crypto engine,
                //   we must set the key type to "ENG"
                // pKeyType  = "PEM";
                // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

                // set the private key (file or ID in engine)
                // pKeyName  = "testkey.pem";
                // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

                // set the file with the certs vaildating the server
                // *pCACertFile = "cacert.pem";
                // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

                // disconnect if we can't validate server's cert
                bool bSslVerifyPeer = false;
                curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                request.setOpt(sslVerifyPeer);

                curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                request.setOpt(sslVerifyHost);

                // request.setOpt(new curlpp::options::SslEngineDefault());                                              

            }
            
            for (string headerMessage: headerList)
                _logger->info(__FILEREF__ + "Adding header message: " + headerMessage);
            request.setOpt(new curlpp::options::HttpHeader(headerList));

            ostringstream response;
            request.setOpt(new curlpp::options::WriteStream(&response));

            // store response headers in the response
            // You simply have to set next option to prefix the header to the normal body output. 
            request.setOpt(new curlpp::options::Header(true)); 
            
            _logger->info(__FILEREF__ + "Calling youTube (first call)"
                    + ", youTubeURL: " + youTubeURL
                    + ", body: " + body
            );
            request.perform();

            long responseCode = curlpp::infos::ResponseCode::get(request);

            sResponse = response.str();
            _logger->info(__FILEREF__ + "Called youTube (first call)"
                    + ", youTubeURL: " + youTubeURL
                    + ", body: " + body
                    + ", responseCode: " + to_string(responseCode)
                    + ", sResponse: " + sResponse
            );
            
            if (responseCode != 200 || sResponse.find("Location: ") == string::npos)
            {
                string errorMessage = __FILEREF__ + "youTube (first call) failed"
                        + ", youTubeURL: " + youTubeURL
                        + ", body: " + body
                        + ", responseCode: " + to_string(responseCode)
                        + ", sResponse: " + sResponse
                ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            
            /* sResponse:
                HTTP/1.1 200 OK
                X-GUploader-UploadID: AEnB2UqO5ml7GRPs5AjsOSPzSGwudclcEFbyXtEK_TLWRhggwxh9gTWBdusefTgmX2ul9axk4ztG_YBWQXGtm1M42Fz9QVE4xA
                Location: https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status,contentDetails&upload_id=AEnB2UqO5ml7GRPs5AjsOSPzSGwudclcEFbyXtEK_TLWRhggwxh9gTWBdusefTgmX2ul9axk4ztG_YBWQXGtm1M42Fz9QVE4xA
                ETag: "XI7nbFXulYBIpL0ayR_gDh3eu1k/bpNRC6h7Ng2_S5XJ6YzbSMF0qXE"
                Vary: Origin
                Vary: X-Origin
                X-Goog-Correlation-Id: FGN7H2Vxp5I
                Cache-Control: no-cache, no-store, max-age=0, must-revalidate
                Pragma: no-cache
                Expires: Mon, 01 Jan 1990 00:00:00 GMT
                Date: Sun, 09 Dec 2018 09:15:41 GMT
                Content-Length: 0
                Server: UploadServer
                Content-Type: text/html; charset=UTF-8
                Alt-Svc: quic=":443"; ma=2592000; v="44,43,39,35"
             */
            
            int locationStartIndex = sResponse.find("Location: ");
            locationStartIndex += string("Location: ").length();
            int locationEndIndex = sResponse.find("\r", locationStartIndex);
            if (locationEndIndex == string::npos)
                locationEndIndex = sResponse.find("\n", locationStartIndex);
            if (locationEndIndex == string::npos)
                youTubeUploadURL = sResponse.substr(locationStartIndex);
            else
                youTubeUploadURL = sResponse.substr(locationStartIndex, locationEndIndex - locationStartIndex);
        }

        bool contentCompletelyUploaded = false;
        CurlUploadYouTubeData curlUploadData;
        curlUploadData.mediaSourceFileStream.open(mmsAssetPathName, ios::binary);
        curlUploadData.lastByteSent = -1;
        curlUploadData.fileSizeInBytes = sizeInBytes;
        while (!contentCompletelyUploaded)
        {
            /*
                // In case of the first request
                PUT UPLOAD_URL HTTP/1.1
                Authorization: Bearer AUTH_TOKEN
                Content-Length: CONTENT_LENGTH
                Content-Type: CONTENT_TYPE

                BINARY_FILE_DATA

                // in case of resuming
                PUT UPLOAD_URL HTTP/1.1
                Authorization: Bearer AUTH_TOKEN
                Content-Length: REMAINING_CONTENT_LENGTH
                Content-Range: bytes FIRST_BYTE-LAST_BYTE/TOTAL_CONTENT_LENGTH

                PARTIAL_BINARY_FILE_DATA            
            */

            {                
                list<string> headerList;
                headerList.push_back(string("Authorization: Bearer ") + youTubeAccessToken);
                if (curlUploadData.lastByteSent == -1)
                    headerList.push_back(string("Content-Length: ") + to_string(sizeInBytes));
                else
                    headerList.push_back(string("Content-Length: ") + to_string(sizeInBytes - curlUploadData.lastByteSent + 1));
                if (curlUploadData.lastByteSent == -1)
                    headerList.push_back(string("Content-Type: ") + videoContentType);
                else
                    headerList.push_back(string("Content-Range: bytes ") + to_string(curlUploadData.lastByteSent) + "-" + to_string(sizeInBytes - 1) + "/" + to_string(sizeInBytes));

                curlpp::Cleanup cleaner;
                curlpp::Easy request;

                {
                    curlpp::options::ReadFunctionCurlFunction curlUploadCallbackFunction(curlUploadVideoOnYouTubeCallback);
                    curlpp::OptionTrait<void *, CURLOPT_READDATA> curlUploadDataData(&curlUploadData);
                    request.setOpt(curlUploadCallbackFunction);
                    request.setOpt(curlUploadDataData);
           
                    bool upload = true;
                    request.setOpt(new curlpp::options::Upload(upload));
                }

                request.setOpt(new curlpp::options::CustomRequest{"PUT"});
                request.setOpt(new curlpp::options::Url(youTubeUploadURL));
                request.setOpt(new curlpp::options::Timeout(_youTubeDataAPITimeoutInSeconds));

                if (_youTubeDataAPIProtocol == "https")
                {
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
        //                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
        //                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
        //                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


                    // cert is stored PEM coded in file... 
                    // since PEM is default, we needn't set it for PEM 
                    // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                    // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
                    // equest.setOpt(sslCertType);

                    // set the cert for client authentication
                    // "testcert.pem"
                    // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
                    // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
                    // request.setOpt(sslCert);

                    // sorry, for engine we must set the passphrase
                    //   (if the key has one...)
                    // const char *pPassphrase = NULL;
                    // if(pPassphrase)
                    //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

                    // if we use a key stored in a crypto engine,
                    //   we must set the key type to "ENG"
                    // pKeyType  = "PEM";
                    // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

                    // set the private key (file or ID in engine)
                    // pKeyName  = "testkey.pem";
                    // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

                    // set the file with the certs vaildating the server
                    // *pCACertFile = "cacert.pem";
                    // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

                    // disconnect if we can't validate server's cert
                    bool bSslVerifyPeer = false;
                    curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                    request.setOpt(sslVerifyPeer);

                    curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                    request.setOpt(sslVerifyHost);

                    // request.setOpt(new curlpp::options::SslEngineDefault());                                              

                }

                for (string headerMessage: headerList)
                    _logger->info(__FILEREF__ + "Adding header message: " + headerMessage);
                request.setOpt(new curlpp::options::HttpHeader(headerList));

                _logger->info(__FILEREF__ + "Calling youTube (upload)"
                        + ", youTubeUploadURL: " + youTubeUploadURL
                );
                request.perform();

                long responseCode = curlpp::infos::ResponseCode::get(request);
                
                _logger->info(__FILEREF__ + "Called youTube (upload)"
                        + ", youTubeUploadURL: " + youTubeUploadURL
                        + ", responseCode: " + to_string(responseCode)
                );
                
                if (responseCode == 200 || responseCode == 201)
                {
                    _logger->info(__FILEREF__ + "youTube upload successful"
                            + ", youTubeUploadURL: " + youTubeUploadURL
                            + ", responseCode: " + to_string(responseCode)
                    );

                    contentCompletelyUploaded = true;
                }
                else if (responseCode == 500 
                        || responseCode == 502
                        || responseCode == 503
                        || responseCode == 504
                        )
                {                    
                    _logger->warn(__FILEREF__ + "youTube upload failed, trying to resume"
                            + ", youTubeUploadURL: " + youTubeUploadURL
                            + ", responseCode: " + to_string(responseCode)
                    );
                    
                    /*
                        PUT UPLOAD_URL HTTP/1.1
                        Authorization: Bearer AUTH_TOKEN
                        Content-Length: 0
                        Content-Range: bytes *\/CONTENT_LENGTH

                        308 Resume Incomplete
                        Content-Length: 0
                        Range: bytes=0-999999
                    */
                    {                
                        list<string> headerList;
                        headerList.push_back(string("Authorization: Bearer ") + youTubeAccessToken);
                        headerList.push_back(string("Content-Length: 0"));
                        headerList.push_back(string("Content-Range: bytes */") + to_string(sizeInBytes));

                        curlpp::Cleanup cleaner;
                        curlpp::Easy request;

                        request.setOpt(new curlpp::options::CustomRequest{"PUT"});
                        request.setOpt(new curlpp::options::Url(youTubeUploadURL));
                        request.setOpt(new curlpp::options::Timeout(_youTubeDataAPITimeoutInSeconds));

                        if (_youTubeDataAPIProtocol == "https")
                        {
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
                //                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
                //                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
                //                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


                            // cert is stored PEM coded in file... 
                            // since PEM is default, we needn't set it for PEM 
                            // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
                            // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
                            // equest.setOpt(sslCertType);

                            // set the cert for client authentication
                            // "testcert.pem"
                            // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
                            // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
                            // request.setOpt(sslCert);

                            // sorry, for engine we must set the passphrase
                            //   (if the key has one...)
                            // const char *pPassphrase = NULL;
                            // if(pPassphrase)
                            //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

                            // if we use a key stored in a crypto engine,
                            //   we must set the key type to "ENG"
                            // pKeyType  = "PEM";
                            // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

                            // set the private key (file or ID in engine)
                            // pKeyName  = "testkey.pem";
                            // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

                            // set the file with the certs vaildating the server
                            // *pCACertFile = "cacert.pem";
                            // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

                            // disconnect if we can't validate server's cert
                            bool bSslVerifyPeer = false;
                            curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
                            request.setOpt(sslVerifyPeer);

                            curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
                            request.setOpt(sslVerifyHost);

                            // request.setOpt(new curlpp::options::SslEngineDefault());                                              

                        }
                        
                        for (string headerMessage: headerList)
                            _logger->info(__FILEREF__ + "Adding header message: " + headerMessage);
                        request.setOpt(new curlpp::options::HttpHeader(headerList));

                        ostringstream response;
                        request.setOpt(new curlpp::options::WriteStream(&response));

                        // store response headers in the response
                        // You simply have to set next option to prefix the header to the normal body output. 
                        request.setOpt(new curlpp::options::Header(true));
            
                        _logger->info(__FILEREF__ + "Calling youTube check status"
                                + ", youTubeUploadURL: " + youTubeUploadURL
                                + ", _youTubeDataAPIProtocol: " + _youTubeDataAPIProtocol
                                + ", _youTubeDataAPIHostName: " + _youTubeDataAPIHostName
                                + ", _youTubeDataAPIPort: " + to_string(_youTubeDataAPIPort)
                        );
                        request.perform();

                        sResponse = response.str();
                        long responseCode = curlpp::infos::ResponseCode::get(request);

                        _logger->info(__FILEREF__ + "Called youTube check status"
                                + ", youTubeUploadURL: " + youTubeUploadURL
                                + ", responseCode: " + to_string(responseCode)
                                + ", sResponse: " + sResponse
                        );

                        if (responseCode != 308 || sResponse.find("Range: bytes=") == string::npos)
                        {   
                            // error
                            string errorMessage (__FILEREF__ + "youTube check status failed"
                                    + ", youTubeUploadURL: " + youTubeUploadURL
                                    + ", responseCode: " + to_string(responseCode)
                            );
                            _logger->error(errorMessage);

                            throw runtime_error(errorMessage);
                        }
                        
                        /* sResponse: 
                            HTTP/1.1 308 Resume Incomplete
                            X-GUploader-UploadID: AEnB2Ur8jQ5DSbXieg8krXWg0f7Bmawvf6XTacURJ7wbITyXdTv8ZeHpepaUwh6F9DB5TvBCzoS4quZMKegyo2x7H9EJOc6ozQ
                            Range: bytes=0-1572863
                            X-Range-MD5: d50bc8fc7ecc41926f841085db3909b3
                            Content-Length: 0
                            Date: Mon, 10 Dec 2018 13:09:51 GMT
                            Server: UploadServer
                            Content-Type: text/html; charset=UTF-8
                            Alt-Svc: quic=":443"; ma=2592000; v="44,43,39,35"
                        */
                        int rangeStartIndex = sResponse.find("Range: bytes=");
                        rangeStartIndex += string("Range: bytes=").length();
                        int rangeEndIndex = sResponse.find("\r", rangeStartIndex);
                        if (rangeEndIndex == string::npos)
                            rangeEndIndex = sResponse.find("\n", rangeStartIndex);
                        string rangeHeader;
                        if (rangeEndIndex == string::npos)
                            rangeHeader = sResponse.substr(rangeStartIndex);
                        else
                            rangeHeader = sResponse.substr(rangeStartIndex, rangeEndIndex - rangeStartIndex);

                        int rangeStartOffsetIndex = rangeHeader.find("-");
                        if (rangeStartOffsetIndex == string::npos)
                        {   
                            // error
                            string errorMessage (__FILEREF__ + "youTube check status failed"
                                    + ", youTubeUploadURL: " + youTubeUploadURL
                                    + ", rangeHeader: " + rangeHeader
                            );
                            _logger->error(errorMessage);

                            throw runtime_error(errorMessage);
                        }

                        _logger->info(__FILEREF__ + "Resuming"
                                + ", youTubeUploadURL: " + youTubeUploadURL
                                + ", rangeHeader: " + rangeHeader
                                + ", rangeHeader.substr(rangeStartOffsetIndex + 1): " + rangeHeader.substr(rangeStartOffsetIndex + 1)
                        );
                        curlUploadData.lastByteSent = stoll(rangeHeader.substr(rangeStartOffsetIndex + 1)) + 1;
                        curlUploadData.mediaSourceFileStream.seekg(curlUploadData.lastByteSent, ios::beg);
                    }
                }
                else
                {   
                    // error
                    string errorMessage (__FILEREF__ + "youTube upload failed"
                            + ", youTubeUploadURL: " + youTubeUploadURL
                            + ", responseCode: " + to_string(responseCode)
                    );
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
            }
        }
                
        {
            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", IngestionStatus: " + "End_TaskSuccess"
                + ", errorMessage: " + ""
            );                            
            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                    MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                    "" // errorMessage
            );
        }
    }
    catch (curlpp::LogicError & e) 
    {
        _logger->error(__FILEREF__ + "Post video on YouTube failed (LogicError)"
            + ", youTubeURL: " + youTubeURL
            + ", youTubeUploadURL: " + youTubeUploadURL
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (curlpp::RuntimeError & e) 
    {
        _logger->error(__FILEREF__ + "Post video on YouTube failed (RuntimeError)"
            + ", youTubeURL: " + youTubeURL
            + ", youTubeUploadURL: " + youTubeUploadURL
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (runtime_error e)
    {
        _logger->error(__FILEREF__ + "Post Video on YouTube failed (runtime_error)"
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Post Video on YouTube failed (exception)"
            + ", exception: " + e.what()
            + ", sResponse: " + sResponse
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
				);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
				);
		}

        return;
    }
}

string MMSEngineProcessor::getYouTubeAccessTokenByConfigurationLabel(
    shared_ptr<Workspace> workspace, string youTubeConfigurationLabel)
{
    string youTubeURL;
    string sResponse;
    
    try
    {
        string youTubeRefreshToken = _mmsEngineDBFacade->getYouTubeRefreshTokenByConfigurationLabel(
                workspace->_workspaceKey, youTubeConfigurationLabel);            

        youTubeURL = _youTubeDataAPIProtocol
            + "://"
            + _youTubeDataAPIHostName
            + ":" + to_string(_youTubeDataAPIPort)
            + _youTubeDataAPIRefreshTokenURI;

        string body =
                string("client_id=") + _youTubeDataAPIClientId
                + "&client_secret=" + _youTubeDataAPIClientSecret
                + "&refresh_token=" + youTubeRefreshToken
                + "&grant_type=refresh_token";

        list<string> headerList;

        {
            /*
            header = "Content-Length: " + to_string(body.length());
            headerList.push_back(header);
            */

            string header = "Content-Type: application/x-www-form-urlencoded";
            headerList.push_back(header);
        }

        curlpp::Cleanup cleaner;
        curlpp::Easy request;

        request.setOpt(new curlpp::options::PostFields(body));
        request.setOpt(new curlpp::options::PostFieldSize(body.length()));

        request.setOpt(new curlpp::options::Url(youTubeURL));
        request.setOpt(new curlpp::options::Timeout(_youTubeDataAPITimeoutInSeconds));

        if (_youTubeDataAPIProtocol == "https")
        {
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;
//                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;
//                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;
//                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;


            // cert is stored PEM coded in file...
            // since PEM is default, we needn't set it for PEM
            // curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
            // curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
            // equest.setOpt(sslCertType);

            // set the cert for client authentication
            // "testcert.pem"
            // curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
            // curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
            // request.setOpt(sslCert);

            // sorry, for engine we must set the passphrase
            //   (if the key has one...)
            // const char *pPassphrase = NULL;
            // if(pPassphrase)
            //  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

            // if we use a key stored in a crypto engine,
            //   we must set the key type to "ENG"
            // pKeyType  = "PEM";
            // curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

            // set the private key (file or ID in engine)
            // pKeyName  = "testkey.pem";
            // curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

            // set the file with the certs vaildating the server
            // *pCACertFile = "cacert.pem";
            // curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

            // disconnect if we can't validate server's cert
            bool bSslVerifyPeer = false;
            curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
            request.setOpt(sslVerifyPeer);

            curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
            request.setOpt(sslVerifyHost);

            // request.setOpt(new curlpp::options::SslEngineDefault());

        }
        request.setOpt(new curlpp::options::HttpHeader(headerList));

        ostringstream response;
        request.setOpt(new curlpp::options::WriteStream(&response));

        _logger->info(__FILEREF__ + "Calling youTube refresh token"
                + ", youTubeURL: " + youTubeURL
                + ", body: " + body
        );
        request.perform();

        long responseCode = curlpp::infos::ResponseCode::get(request);

        sResponse = response.str();
        _logger->info(__FILEREF__ + "Called youTube refresh token"
                + ", youTubeURL: " + youTubeURL
                + ", body: " + body
                + ", responseCode: " + to_string(responseCode)
                + ", sResponse: " + sResponse
        );

        if (responseCode != 200)
        {
            string errorMessage = __FILEREF__ + "YouTube refresh token failed"
                    + ", responseCode: " + to_string(responseCode);
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        Json::Value youTubeResponseRoot;
        try
        {
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            string errors;

            bool parsingSuccessful = reader->parse(sResponse.c_str(),
                    sResponse.c_str() + sResponse.size(),
                    &youTubeResponseRoot, &errors);
            delete reader;

            if (!parsingSuccessful)
            {
                string errorMessage = __FILEREF__ + "failed to parse the youTube response"
                        + ", errors: " + errors
                        + ", sResponse: " + sResponse
                        ;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
        catch(...)
        {
            string errorMessage = string("youTube json response is not well format")
                    + ", sResponse: " + sResponse
                    ;
            _logger->error(__FILEREF__ + errorMessage);

            throw runtime_error(errorMessage);
        }

        /*
            {
              "access_token": "ya29.GlxvBv2JUSUGmxHncG7KK118PHh4IY3ce6hbSRBoBjeXMiZjD53y3ZoeGchIkyJMb2rwQHlp-tQUZcIJ5zrt6CL2iWj-fV_2ArlAOCTy8y2B0_3KeZrbbJYgoFXCYA",
              "expires_in": 3600,
              "scope": "https://www.googleapis.com/auth/youtube https://www.googleapis.com/auth/youtube.upload",
              "token_type": "Bearer"
            }
        */
        
        string field = "access_token";
        if (!JSONUtils::isMetadataPresent(youTubeResponseRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
        
        return youTubeResponseRoot.get(field, "XXX").asString();
    }
    catch(runtime_error e)
    {
        string errorMessage = string("youTube refresh token failed")
                + ", youTubeURL: " + youTubeURL
                + ", sResponse: " + sResponse
                + ", e.what(): " + e.what()
                ;
        _logger->error(__FILEREF__ + errorMessage);

        throw runtime_error(errorMessage);
    }
    catch(exception e)
    {
        string errorMessage = string("youTube refresh token failed")
                + ", youTubeURL: " + youTubeURL
                + ", sResponse: " + sResponse
                ;
        _logger->error(__FILEREF__ + errorMessage);

        throw runtime_error(errorMessage);
    }
}

void MMSEngineProcessor::userHttpCallbackThread(
        shared_ptr<long> processorsThreadsNumber,
        int64_t ingestionJobKey, string httpProtocol, string httpHostName,
        int httpPort, string httpURI, string httpURLParameters,
        string httpMethod, long callbackTimeoutInSeconds,
        Json::Value userHeadersRoot, 
        Json::Value callbackMedatada, int maxRetries
        )
{
	int currentRetries = 0;
	bool callbackSuccessful = false;
	string errorMessage;

	while (!callbackSuccessful && currentRetries < maxRetries)
	{
		callbackSuccessful = true;
		errorMessage = "";
		currentRetries++;

		string userURL;
		string sResponse;

		try
		{
			userURL		= "";
			sResponse	= "";

			_logger->info(__FILEREF__ + "userHttpCallbackThread"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", httpProtocol: " + httpProtocol
				+ ", httpHostName: " + httpHostName
				+ ", httpPort: " + to_string(httpPort)
				+ ", httpURI: " + httpURI
				+ ", currentRetries: " + to_string(currentRetries)
				+ ", maxRetries: " + to_string(maxRetries)
			);

			userURL = httpProtocol
                + "://"
                + httpHostName
                + ":"
                + to_string(httpPort)
                + httpURI
                + httpURLParameters;

			string data;
			if (callbackMedatada.type() != Json::nullValue)
			{
				Json::StreamWriterBuilder wbuilder;

				data = Json::writeString(wbuilder, callbackMedatada);
			}

			list<string> header;

			if (httpMethod == "POST" && data != "")
				header.push_back("Content-Type: application/json");

			for (int userHeaderIndex = 0; userHeaderIndex < userHeadersRoot.size(); ++userHeaderIndex)
			{
				string userHeader = userHeadersRoot[userHeaderIndex].asString();

				header.push_back(userHeader);
			}

			curlpp::Cleanup cleaner;
			curlpp::Easy request;

			if (data != "")
			{
				if (httpMethod == "GET")
				{
					if (httpURLParameters == "")
						userURL += "?";
					else
						userURL += "&";
					userURL += ("data=" + curlpp::escape(data));
				}
				else    // POST
				{
					request.setOpt(new curlpp::options::PostFields(data));
					request.setOpt(new curlpp::options::PostFieldSize(data.length()));
				}
			}

			request.setOpt(new curlpp::options::Url(userURL));
			request.setOpt(new curlpp::options::Timeout(callbackTimeoutInSeconds));

			if (httpProtocol == "https")
			{
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;                            
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;                                          
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;                                  
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;                              
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;                                    
//                typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;                           
//                typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;                                         
//                typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;                                          
//                typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;                                          
//                typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;                                 
//                typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;                                    
//                typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;                          
//                typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;                                    


				// cert is stored PEM coded in file... 
				// since PEM is default, we needn't set it for PEM 
				// curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
				// curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
				// equest.setOpt(sslCertType);

				// set the cert for client authentication
				// "testcert.pem"
				// curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
				// curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
				// request.setOpt(sslCert);

				// sorry, for engine we must set the passphrase
				//   (if the key has one...)
				// const char *pPassphrase = NULL;
				// if(pPassphrase)
				//  curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

				// if we use a key stored in a crypto engine,
				//   we must set the key type to "ENG"
				// pKeyType  = "PEM";
				// curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

				// set the private key (file or ID in engine)
				// pKeyName  = "testkey.pem";
				// curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

				// set the file with the certs vaildating the server
				// *pCACertFile = "cacert.pem";
				// curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);

				// disconnect if we can't validate server's cert
				bool bSslVerifyPeer = false;
				curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
				request.setOpt(sslVerifyPeer);

				curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
				request.setOpt(sslVerifyHost);

				// request.setOpt(new curlpp::options::SslEngineDefault());                                              

			}
			request.setOpt(new curlpp::options::HttpHeader(header));

			ostringstream response;
			request.setOpt(new curlpp::options::WriteStream(&response));

			_logger->info(__FILEREF__ + "Calling user callback"
                + ", userURL: " + userURL
                + ", httpProtocol: " + httpProtocol
                + ", httpHostName: " + httpHostName
                + ", httpPort: " + to_string(httpPort)
                + ", httpURI: " + httpURI
                + ", httpURLParameters: " + httpURLParameters
                + ", httpProtocol: " + httpProtocol
                + ", data: " + data
				+ ", currentRetries: " + to_string(currentRetries)
				+ ", maxRetries: " + to_string(maxRetries)
			);
			request.perform();

			sResponse = response.str();
			_logger->info(__FILEREF__ + "Called user callback"
                + ", userURL: " + userURL
                + ", data: " + data
                + ", sResponse: " + sResponse
				+ ", currentRetries: " + to_string(currentRetries)
				+ ", maxRetries: " + to_string(maxRetries)
			);        

			long responseCode = curlpp::infos::ResponseCode::get(request);
			if (responseCode != 200)
			{
				string errorMessage = __FILEREF__ + "User callback failed (wrong responseCode)"
					+ ", userURL: " + userURL
					+ ", responseCode: " + to_string(responseCode)
					+ ", data: " + data
					+ ", sResponse: " + sResponse
					+ ", currentRetries: " + to_string(currentRetries)
					+ ", maxRetries: " + to_string(maxRetries)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			/*
			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_TaskSuccess"
				+ ", errorMessage: " + ""
			);                            
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
                "" // errorMessage
			);
			*/
		}
		catch (curlpp::LogicError & e) 
		{
			_logger->error(__FILEREF__ + "User Callback URL failed (LogicError)"
				+ ", userURL: " + userURL
				+ ", exception: " + e.what()
				+ ", sResponse: " + sResponse
			);

			callbackSuccessful = false;
			errorMessage = e.what();

			/*
			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + e.what()
			);
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
					MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
					e.what());
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + re.what()
				);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + ex.what()
				);
			}

			return;
			*/
		}
		catch (curlpp::RuntimeError & e) 
		{
			_logger->error(__FILEREF__ + "User Callback URL failed (RuntimeError)"
				+ ", userURL: " + userURL
				+ ", exception: " + e.what()
				+ ", sResponse: " + sResponse
			);

			callbackSuccessful = false;
			errorMessage = e.what();

			/*
			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + e.what()
			);                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
					MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
					e.what());
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + re.what()
				);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + ex.what()
				);
			}

			return;
			*/
		}
		catch (runtime_error e)
		{
			_logger->error(__FILEREF__ + "User Callback URL failed (runtime_error)"
				+ ", userURL: " + userURL
				+ ", exception: " + e.what()
				+ ", sResponse: " + sResponse
			);

			callbackSuccessful = false;
			errorMessage = e.what();

			/*
			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + e.what()
			);                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
					MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
					e.what());
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + re.what()
				);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + ex.what()
				);
			}

			return;
			*/
		}
		catch (exception e)
		{
			_logger->error(__FILEREF__ + "User Callback URL failed (exception)"
				+ ", userURL: " + userURL
				+ ", exception: " + e.what()
				+ ", sResponse: " + sResponse
			);

			callbackSuccessful = false;
			errorMessage = e.what();

			/*
			_logger->info(__FILEREF__ + "Update IngestionJob"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + e.what()
			);                            
			try
			{
				_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
					MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
					e.what());
			}
			catch(runtime_error& re)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + re.what()
				);
			}
			catch(exception ex)
			{
				_logger->info(__FILEREF__ + "Update IngestionJob failed"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", errorMessage: " + ex.what()
				);
			}

			return;
			*/
		}
	}

	if (!callbackSuccessful)
	{
		_logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", IngestionStatus: " + "End_IngestionFailure"
			+ ", errorMessage: " + errorMessage
			+ ", currentRetries: " + to_string(currentRetries)
			+ ", maxRetries: " + to_string(maxRetries)
		);
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
				MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
				errorMessage);
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", errorMessage: " + ex.what()
			);
		}
	}
	else
	{
		_logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", ingestionJobKey: " + to_string(ingestionJobKey)
			+ ", IngestionStatus: " + "End_TaskSuccess"
			+ ", errorMessage: " + ""
			+ ", currentRetries: " + to_string(currentRetries)
			+ ", maxRetries: " + to_string(maxRetries)
		);                            
		_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
			MMSEngineDBFacade::IngestionStatus::End_TaskSuccess, 
			"" // errorMessage
		);
	}
}

void MMSEngineProcessor::moveMediaSourceFileThread(
        shared_ptr<long> processorsThreadsNumber, string sourceReferenceURL, bool segmentedContent,
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace)
{

    try 
    {
        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(workspace);
        string destBinaryPathName =
			workspaceIngestionRepository
			+ "/"
			+ to_string(ingestionJobKey)
			+ "_source";
        if (segmentedContent)
			destBinaryPathName = destBinaryPathName + ".tar.gz";

        string movePrefix("move://");
        if (!(sourceReferenceURL.size() >= movePrefix.size() && 0 == sourceReferenceURL.compare(0, movePrefix.size(), movePrefix)))
        {
            string errorMessage = string("sourceReferenceURL is not a move reference")
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", sourceReferenceURL: " + sourceReferenceURL 
            ;
            
            _logger->error(__FILEREF__ + errorMessage);
            
            throw runtime_error(errorMessage);
        }
        string sourcePathName = sourceReferenceURL.substr(movePrefix.length());
                
        _logger->info(__FILEREF__ + "Moving"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", sourcePathName: " + sourcePathName
            + ", destBinaryPathName: " + destBinaryPathName
        );
        
		chrono::system_clock::time_point startMoving = chrono::system_clock::now();
        FileIO::moveFile(sourcePathName, destBinaryPathName);
        chrono::system_clock::time_point endMoving = chrono::system_clock::now();

		if (segmentedContent)
		{
			try
			{
				_logger->info(__FILEREF__ + "Calling manageTarFileInCaseOfIngestionOfSegments "
					+ ", destBinaryPathName: " + destBinaryPathName
					+ ", workspaceIngestionRepository: " + workspaceIngestionRepository
					+ ", sourcePathName: " + sourcePathName
				);
				manageTarFileInCaseOfIngestionOfSegments(ingestionJobKey,
					destBinaryPathName, workspaceIngestionRepository,
					sourcePathName);
			}
			catch(runtime_error e)
			{
				string errorMessage = string("manageTarFileInCaseOfIngestionOfSegments failed")
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
					+ ", sourceReferenceURL: " + sourceReferenceURL 
				;
           
				_logger->error(__FILEREF__ + errorMessage);
           
				throw runtime_error(errorMessage);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", movingCompleted: " + to_string(true)
			+ ", movingDuration (millisecs): " + to_string(chrono::duration_cast<chrono::milliseconds>(endMoving - startMoving).count())
        );                            
        _mmsEngineDBFacade->updateIngestionJobSourceBinaryTransferred (
            ingestionJobKey, true);
    }
    catch (runtime_error& e) 
    {
        _logger->error(__FILEREF__ + "Moving failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", sourceReferenceURL: " + sourceReferenceURL 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + ex.what()
			);
		}
        
        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Moving failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", sourceReferenceURL: " + sourceReferenceURL 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + ex.what()
			);
		}

        return;
    }
}

void MMSEngineProcessor::copyMediaSourceFileThread(
        shared_ptr<long> processorsThreadsNumber, string sourceReferenceURL, bool segmentedContent,
        int64_t ingestionJobKey, shared_ptr<Workspace> workspace)
{

    try 
    {
        string workspaceIngestionRepository = _mmsStorage->getWorkspaceIngestionRepository(workspace);
        string destBinaryPathName =
			workspaceIngestionRepository
			+ "/"
			+ to_string(ingestionJobKey)
			+ "_source";
        if (segmentedContent)
			destBinaryPathName = destBinaryPathName + ".tar.gz";

        string copyPrefix("copy://");
        if (!(sourceReferenceURL.size() >= copyPrefix.size() && 0 == sourceReferenceURL.compare(0, copyPrefix.size(), copyPrefix)))
        {
            string errorMessage = string("sourceReferenceURL is not a copy reference")
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", sourceReferenceURL: " + sourceReferenceURL 
            ;
            
            _logger->error(__FILEREF__ + errorMessage);
            
            throw runtime_error(errorMessage);
        }
        string sourcePathName = sourceReferenceURL.substr(copyPrefix.length());

        _logger->info(__FILEREF__ + "Coping"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", sourcePathName: " + sourcePathName
            + ", destBinaryPathName: " + destBinaryPathName
        );

		chrono::system_clock::time_point startCoping = chrono::system_clock::now();
        FileIO::copyFile(sourcePathName, destBinaryPathName);
        chrono::system_clock::time_point endCoping = chrono::system_clock::now();

		if (segmentedContent)
		{
			try
			{
				_logger->info(__FILEREF__ + "Calling manageTarFileInCaseOfIngestionOfSegments "
					+ ", destBinaryPathName: " + destBinaryPathName
					+ ", workspaceIngestionRepository: " + workspaceIngestionRepository
					+ ", sourcePathName: " + sourcePathName
				);
				manageTarFileInCaseOfIngestionOfSegments(ingestionJobKey,
					destBinaryPathName, workspaceIngestionRepository,
					sourcePathName);
			}
			catch(runtime_error e)
			{
				string errorMessage = string("manageTarFileInCaseOfIngestionOfSegments failed")
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
					+ ", sourceReferenceURL: " + sourceReferenceURL 
				;
           
				_logger->error(__FILEREF__ + errorMessage);
           
				throw runtime_error(errorMessage);
			}
		}

        _logger->info(__FILEREF__ + "Update IngestionJob"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", movingCompleted: " + to_string(true)
			+ ", copingDuration (millisecs): " + to_string(chrono::duration_cast<chrono::milliseconds>(endCoping - startCoping).count())
        );

        _mmsEngineDBFacade->updateIngestionJobSourceBinaryTransferred (
            ingestionJobKey, true);
    }
    catch (runtime_error& e) 
    {
        _logger->error(__FILEREF__ + "Coping failed"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", sourceReferenceURL: " + sourceReferenceURL 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + ex.what()
			);
		}
        
        return;
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "Coping failed"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", sourceReferenceURL: " + sourceReferenceURL 
            + ", exception: " + e.what()
        );

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
		try
		{
			_mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, 
                e.what());
		}
		catch(runtime_error& re)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + re.what()
			);
		}
		catch(exception ex)
		{
			_logger->info(__FILEREF__ + "Update IngestionJob failed"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", IngestionStatus: " + "End_IngestionFailure"
				+ ", errorMessage: " + ex.what()
			);
		}

        return;
    }
}

void MMSEngineProcessor::handleUpdateLiveRecorderVODEventThread (
        shared_ptr<long> processorsThreadsNumber)
{

	chrono::system_clock::time_point start = chrono::system_clock::now();

    {
        _logger->info(__FILEREF__ + "Update Live Recorder VOD started"
            + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", processors threads number: " + to_string(processorsThreadsNumber.use_count())
        );

		int64_t liveRecorderIngestionJobKey;
		string liveRecorderConfigurationLabel;

		try
		{
			int milliSecondsToSleepWaitingLock = 500;

			PersistenceLock persistenceLock(_mmsEngineDBFacade.get(),
				MMSEngineDBFacade::LockType::UpdateLiveRecorderVOD,
				_maxSecondsToWaitUpdateLiveRecorderVOD,
				_processorMMS, "UpdateLiveRecorderVOD",
				milliSecondsToSleepWaitingLock, _logger);

			vector<tuple<int64_t, int64_t, string, int, string, string, int64_t, string>> runningLiveRecordersDetails;

			_mmsEngineDBFacade->getRunningLiveRecorderVODsDetails(runningLiveRecordersDetails);

			for(tuple<int64_t, int64_t, string, int, string, string, int64_t, string> runningLiveRecorderDetails:
					runningLiveRecordersDetails)
			{
				int64_t workspaceKey;

				try
				{
					string liveRecorderVODEncodingProfileLabel;
					int64_t liveRecorderVODEncodingProfileKey;
					int liveRecorderSegmentDuration;
					string liveChunkRetention;
					int64_t liveRecorderUserKey;
					string liveRecorderApiKey;

					tie(workspaceKey, liveRecorderIngestionJobKey,
						liveRecorderVODEncodingProfileLabel, liveRecorderSegmentDuration,
						liveRecorderConfigurationLabel, liveChunkRetention, liveRecorderUserKey,
						liveRecorderApiKey) = runningLiveRecorderDetails;

					shared_ptr<Workspace> workspace = _mmsEngineDBFacade->getWorkspace(workspaceKey);

					if (liveRecorderVODEncodingProfileLabel != "")
					{
						liveRecorderVODEncodingProfileKey = _mmsEngineDBFacade->getEncodingProfileKeyByLabel(workspace,
							MMSEngineDBFacade::ContentType::Video, liveRecorderVODEncodingProfileLabel);

						_logger->info(__FILEREF__ + "Retrieve encoding profile key through his label"
							+ ", _processorMMS: " + _processorMMS
							+ ", workspaceKey: " + to_string(workspaceKey)
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
							+ ", liveRecorderVODEncodingProfileLabel: " + liveRecorderVODEncodingProfileLabel
							+ ", liveRecorderVODEncodingProfileKey: " + to_string(liveRecorderVODEncodingProfileKey)
						);
					}
					else
						liveRecorderVODEncodingProfileKey = -1;

					string liveRecorderVODUniqueName =
						liveRecorderConfigurationLabel + " (" + to_string(liveRecorderIngestionJobKey) + ")";

					bool liveRecorderVODPresent = true;
					bool warningIfMissing = true;
					int64_t liveRecorderVODMediaItemKey = -1;
					int64_t liveRecorderVODPhysicalPathKey;
					string liveRecorderVODManifestPathName;
					try
					{
						// look for the Live Recorder VOD
						pair<int64_t,MMSEngineDBFacade::ContentType> mediaItemKeyDetails =
							_mmsEngineDBFacade->getMediaItemKeyDetailsByUniqueName(workspaceKey,
							liveRecorderVODUniqueName, warningIfMissing);
						tie(liveRecorderVODMediaItemKey, ignore) = mediaItemKeyDetails;

						int64_t encodingProfileKey = -1;
						bool warningIfMissing = false;
						tuple<int64_t, string, int, string, string, int64_t, string> physicalPathDetails
							= _mmsStorage->getPhysicalPath(_mmsEngineDBFacade, liveRecorderVODMediaItemKey,
							encodingProfileKey, warningIfMissing);
						tie(liveRecorderVODPhysicalPathKey, liveRecorderVODManifestPathName,
								ignore, ignore, ignore, ignore, ignore) = physicalPathDetails;
					}
					catch(MediaItemKeyNotFound mnf)
					{
						liveRecorderVODPresent = false;
					}

					if (liveRecorderVODPresent)
					{
						_logger->info(__FILEREF__ + "Live Recorder VOD is already present, just update the manifest"
							+ ", _processorMMS: " + _processorMMS
							+ ", workspaceKey: " + to_string(workspaceKey)
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
							+ ", liveRecorderVODUniqueName: " + liveRecorderVODUniqueName
							+ ", liveRecorderVODMediaItemKey: " + to_string(liveRecorderVODMediaItemKey)
							+ ", liveRecorderVODPhysicalPathKey: " + to_string(liveRecorderVODPhysicalPathKey)
							+ ", liveRecorderVODManifestPathName: " + liveRecorderVODManifestPathName
						);

						vector<tuple<int64_t, int64_t, MMSEngineDBFacade::ContentType>> liveChunksDetails;
						bool warningIfMissing = true;
						_mmsEngineDBFacade->getMediaItemDetailsByIngestionJobKey(
							workspaceKey, liveRecorderIngestionJobKey, liveChunksDetails, warningIfMissing);

						liveRecorder_updateVOD(workspace,
							liveRecorderIngestionJobKey,
							liveRecorderVODUniqueName,
							liveRecorderVODEncodingProfileKey,
							liveRecorderSegmentDuration,
							liveRecorderConfigurationLabel,
							liveChunksDetails,
							liveRecorderVODMediaItemKey,
							liveRecorderVODPhysicalPathKey,
							liveRecorderVODManifestPathName);
					}
					else
					{
						_logger->info(__FILEREF__ + "Live Recorder VOD is NOT present"
							+ ", _processorMMS: " + _processorMMS
							+ ", workspaceKey: " + to_string(workspaceKey)
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
							+ ", liveRecorderVODUniqueName: " + liveRecorderVODUniqueName
						);

						vector<tuple<int64_t, int64_t, MMSEngineDBFacade::ContentType>> liveChunksDetails;
						bool warningIfMissing = true;
						_mmsEngineDBFacade->getMediaItemDetailsByIngestionJobKey(
							workspaceKey, liveRecorderIngestionJobKey, liveChunksDetails, warningIfMissing);

						shared_ptr<Workspace> workspace = _mmsEngineDBFacade->getWorkspace(workspaceKey);

						liveRecorder_ingestVOD(
							workspace,
							liveRecorderIngestionJobKey,
							liveRecorderVODEncodingProfileKey,
							liveRecorderSegmentDuration,
							liveRecorderConfigurationLabel,
							liveChunksDetails,
							liveChunkRetention,
							liveRecorderVODUniqueName,
							liveRecorderUserKey,
							liveRecorderApiKey);
					}
				}
				catch(runtime_error e)
				{
					_logger->error(__FILEREF__ + "UpdateLiveRecorderVOD failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", workspaceKey: " + to_string(workspaceKey)
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
						+ ", exception: " + e.what()
					);
				}
				catch(exception e)
				{
					_logger->error(__FILEREF__ + "handleUpdateLiveRecorderVODEventThread failed"
						+ ", _processorIdentifier: " + to_string(_processorIdentifier)
						+ ", workspaceKey: " + to_string(workspaceKey)
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
					);
				}
			}
		}
		catch(runtime_error e)
		{
			_logger->error(__FILEREF__ + "handleUpdateLiveRecorderVODEventThread failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
				+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
				+ ", exception: " + e.what()
			);

			// no throw since it is running in a detached thread
			// throw e;
		}
		catch(AlreadyLocked e)
		{
			_logger->warn(__FILEREF__ + "handleUpdateLiveRecorderVODEventThread already locked"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			);

			// no throw since it is running in a detached thread
			// throw e;
		}
		catch(exception e)
		{
			_logger->error(__FILEREF__ + "handleUpdateLiveRecorderVODEventThread failed"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
				+ ", liveRecorderConfigurationLabel: " + liveRecorderConfigurationLabel
			);

			// no throw since it is running in a detached thread
			// throw e;
		}

		chrono::system_clock::time_point end = chrono::system_clock::now();
		_logger->info(__FILEREF__ + "Update Live Recorder VOD finished"
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", duration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(end - start).count())
		);
    }
}

void MMSEngineProcessor::liveRecorder_ingestVOD(
	shared_ptr<Workspace> workspace,
	int64_t liveRecorderIngestionJobKey,
	int64_t liveRecorderVODEncodingProfileKey,
	int liveRecorderSegmentDuration,
	string liveRecorderConfigurationLabel,
	vector<tuple<int64_t,int64_t,MMSEngineDBFacade::ContentType>>& liveChunksDetails,
	string liveChunkRetention,
	string liveRecorderVODUniqueName,
	int64_t liveRecorderUserKey,
	string liveRecorderApiKey
	)
{
	// prepare the content to be ingested.
	// It will contain just one real ts file. Once the content is created, next update,
	// will replace the singl ts file with all the links to the ts generated files

	int tsWillBePresentAtLeastForSeconds = 20 * 60;

	// look for the ts to be used
	string tsPathFileName;
	string tsFileName;
	int64_t tsDuration;
	int64_t utcChunkStartTime;
	int64_t utcChunkEndTime;
	try
	{
		bool tsFound = false;

		for (tuple<int64_t,int64_t,MMSEngineDBFacade::ContentType> mediaItemDetail:
			liveChunksDetails)
		{
			bool validated;
			int64_t willBeRemovedInSeconds;

			int64_t tsMediaItemKey;
			int64_t tsPhysicalPathKey;

			{
				tie(tsMediaItemKey, tsPhysicalPathKey, ignore) = mediaItemDetail;

				string userData;
				try
				{
					bool warningIfMissing = true;
					tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
						moreMediaItemDetails;
					moreMediaItemDetails = _mmsEngineDBFacade->getMediaItemKeyDetails(
						workspace->_workspaceKey, tsMediaItemKey, warningIfMissing);

					tie(ignore, ignore, userData, ignore, willBeRemovedInSeconds, ignore) =
						moreMediaItemDetails;
				}
				catch(MediaItemKeyNotFound mnf)
				{
					continue;
				}

				Json::Value userDataRoot;
				try
				{
					Json::CharReaderBuilder builder;
					Json::CharReader* reader = builder.newCharReader();
					string errors;

					bool parsingSuccessful = reader->parse(userData.c_str(),
						userData.c_str() + userData.size(), 
						&userDataRoot, &errors);
					delete reader;

					if (!parsingSuccessful)
					{
						string errorMessage = __FILEREF__ + "failed to parse the metadata"
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", errors: " + errors
							+ ", userData: " + userData
						;
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}
				}
				catch(...)
				{
					string errorMessage = string("metadata json is not well format")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", userData: " + userData
					;
					_logger->error(__FILEREF__ + errorMessage);

					// throw runtime_error(errorMessage);
					continue;
				}

				/*
				* {"mmsData": {"main": true, "dataType": "liveRecordingChunk", "validated": true, "liveURLConfKey": 2, "ingestionJobKey": 9257842, "utcChunkEndTime": 1587806100, "utcChunkStartTime": 1587806040, "utcPreviousChunkStartTime": 1587805980}}
				*/

				string mmsDataField = "mmsData";
				string dataTypeField = "dataType";
				if (JSONUtils::isMetadataPresent(userDataRoot, mmsDataField)
					&& JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], dataTypeField)
				)
				{
					string dataType = (userDataRoot[mmsDataField]).get(dataTypeField, "").asString();
					if (dataType == "liveRecordingChunk")
					{
						string field = "validated";
						if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], field))
							validated = JSONUtils::asBool((userDataRoot[mmsDataField]), field, false);
						else
						{
							string errorMessage = string("metadata json is not well format")
								+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
								+ ", userData: " + userData
							;
							_logger->error(__FILEREF__ + errorMessage);

							// throw runtime_error(errorMessage);
							continue;
						}

						field = "utcChunkStartTime";
						if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], field))
							utcChunkStartTime = JSONUtils::asInt64((userDataRoot[mmsDataField]), field, -1);
						else
						{
							string errorMessage = string("metadata json is not well format")
								+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
								+ ", userData: " + userData
							;
							_logger->error(__FILEREF__ + errorMessage);

							// throw runtime_error(errorMessage);
							continue;
						}

						field = "utcChunkEndTime";
						if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], field))
							utcChunkEndTime = JSONUtils::asInt64((userDataRoot[mmsDataField]), field, -1);
						else
						{
							string errorMessage = string("metadata json is not well format")
								+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
								+ ", userData: " + userData
							;
							_logger->error(__FILEREF__ + errorMessage);

							// throw runtime_error(errorMessage);
							continue;
						}
					}
					else
					{
						string errorMessage = string("metadata json is not well format")
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", userData: " + userData
						;
						_logger->error(__FILEREF__ + errorMessage);

						// throw runtime_error(errorMessage);
						continue;
					}
				}
				else
				{
					string errorMessage = string("metadata json is not well format")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", userData: " + userData
					;
					_logger->error(__FILEREF__ + errorMessage);

					// throw runtime_error(errorMessage);
					continue;
				}
			}

			if (validated && willBeRemovedInSeconds >= tsWillBePresentAtLeastForSeconds)
			{
				if (liveRecorderVODEncodingProfileKey != -1)
				{
					_logger->info(__FILEREF__ + "retrieve the TS path file name through liveRecorderVODEncodingProfileKey"
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", liveRecorderVODEncodingProfileKey: " + to_string(liveRecorderVODEncodingProfileKey)
						);

					try
					{
						bool warningIfMissing = true;
						tuple<int64_t, string, int, string, string, int64_t, string> physicalPathDetails =
							_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, tsMediaItemKey,
							liveRecorderVODEncodingProfileKey, warningIfMissing);

						tie(tsPhysicalPathKey, tsPathFileName, ignore, ignore, tsFileName, ignore, ignore)
							= physicalPathDetails;

						tsFound = true;
					}
					catch(MediaItemKeyNotFound e)
					{
						// profileKey, the one selected for the live recorder VOD is not present yet

						continue;
					}
				}
				else
				{
					_logger->info(__FILEREF__ + "retrieve the TS path file name through tsPhysicalPathKey"
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", tsPhysicalPathKey: " + to_string(tsPhysicalPathKey)
						);

					tsFound = true;

					tuple<string, int, string, string, int64_t, string> physicalPathDetails =
						_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, tsPhysicalPathKey);

					tie(tsPathFileName, ignore, ignore, tsFileName, ignore, ignore)
						= physicalPathDetails;
				}

				tsDuration = _mmsEngineDBFacade->getMediaDurationInMilliseconds(
					tsMediaItemKey, tsPhysicalPathKey);

				break;
			}
			else
			{
				continue;
			}
		}

		if (!tsFound)
		{
			string errorMessage = string("No TS found to ingest the live recorder VOD")
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			;
			_logger->warn(__FILEREF__ + errorMessage);

			return;
		}
	}
	catch(runtime_error e)
	{
		string errorMessage = string("look for one TS to build the live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", e.what: " + e.what()
		;
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}
	catch(exception e)
	{
		string errorMessage = string("look for one TS to build the live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
		;
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}

	// we have the TS, let's build the live recorder VOD (the directory,
	// TS, manifest file)
	string liveRecorderVODName;
	string stagingLiveRecorderVODPathName;
	string tarGzStagingLiveRecorderVODPathName;
	try
	{
		{
			liveRecorderVODName = to_string(liveRecorderIngestionJobKey)
				+ "_liveRecorderVOD"
			;

			bool removeLinuxPathIfExist = false;
			bool neededForTranscoder = false;
			stagingLiveRecorderVODPathName = _mmsStorage->getStagingAssetPathName(  
				neededForTranscoder,
				workspace->_directoryName,
				to_string(liveRecorderIngestionJobKey),  // directoryNamePrefix,
				"/",    // relativePath,
				liveRecorderVODName,	// filename
				-1, // mediaItemKey, not used because FileName is not ""
				-1, // physicalPathKey, not used because FileName is not ""
				removeLinuxPathIfExist);

			// in our case 'fileName' is a directory, so we have to create it
			bool noErrorIfExists = true;
			bool recursive = true;
			FileIO::createDirectory(
				stagingLiveRecorderVODPathName,
				S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IXGRP |
				S_IROTH | S_IXOTH, noErrorIfExists, recursive);
		}

		// copy TS file into the stagingLiveRecorderVODPathName
		{
			string destTSPathFileName = stagingLiveRecorderVODPathName + "/" + tsFileName;

			_logger->info(__FILEREF__ + "Coping"
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
				+ ", tsPathFileName,: " + tsPathFileName
				+ ", destTSPathFileName: " + destTSPathFileName
			);

			chrono::system_clock::time_point startCoping = chrono::system_clock::now();
			FileIO::copyFile(tsPathFileName, destTSPathFileName);
			chrono::system_clock::time_point endCoping = chrono::system_clock::now();

			_logger->info(__FILEREF__ + "TS copied"
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
				+ ", copingDuration (millisecs): "
					+ to_string(chrono::duration_cast<chrono::milliseconds>(endCoping - startCoping).count())
			);
		}

		// save manifest file
		{
			string endLine = "\n";

			char	pTsDuration [64];
			sprintf(pTsDuration, "%0.6f", ((float) tsDuration) / 1000);

			string manifestContent =
				"#EXTM3U" + endLine
				+ "#EXT-X-VERSION:3" + endLine
				+ "#EXT-X-TARGETDURATION:" + to_string(liveRecorderSegmentDuration) + endLine
				+ "#EXT-X-MEDIA-SEQUENCE:0" + endLine
				+ "#EXTINF:" + string(pTsDuration) + "," + endLine
				+ tsFileName + endLine
				+ "#EXT-X-ENDLIST" + endLine
			;

			string manifestPathFileName = stagingLiveRecorderVODPathName + "/" + liveRecorderVODName + ".m3u8";

			ofstream ofManifestFile(manifestPathFileName);
			ofManifestFile << manifestContent;
		}

		{
			string executeCommand;
			try
			{
				tarGzStagingLiveRecorderVODPathName = stagingLiveRecorderVODPathName + ".tar.gz";

				size_t endOfPathIndex = stagingLiveRecorderVODPathName.find_last_of("/");
				if (endOfPathIndex == string::npos)
				{
					string errorMessage = string("No stagingLiveRecorderVODDirectory found")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", stagingLiveRecorderVODPathName: " + stagingLiveRecorderVODPathName 
					;
					_logger->error(__FILEREF__ + errorMessage);
          
					throw runtime_error(errorMessage);
				}
				string stagingLiveRecorderVODDirectory =
					stagingLiveRecorderVODPathName.substr(0, endOfPathIndex);

				executeCommand =
					"tar cfz " + tarGzStagingLiveRecorderVODPathName
					+ " -C " + stagingLiveRecorderVODDirectory
					+ " " + liveRecorderVODName;
				_logger->info(__FILEREF__ + "Start tar command "
					+ ", executeCommand: " + executeCommand
				);
				chrono::system_clock::time_point startTar = chrono::system_clock::now();
				int executeCommandStatus = ProcessUtility::execute(executeCommand);
				chrono::system_clock::time_point endTar = chrono::system_clock::now();
				_logger->info(__FILEREF__ + "End tar command "
					+ ", executeCommand: " + executeCommand
					+ ", tarDuration (millisecs): " + to_string(chrono::duration_cast<chrono::milliseconds>(endTar - startTar).count())
				);
				if (executeCommandStatus != 0)
				{
					string errorMessage = string("ProcessUtility::execute failed")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", executeCommandStatus: " + to_string(executeCommandStatus) 
						+ ", executeCommand: " + executeCommand 
					;
					_logger->error(__FILEREF__ + errorMessage);
          
					throw runtime_error(errorMessage);
				}

				{
					_logger->info(__FILEREF__ + "Remove directory"
						+ ", stagingLiveRecorderVODPathName: " + stagingLiveRecorderVODPathName
					);
					bool removeRecursively = true;
					FileIO::removeDirectory(stagingLiveRecorderVODPathName, removeRecursively);
				}
			}
			catch(runtime_error e)
			{
				string errorMessage = string("tar command failed")
					+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
					+ ", executeCommand: " + executeCommand 
				;
				_logger->error(__FILEREF__ + errorMessage);
         
				throw runtime_error(errorMessage);
			}
		}
	}
	catch(runtime_error e)
	{
		string errorMessage = string("build the live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", e.what: " + e.what()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		if (stagingLiveRecorderVODPathName != ""
			&& FileIO::directoryExisting(stagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove directory"
				+ ", stagingLiveRecorderVODPathName: " + stagingLiveRecorderVODPathName
			);
			bool removeRecursively = true;
			FileIO::removeDirectory(stagingLiveRecorderVODPathName, removeRecursively);
		}

		throw runtime_error(errorMessage);
	}
	catch(exception e)
	{
		string errorMessage = string("build the live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		if (stagingLiveRecorderVODPathName != ""
			&& FileIO::directoryExisting(stagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove directory"
				+ ", stagingLiveRecorderVODPathName: " + stagingLiveRecorderVODPathName
			);
			bool removeRecursively = true;
			FileIO::removeDirectory(stagingLiveRecorderVODPathName, removeRecursively);
		}

		throw runtime_error(errorMessage);
	}

	// build workflow
	string workflowMetadata;
	try
	{
		/*
		{
        	"Label": "<workflow label>",
        	"Type": "Workflow",
        	"Task": {
                "Label": "<task label 1>",
                "Type": "Add-Content"
                "Parameters": {
                        "FileFormat": "m3u8",
                        "Ingester": "Giuliano",
                        "SourceURL": "move:///abc...."
                },
        	}
		}
		*/
		Json::Value mmsDataRoot;

		// 2020-04-28: set it to liveRecordingChunk to avoid to be visible into the GUI (view MediaItems).
		//	This is because this MediaItem is not completed yet
		string field = "dataType";
		mmsDataRoot[field] = "liveRecordingChunk";

		field = "firstUtcChunkStartTime";
		mmsDataRoot[field] = utcChunkStartTime;

		field = "lastUtcChunkEndTime";
		mmsDataRoot[field] = utcChunkEndTime;

		field = "configurationLabel";
		mmsDataRoot[field] = liveRecorderConfigurationLabel;

		string sUtcChunkEndTime;
		{
			char	lastUtcChunkEndTime_str [64];
			tm		tmDateTime;


			// from utc to local time
			localtime_r (&utcChunkEndTime, &tmDateTime);

			sprintf (lastUtcChunkEndTime_str,
				"%04d-%02d-%02d %02d:%02d:%02d",
				tmDateTime. tm_year + 1900,
				tmDateTime. tm_mon + 1,
				tmDateTime. tm_mday,
				tmDateTime. tm_hour,
				tmDateTime. tm_min,
				tmDateTime. tm_sec);

			sUtcChunkEndTime = lastUtcChunkEndTime_str;

			field = "lastUtcChunkEndTime_str";
			mmsDataRoot[field] = sUtcChunkEndTime;
		}

		Json::Value userDataRoot;

		field = "mmsData";
		userDataRoot[field] = mmsDataRoot;

		Json::Value addContentRoot;

		string addContentLabel = liveRecorderConfigurationLabel + " up to " + sUtcChunkEndTime;

		field = "Label";
		addContentRoot[field] = addContentLabel;

		field = "Type";
		addContentRoot[field] = "Add-Content";

		Json::Value addContentParametersRoot;

		field = "FileFormat";
		addContentParametersRoot[field] = "m3u8";

		string sourceURL = string("copy") + "://" + tarGzStagingLiveRecorderVODPathName;
        field = "SourceURL";
        addContentParametersRoot[field] = sourceURL;

		field = "Ingester";
		addContentParametersRoot[field] = "Live Recorder Task";

		field = "Title";
		addContentParametersRoot[field] = addContentLabel;

		field = "UniqueName";
		addContentParametersRoot[field] = liveRecorderVODUniqueName;

		field = "Retention";
		addContentParametersRoot[field] = liveChunkRetention;

		field = "UserData";
		addContentParametersRoot[field] = userDataRoot;

		if (_liveRecorderVODImageMediaItemKey != -1)
		{
			Json::Value crossReferenceRoot;

			field = "Type";
			crossReferenceRoot[field] = "VideoOfImage";

			field = "MediaItemKey";
			crossReferenceRoot[field] = _liveRecorderVODImageMediaItemKey;

			field = "CrossReference";
			addContentParametersRoot[field] = crossReferenceRoot;
		}

		field = "Parameters";
		addContentRoot[field] = addContentParametersRoot;


		Json::Value workflowRoot;

		field = "Label";
		workflowRoot[field] = addContentLabel;

		field = "Type";
		workflowRoot[field] = "Workflow";

		field = "Task";
		workflowRoot[field] = addContentRoot;

   		{
       		Json::StreamWriterBuilder wbuilder;
       		workflowMetadata = Json::writeString(wbuilder, workflowRoot);
   		}

		_logger->info(__FILEREF__ + "Live Recorder VOD Workflow metadata generated"
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", " + addContentLabel + ", "
		);
	}
	catch (runtime_error e)
	{
		string errorMessage = string("build workflowMetadata live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", e.what: " + e.what()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		throw runtime_error(errorMessage);
	}
	catch (exception e)
	{
		string errorMessage = string("build workflowMetadata live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", e.what: " + e.what()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		throw runtime_error(errorMessage);
	}

	// ingest the Live Recorder VOD
	ostringstream response;
	string mmsAPIURL;
	try
	{
		mmsAPIURL =
			_mmsAPIProtocol
			+ "://"
			+ _mmsAPIHostname + ":"
			+ to_string(_mmsAPIPort)
			+ _mmsAPIIngestionURI
           ;

		list<string> header;

		header.push_back("Content-Type: application/json");
		{
			// string userPasswordEncoded = Convert::base64_encode(_mmsAPIUser + ":" + _mmsAPIPassword);
			string userPasswordEncoded = Convert::base64_encode(to_string(liveRecorderUserKey) + ":" + liveRecorderApiKey);
			string basicAuthorization = string("Authorization: Basic ") + userPasswordEncoded;

			header.push_back(basicAuthorization);
		}

		curlpp::Cleanup cleaner;
		curlpp::Easy request;

		// Setting the URL to retrive.
		request.setOpt(new curlpp::options::Url(mmsAPIURL));

		if (_mmsAPIProtocol == "https")
		{
			/*
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLCERTPASSWD> SslCertPasswd;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEY> SslKey;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYTYPE> SslKeyType;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLKEYPASSWD> SslKeyPasswd;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSLENGINE> SslEngine;
			typedef curlpp::NoValueOptionTrait<CURLOPT_SSLENGINE_DEFAULT> SslEngineDefault;
			typedef curlpp::OptionTrait<long, CURLOPT_SSLVERSION> SslVersion;
			typedef curlpp::OptionTrait<std::string, CURLOPT_CAINFO> CaInfo;
			typedef curlpp::OptionTrait<std::string, CURLOPT_CAPATH> CaPath;
			typedef curlpp::OptionTrait<std::string, CURLOPT_RANDOM_FILE> RandomFile;
			typedef curlpp::OptionTrait<std::string, CURLOPT_EGDSOCKET> EgdSocket;
			typedef curlpp::OptionTrait<std::string, CURLOPT_SSL_CIPHER_LIST> SslCipherList;
			typedef curlpp::OptionTrait<std::string, CURLOPT_KRB4LEVEL> Krb4Level;
			*/

			/*
			// cert is stored PEM coded in file... 
			// since PEM is default, we needn't set it for PEM 
			// curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
			curlpp::OptionTrait<string, CURLOPT_SSLCERTTYPE> sslCertType("PEM");
			equest.setOpt(sslCertType);

			// set the cert for client authentication
			// "testcert.pem"
			// curl_easy_setopt(curl, CURLOPT_SSLCERT, pCertFile);
			curlpp::OptionTrait<string, CURLOPT_SSLCERT> sslCert("cert.pem");
			request.setOpt(sslCert);
			*/

			/*
			// sorry, for engine we must set the passphrase
			//   (if the key has one...)
			// const char *pPassphrase = NULL;
			if(pPassphrase)
			curl_easy_setopt(curl, CURLOPT_KEYPASSWD, pPassphrase);

			// if we use a key stored in a crypto engine,
			//   we must set the key type to "ENG"
			// pKeyType  = "PEM";
			curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, pKeyType);

			// set the private key (file or ID in engine)
			// pKeyName  = "testkey.pem";
			curl_easy_setopt(curl, CURLOPT_SSLKEY, pKeyName);

			// set the file with the certs vaildating the server
			// *pCACertFile = "cacert.pem";
			curl_easy_setopt(curl, CURLOPT_CAINFO, pCACertFile);
			*/

			// disconnect if we can't validate server's cert
			bool bSslVerifyPeer = false;
			curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYPEER> sslVerifyPeer(bSslVerifyPeer);
			request.setOpt(sslVerifyPeer);
              
			curlpp::OptionTrait<bool, CURLOPT_SSL_VERIFYHOST> sslVerifyHost(0L);
			request.setOpt(sslVerifyHost);
              
			// request.setOpt(new curlpp::options::SslEngineDefault());                                              
		}

		request.setOpt(new curlpp::options::HttpHeader(header));
		request.setOpt(new curlpp::options::PostFields(workflowMetadata));
		request.setOpt(new curlpp::options::PostFieldSize(workflowMetadata.length()));

		request.setOpt(new curlpp::options::WriteStream(&response));

		_logger->info(__FILEREF__ + "Ingesting Live Recorder VOD workflow"
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey) 
			+ ", mmsAPIURL: " + mmsAPIURL
			+ ", workflowMetadata: " + workflowMetadata
		);
		chrono::system_clock::time_point startIngesting = chrono::system_clock::now();
		request.perform();
		chrono::system_clock::time_point endIngesting = chrono::system_clock::now();

		string sResponse = response.str();
		// LF and CR create problems to the json parser...
		while (sResponse.back() == 10 || sResponse.back() == 13)
			sResponse.pop_back();

		long responseCode = curlpp::infos::ResponseCode::get(request);
		if (responseCode == 201)
		{
			string message = __FILEREF__ + "Ingested Live Recorder VOD workflow response"
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey) 
				+ ", ingestingDuration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(endIngesting - startIngesting).count())
				+ ", workflowMetadata: " + workflowMetadata
				+ ", sResponse: " + sResponse
				;
			_logger->info(message);
		}
		else
		{
			string message = __FILEREF__ + "Ingested Live Recorder VOD workflow response"
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey) 
				+ ", ingestingDuration (secs): " + to_string(chrono::duration_cast<chrono::seconds>(endIngesting - startIngesting).count())
				+ ", workflowMetadata: " + workflowMetadata
				+ ", sResponse: " + sResponse
				+ ", responseCode: " + to_string(responseCode)
				;
			_logger->error(message);

			throw runtime_error(message);
		}
	}
	catch (curlpp::LogicError& e)
	{
		string errorMessage = string("ingest live recorder VOD failed (LogicError)")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", mmsAPIURL: " + mmsAPIURL
			+ ", workflowMetadata: " + workflowMetadata
			+ ", e.what: " + e.what()
			+ ", response.str(): " + response.str()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		throw runtime_error(errorMessage);
	}
	catch (curlpp::RuntimeError& e)
	{
		string errorMessage = string("ingest live recorder VOD failed (RuntimeError)")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", mmsAPIURL: " + mmsAPIURL
			+ ", workflowMetadata: " + workflowMetadata
			+ ", e.what: " + e.what()
			+ ", response.str(): " + response.str()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		throw runtime_error(errorMessage);
	}
	catch (runtime_error e)
	{
		string errorMessage = string("ingest live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", mmsAPIURL: " + mmsAPIURL
			+ ", workflowMetadata: " + workflowMetadata
			+ ", e.what: " + e.what()
			+ ", response.str(): " + response.str()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		throw runtime_error(errorMessage);
	}
	catch (exception e)
	{
		string errorMessage = string("ingest live recorder VOD failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", mmsAPIURL: " + mmsAPIURL
			+ ", workflowMetadata: " + workflowMetadata
			+ ", response.str(): " + response.str()
		;
		_logger->error(__FILEREF__ + errorMessage);

		if (tarGzStagingLiveRecorderVODPathName != ""
			&& FileIO::fileExisting(tarGzStagingLiveRecorderVODPathName))
		{
			_logger->info(__FILEREF__ + "Remove"
				+ ", tarGzStagingLiveRecorderVODPathName: " + tarGzStagingLiveRecorderVODPathName
			);
			FileIO::remove(tarGzStagingLiveRecorderVODPathName);
		}

		throw runtime_error(errorMessage);
	}
}

void MMSEngineProcessor::liveRecorder_updateVOD(
	shared_ptr<Workspace> workspace,
	int64_t liveRecorderIngestionJobKey,
	string liveRecorderVODUniqueName,
	int64_t liveRecorderVODEncodingProfileKey,
	int liveRecorderSegmentDuration,
	string liveRecorderConfigurationLabel,
	vector<tuple<int64_t,int64_t,MMSEngineDBFacade::ContentType>>& liveChunksDetails,
	int64_t liveRecorderVODMediaItemKey,
	int64_t liveRecorderVODPhysicalPathKey,
	string liveRecorderVODManifestPathName
	)
{
	// look for all the TS to be used (vector tsToBeUsed)
	// build the new manifest in memory
	// save the new manifest having references to the ts files

	int tsWillBePresentAtLeastForSeconds = 20 * 60;

	// look for the ts to be used
	vector<tuple<int64_t, int64_t, int, string, string, int64_t>> tsToBeUsed;
	try
	{
		for (tuple<int64_t,int64_t,MMSEngineDBFacade::ContentType> mediaItemDetail:
			liveChunksDetails)
		{
			bool validated;
			int64_t willBeRemovedInSeconds;
			int64_t currentUtcChunkStartTime;
			int64_t currentUtcChunkEndTime;

			int64_t tsPhysicalPathKey;
			int64_t tsMediaItemKey;

			{
				tie(tsMediaItemKey, tsPhysicalPathKey, ignore) = mediaItemDetail;

				string userData;
				try
				{
					bool warningIfMissing = true;
					tuple<MMSEngineDBFacade::ContentType, string, string, string, int64_t, int64_t>
						moreMediaItemDetails;
					moreMediaItemDetails = _mmsEngineDBFacade->getMediaItemKeyDetails(
						workspace->_workspaceKey, tsMediaItemKey, warningIfMissing);

					tie(ignore, ignore, userData, ignore, willBeRemovedInSeconds, ignore) =
						moreMediaItemDetails;
				}
				catch(MediaItemKeyNotFound mnf)
				{
					continue;
				}

				Json::Value userDataRoot;
				try
				{
					Json::CharReaderBuilder builder;
					Json::CharReader* reader = builder.newCharReader();
					string errors;

					bool parsingSuccessful = reader->parse(userData.c_str(),
						userData.c_str() + userData.size(), 
						&userDataRoot, &errors);
					delete reader;

					if (!parsingSuccessful)
					{
						string errorMessage = __FILEREF__ + "failed to parse the metadata"
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", errors: " + errors
							+ ", userData: " + userData
						;
						_logger->error(errorMessage);

						throw runtime_error(errorMessage);
					}
				}
				catch(...)
				{
					string errorMessage = string("metadata json is not well format")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", userData: " + userData
					;
					_logger->error(__FILEREF__ + errorMessage);

					// throw runtime_error(errorMessage);
					continue;
				}

				/*
				* {"mmsData": {"main": true, "dataType": "liveRecordingChunk", "validated": true, "liveURLConfKey": 2, "ingestionJobKey": 9257842, "utcChunkEndTime": 1587806100, "utcChunkStartTime": 1587806040, "utcPreviousChunkStartTime": 1587805980}}
				*/

				string mmsDataField = "mmsData";
				string dataTypeField = "dataType";
				if (JSONUtils::isMetadataPresent(userDataRoot, mmsDataField)
					&& JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], dataTypeField)
				)
				{
					string dataType = (userDataRoot[mmsDataField]).get(dataTypeField, "").asString();
					if (dataType == "liveRecordingChunk")
					{
						string field = "validated";
						if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], field))
							validated = JSONUtils::asBool((userDataRoot[mmsDataField]), field, false);
						else
						{
							string errorMessage = string("metadata json is not well format")
								+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
								+ ", tsMediaItemKey: " + to_string(tsMediaItemKey)
								+ ", tsPhysicalPathKey: " + to_string(tsPhysicalPathKey)
								+ ", userData: " + userData
							;
							// 2020-04-28: in case of high availability, no validated field is present at the beggining
							//	So set it as warn
							_logger->warn(__FILEREF__ + errorMessage);

							// throw runtime_error(errorMessage);
							continue;
						}

						field = "utcChunkStartTime";
						if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], field))
						{
							currentUtcChunkStartTime = JSONUtils::asInt64((userDataRoot[mmsDataField]), field, -1);
						}
						else
						{
							string errorMessage = string("metadata json is not well format")
								+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
								+ ", userData: " + userData
							;
							_logger->error(__FILEREF__ + errorMessage);

							// throw runtime_error(errorMessage);
							continue;
						}

						field = "utcChunkEndTime";
						if (JSONUtils::isMetadataPresent(userDataRoot[mmsDataField], field))
						{
							currentUtcChunkEndTime = JSONUtils::asInt64((userDataRoot[mmsDataField]), field, -1);
						}
						else
						{
							string errorMessage = string("metadata json is not well format")
								+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
								+ ", userData: " + userData
							;
							_logger->error(__FILEREF__ + errorMessage);

							// throw runtime_error(errorMessage);
							continue;
						}
					}
					else
					{
						string errorMessage = string("metadata json is not well format")
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", userData: " + userData
						;
						_logger->error(__FILEREF__ + errorMessage);

						// throw runtime_error(errorMessage);
						continue;
					}
				}
				else
				{
					string errorMessage = string("metadata json is not well format")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", userData: " + userData
					;
					_logger->error(__FILEREF__ + errorMessage);

					// throw runtime_error(errorMessage);
					continue;
				}
			}

			if (validated && willBeRemovedInSeconds >= tsWillBePresentAtLeastForSeconds)
			{
				int mmsPartitionNumber;
				string relativePath;
				string fileName;

				if (liveRecorderVODEncodingProfileKey != -1)
				{
					_logger->info(__FILEREF__ + "retrieve the TS path file name through liveRecorderVODEncodingProfileKey"
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", liveRecorderVODEncodingProfileKey: " + to_string(liveRecorderVODEncodingProfileKey)
						);

					try
					{
						bool warningIfMissing = true;
						tuple<int64_t, string, int, string, string, int64_t, string> physicalPathDetails =
							_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, tsMediaItemKey,
							liveRecorderVODEncodingProfileKey, warningIfMissing);

						tie(tsPhysicalPathKey, ignore, mmsPartitionNumber, relativePath, fileName, ignore, ignore)
							= physicalPathDetails;
					}
					catch(MediaItemKeyNotFound e)
					{
						// profileKey, the one selected for the live recorder VOD is not present yet

						continue;
					}
				}
				else
				{
					_logger->info(__FILEREF__ + "retrieve the TS path file name through tsPhysicalPathKey"
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", tsPhysicalPathKey: " + to_string(tsPhysicalPathKey)
						);

					tuple<string, int, string, string, int64_t, string> physicalPathDetails =
						_mmsStorage->getPhysicalPath(_mmsEngineDBFacade, tsPhysicalPathKey);

					tie(ignore, mmsPartitionNumber, relativePath, fileName, ignore, ignore)
						= physicalPathDetails;
				}

				int64_t tsDuration = _mmsEngineDBFacade->getMediaDurationInMilliseconds(
					tsMediaItemKey, tsPhysicalPathKey);

				tsToBeUsed.push_back(
						make_tuple(currentUtcChunkStartTime, currentUtcChunkEndTime,
							mmsPartitionNumber, relativePath, fileName, tsDuration)
						);
			}
			else
			{
				continue;
			}
		}
	}
	catch(runtime_error e)
	{
		string errorMessage = string("retrieve all the TS to update the live recorder manifest failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", e.what: " + e.what()
		;
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}
	catch(exception e)
	{
		string errorMessage = string("retrieve all the TS to update the live recorder manifest failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
		;
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}

	int maxTSToBeUsed = 2000;
	if (tsToBeUsed.size() > maxTSToBeUsed)
		tsToBeUsed.erase(tsToBeUsed.begin(), tsToBeUsed.begin() + (tsToBeUsed.size() - maxTSToBeUsed));

	if (tsToBeUsed.size() == 0)
	{
		string errorMessage = string("No chunks found")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", liveRecorderVODManifestPathName: " + liveRecorderVODManifestPathName 
			+ ", tsToBeUsed.size: " + to_string(tsToBeUsed.size())
		;
		_logger->error(__FILEREF__ + errorMessage);
       
		throw runtime_error(errorMessage);
	}

	int64_t firstUtcChunkStartTime;
	int64_t lastUtcChunkEndTime;
	{
		tuple<int64_t, int64_t, int, string, string, int64_t> tsInfo = tsToBeUsed[0];
		tie(firstUtcChunkStartTime, ignore, ignore, ignore, ignore, ignore) = tsInfo;

		tsInfo = tsToBeUsed[tsToBeUsed.size() - 1];
		tie(ignore, lastUtcChunkEndTime, ignore, ignore, ignore, ignore) = tsInfo;
	}

	// build and update the new manifest file
	try
	{
		string liveRecorderVODDirectory;
		{
			size_t endOfPathIndex = liveRecorderVODManifestPathName.find_last_of("/");
			if (endOfPathIndex == string::npos)
			{
				string errorMessage = string("No liveRecorderVODDirectory found")
					+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
					+ ", liveRecorderVODManifestPathName: " + liveRecorderVODManifestPathName 
				;
				_logger->error(__FILEREF__ + errorMessage);
         
				throw runtime_error(errorMessage);
			}
			liveRecorderVODDirectory = liveRecorderVODManifestPathName.substr(0, endOfPathIndex);
		}

		// build new manifest file
		string manifestContent;
		{
			string endLine = "\n";

			manifestContent =
				"#EXTM3U" + endLine
				+ "#EXT-X-VERSION:3" + endLine
				+ "#EXT-X-TARGETDURATION:" + to_string(liveRecorderSegmentDuration) + endLine
				+ "#EXT-X-MEDIA-SEQUENCE:0" + endLine
			;

			for (tuple<int64_t, int64_t, int, string, string, int64_t> tsInfo: tsToBeUsed)
			{
				int64_t currentUtcChunkStartTime;
				int64_t currentUtcChunkEndTime;
				int mmsPartitionNumber;
				string relativePath;
				string fileName;
				int64_t tsDuration;
				
				tie(currentUtcChunkStartTime, currentUtcChunkEndTime,
						mmsPartitionNumber, relativePath, fileName, tsDuration) = tsInfo;

				{
					char	pTsDuration [64];
					sprintf(pTsDuration, "%0.6f", ((float) tsDuration) / 1000);

					manifestContent +=
						("#EXTINF:" + string(pTsDuration) + "," + endLine);
				}

				{
					char pMMSPartitionName [64];
					sprintf(pMMSPartitionName, "MMS_%04lu",
						(unsigned long) mmsPartitionNumber);

					if (!FileIO::fileExisting(liveRecorderVODDirectory + "/" + fileName))
					{
						string relativePathName = string("../../../../../../")
							+ string(pMMSPartitionName)
							+ "/"
							+ workspace->_directoryName
							+ relativePath
							+ fileName;
						string linkPathName = liveRecorderVODDirectory + "/" + fileName;
						Boolean_t bReplaceItIfExist = true;
						_logger->info(__FILEREF__ + "create link"
							+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
							+ ", linkPathName: " + linkPathName 
							+ ", relativePathName: " + relativePathName
						);
						FileIO:: createLink (relativePathName.c_str(), 
							linkPathName.c_str(), bReplaceItIfExist);
					}

					// manifestContent += (relativePathName + endLine);
					manifestContent += (fileName + endLine);
				}
			}

			manifestContent += ("#EXT-X-ENDLIST" + endLine);

			_logger->info(__FILEREF__ + "update the Live Recorder VOD manifest file"
				+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey) 
				+ ", manifestContent: " + manifestContent
			);

			ofstream ofManifestFile(liveRecorderVODManifestPathName);
			ofManifestFile << manifestContent;
		}

		// retrieve updated information
		string sFirstUtcChunkStartTime;
		string sLastUtcChunkEndTime;
		string title;
		int64_t durationInMilliSeconds = -1;
		long bitRate = -1;
		unsigned long long sizeInBytes;
		vector<tuple<int, int64_t, string, string, int, int, string, long>> videoTracks;
		vector<tuple<int, int64_t, string, long, int, long, string>> audioTracks;
		{
			{
				char	firstUtcChunkStartTime_str [64];
				tm		tmDateTime;

				// from utc to local time
				localtime_r (&firstUtcChunkStartTime, &tmDateTime);

				sprintf (firstUtcChunkStartTime_str,
					"%04d-%02d-%02d %02d:%02d:%02d",
					tmDateTime. tm_year + 1900,
					tmDateTime. tm_mon + 1,
					tmDateTime. tm_mday,
					tmDateTime. tm_hour,
					tmDateTime. tm_min,
					tmDateTime. tm_sec);

				sFirstUtcChunkStartTime = firstUtcChunkStartTime_str;
			}

			{
				char	lastUtcChunkEndTime_str [64];
				tm		tmDateTime;

				// from utc to local time
				localtime_r (&lastUtcChunkEndTime, &tmDateTime);

				sprintf (lastUtcChunkEndTime_str,
					"%04d-%02d-%02d %02d:%02d:%02d",
					tmDateTime. tm_year + 1900,
					tmDateTime. tm_mon + 1,
					tmDateTime. tm_mday,
					tmDateTime. tm_hour,
					tmDateTime. tm_min,
					tmDateTime. tm_sec);

				sLastUtcChunkEndTime = lastUtcChunkEndTime_str;
			}
			title = liveRecorderConfigurationLabel + " from " + sFirstUtcChunkStartTime + " to " + sLastUtcChunkEndTime;

			{
				pair<int64_t, long> mediaInfoDetails;

				FFMpeg ffmpeg (_configuration, _logger);
				mediaInfoDetails = ffmpeg.getMediaInfo(liveRecorderVODManifestPathName,
					videoTracks, audioTracks);

				tie(durationInMilliSeconds, bitRate) = mediaInfoDetails;
			}

			{
				/*
				size_t endOfPathIndex = liveRecorderVODManifestPathName.find_last_of("/");
				if (endOfPathIndex == string::npos)
				{
					string errorMessage = string("No liveRecorderVODDirectory found")
						+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
						+ ", liveRecorderVODManifestPathName: " + liveRecorderVODManifestPathName 
					;
					_logger->error(__FILEREF__ + errorMessage);
          
					throw runtime_error(errorMessage);
				}
				string liveRecorderVODDirectory =
					liveRecorderVODManifestPathName.substr(0, endOfPathIndex);
				*/

				sizeInBytes = FileIO::getDirectorySizeInBytes(liveRecorderVODDirectory);
			}
		}

		// update Media Item
		{
			// 2020-04-28: next method will change also:
			//  - $.mmsData.dataType of the current liveRecordingVOD: from liveRecordingChunk to liveRecordingVOD
			//		(to make it visible in the MediaItems GUI view)
			//  - $.mmsData.dataType of the previous liveRecordingVOD: from liveRecordingVOD to liveRecordingChunk
			//		(to make it not visible in the MediaItems GUI view)
			//	- the unique name: so next check the item is not found and a new Media Item is created.
			//		This is because I saw the player may not work fine if every minutes the playlist/manifest
			//		is changed
			//	- 
			int newRetentionInMinutes = 120;
			_mmsEngineDBFacade->updateLiveRecorderVOD (
				workspace->_workspaceKey,
				liveRecorderVODUniqueName,
				liveRecorderVODMediaItemKey,
				liveRecorderVODPhysicalPathKey,

				newRetentionInMinutes,

				firstUtcChunkStartTime,
				sFirstUtcChunkStartTime,
				lastUtcChunkEndTime,
				sLastUtcChunkEndTime,
				title,
				durationInMilliSeconds,
				bitRate,
				sizeInBytes,

				videoTracks,
				audioTracks
			);

			// 2020-04-28: the saving of the manifest was moved above because otherwise
			//	ffmpeg.getMediaInfo, of course, was not working
			// ofstream ofManifestFile(liveRecorderVODManifestPathName);
			// ofManifestFile << manifestContent;
		}
	}
	catch(runtime_error e)
	{
		string errorMessage = string("build and update new live recorder VOD manifest file failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
			+ ", e.what: " + e.what()
		;
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}
	catch(exception e)
	{
		string errorMessage = string("build and update new live recorder VOD manifest file failed")
			+ ", liveRecorderIngestionJobKey: " + to_string(liveRecorderIngestionJobKey)
		;
		_logger->error(__FILEREF__ + errorMessage);

		throw runtime_error(errorMessage);
	}
}

void MMSEngineProcessor::manageTarFileInCaseOfIngestionOfSegments(
		int64_t ingestionJobKey,
		string tarBinaryPathName, string workspaceIngestionRepository,
		string sourcePathName
	)
{
	string executeCommand;
	try
	{
		// tar into workspaceIngestion directory
		//	source will be something like <ingestion key>_source
		//	destination will be the original directory (that has to be the same name of the tar file name)
		executeCommand =
			"tar xfz " + tarBinaryPathName
			+ " --directory " + workspaceIngestionRepository;
		_logger->info(__FILEREF__ + "Start tar command "
			+ ", executeCommand: " + executeCommand
		);
		chrono::system_clock::time_point startTar = chrono::system_clock::now();
		int executeCommandStatus = ProcessUtility::execute(executeCommand);
		chrono::system_clock::time_point endTar = chrono::system_clock::now();
		_logger->info(__FILEREF__ + "End tar command "
			+ ", executeCommand: " + executeCommand
			+ ", tarDuration (millisecs): " + to_string(chrono::duration_cast<chrono::milliseconds>(endTar - startTar).count())
		);
		if (executeCommandStatus != 0)
		{
			string errorMessage = string("ProcessUtility::execute failed")
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
				+ ", executeCommandStatus: " + to_string(executeCommandStatus) 
				+ ", executeCommand: " + executeCommand 
			;

			_logger->error(__FILEREF__ + errorMessage);
          
			throw runtime_error(errorMessage);
		}

		// sourceFileName is the name of the tar file name that is the same
		//	of the name of the directory inside the tar file
		string sourceFileName;
		{
			string suffix(".tar.gz");
			if (!(sourcePathName.size() >= suffix.size()
				&& 0 == sourcePathName.compare(sourcePathName.size()-suffix.size(), suffix.size(), suffix)))
			{
				string errorMessage = __FILEREF__ + "sourcePathName does not end with " + suffix
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", sourcePathName: " + sourcePathName
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}

			size_t startFileNameIndex = sourcePathName.find_last_of("/");
			if (startFileNameIndex == string::npos)
			{
				string errorMessage = __FILEREF__ + "sourcePathName bad format"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", sourcePathName: " + sourcePathName
					+ ", startFileNameIndex: " + to_string(startFileNameIndex)
				;
				_logger->error(errorMessage);

				throw runtime_error(errorMessage);
			}
			sourceFileName = sourcePathName.substr(startFileNameIndex + 1);
			sourceFileName = sourceFileName.substr(0, sourceFileName.size() - suffix.size());
		}

		// remove tar file
		{
			string sourceTarFile = workspaceIngestionRepository + "/"
				+ to_string(ingestionJobKey)
				+ "_source"
				+ ".tar.gz";

			_logger->info(__FILEREF__ + "Remove file"
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", sourceTarFile: " + sourceTarFile
			);

			FileIO::remove(sourceTarFile);
		}

		// rename directory generated from tar: from user_tar_filename to 1247848_source
		// Example from /var/catramms/storage/IngestionRepository/users/1/9670725_liveRecorderVOD
		//	to /var/catramms/storage/IngestionRepository/users/1/9676038_source
		{
			string sourceDirectory = workspaceIngestionRepository + "/" + sourceFileName;
			string destDirectory = workspaceIngestionRepository + "/" + to_string(ingestionJobKey) + "_source";
			_logger->info(__FILEREF__ + "Start moveDirectory..."
				+ ", _processorIdentifier: " + to_string(_processorIdentifier)
				+ ", ingestionJobKey: " + to_string(ingestionJobKey)
				+ ", sourceDirectory: " + sourceDirectory
				+ ", destDirectory: " + destDirectory
			);
			// 2020-05-01: since the remove of the director could fails because of nfs issue,
			//	better do a copy and then a remove.
			//	In this way, in case the remove fails, we can ignore the error.
			//	The directory will be removed later by cron job
			{
				chrono::system_clock::time_point startPoint = chrono::system_clock::now();
				FileIO::copyDirectory(sourceDirectory, destDirectory,
					S_IRUSR | S_IWUSR | S_IXUSR |                                                                         
					S_IRGRP | S_IXGRP |                                                                                   
					S_IROTH | S_IXOTH);
				chrono::system_clock::time_point endPoint = chrono::system_clock::now();
				_logger->info(__FILEREF__ + "End copyDirectory"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", sourceDirectory: " + sourceDirectory
					+ ", destDirectory: " + destDirectory
					+ ", copyDuration (millisecs): " + to_string(chrono::duration_cast<chrono::milliseconds>(endPoint - startPoint).count())
				);
			}

			try
			{
				chrono::system_clock::time_point startPoint = chrono::system_clock::now();
				bool removeRecursively = true;
				FileIO::removeDirectory(sourceDirectory, removeRecursively);
				chrono::system_clock::time_point endPoint = chrono::system_clock::now();
				_logger->info(__FILEREF__ + "End removeDirectory"
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey)
					+ ", sourceDirectory: " + sourceDirectory
					+ ", removeDuration (millisecs): " + to_string(chrono::duration_cast<chrono::milliseconds>(endPoint - startPoint).count())
				);
			}
			catch(runtime_error e)
			{
				string errorMessage = string("removeDirectory failed")
					+ ", _processorIdentifier: " + to_string(_processorIdentifier)
					+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
					+ ", e.what: " + e.what() 
				;
				_logger->error(__FILEREF__ + errorMessage);
         
				// throw runtime_error(errorMessage);
			}
		}
	}
	catch(runtime_error e)
	{
		string errorMessage = string("manageTarFileInCaseOfIngestionOfSegments failed")
			+ ", _processorIdentifier: " + to_string(_processorIdentifier)
			+ ", ingestionJobKey: " + to_string(ingestionJobKey) 
			+ ", e.what: " + e.what() 
		;
		_logger->error(__FILEREF__ + errorMessage);
         
		throw runtime_error(errorMessage);
	}
}


int MMSEngineProcessor::progressDownloadCallback(
        int64_t ingestionJobKey,
        chrono::system_clock::time_point& lastTimeProgressUpdate, 
        double& lastPercentageUpdated, bool& downloadingStoppedByUser,
        double dltotal, double dlnow,
        double ultotal, double ulnow)
{

    chrono::system_clock::time_point now = chrono::system_clock::now();
            
    if (dltotal != 0 &&
            (dltotal == dlnow 
            || now - lastTimeProgressUpdate >= chrono::seconds(_progressUpdatePeriodInSeconds)))
    {
        double progress = (dlnow / dltotal) * 100;
        // int downloadingPercentage = floorf(progress * 100) / 100;
        // this is to have one decimal in the percentage
        double downloadingPercentage = ((double) ((int) (progress * 10))) / 10;

        _logger->info(__FILEREF__ + "Download still running"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", downloadingPercentage: " + to_string(downloadingPercentage)
            + ", dltotal: " + to_string(dltotal)
            + ", dlnow: " + to_string(dlnow)
            + ", ultotal: " + to_string(ultotal)
            + ", ulnow: " + to_string(ulnow)
        );
        
        lastTimeProgressUpdate = now;

        if (lastPercentageUpdated != downloadingPercentage)
        {
            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", downloadingPercentage: " + to_string(downloadingPercentage)
            );                            
            downloadingStoppedByUser = _mmsEngineDBFacade->updateIngestionJobSourceDownloadingInProgress (
                ingestionJobKey, downloadingPercentage);

            lastPercentageUpdated = downloadingPercentage;
        }

        if (downloadingStoppedByUser)
            return 1;   // stop downloading
    }

    return 0;
}

int MMSEngineProcessor::progressUploadCallback(
        int64_t ingestionJobKey,
        chrono::system_clock::time_point& lastTimeProgressUpdate, 
        double& lastPercentageUpdated, bool& uploadingStoppedByUser,
        double dltotal, double dlnow,
        double ultotal, double ulnow)
{

    chrono::system_clock::time_point now = chrono::system_clock::now();
            
    if (ultotal != 0 &&
            (ultotal == ulnow 
            || now - lastTimeProgressUpdate >= chrono::seconds(_progressUpdatePeriodInSeconds)))
    {
        double progress = (ulnow / ultotal) * 100;
        // int uploadingPercentage = floorf(progress * 100) / 100;
        // this is to have one decimal in the percentage
        double uploadingPercentage = ((double) ((int) (progress * 10))) / 10;

        _logger->info(__FILEREF__ + "Upload still running"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", uploadingPercentage: " + to_string(uploadingPercentage)
            + ", dltotal: " + to_string(dltotal)
            + ", dlnow: " + to_string(dlnow)
            + ", ultotal: " + to_string(ultotal)
            + ", ulnow: " + to_string(ulnow)
        );
        
        lastTimeProgressUpdate = now;

        if (lastPercentageUpdated != uploadingPercentage)
        {
            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", _processorIdentifier: " + to_string(_processorIdentifier)
                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                + ", uploadingPercentage: " + to_string(uploadingPercentage)
            );                            
            uploadingStoppedByUser = _mmsEngineDBFacade->updateIngestionJobSourceUploadingInProgress (
                ingestionJobKey, uploadingPercentage);

            lastPercentageUpdated = uploadingPercentage;
        }

        if (uploadingStoppedByUser)
            return 1;   // stop downloading
    }
        
    return 0;
}
