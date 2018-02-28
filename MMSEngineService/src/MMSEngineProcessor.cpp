
#include <fstream>
#include <sstream>
#include <regex>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include "catralibraries/System.h"
#include "MMSEngineProcessor.h"
#include "CheckIngestionTimes.h"
#include "CheckEncodingTimes.h"
#include "catralibraries/md5.h"

MMSEngineProcessor::MMSEngineProcessor(
        shared_ptr<spdlog::logger> logger, 
        shared_ptr<MultiEventsSet> multiEventsSet,
        shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade,
        shared_ptr<MMSStorage> mmsStorage,
        ActiveEncodingsManager* pActiveEncodingsManager
)
{
    _logger             = logger;
    _multiEventsSet     = multiEventsSet;
    _mmsEngineDBFacade  = mmsEngineDBFacade;
    _mmsStorage      = mmsStorage;
    _pActiveEncodingsManager = pActiveEncodingsManager;

    _ulIngestionLastCustomerIndex   = 0;
    _firstGetEncodingJob            = true;
    _processorMMS                   = System::getHostName();
    _maxDownloadAttemptNumber       = 3;
    _progressUpdatePeriodInSeconds  = 5;
    _secondsWaitingAmongDownloadingAttempt  = 5;
    
    _ulMaxIngestionsNumberPerCustomerEachIngestionPeriod        = 2;
    _ulJsonToBeProcessedAfterSeconds                            = 3;
    _ulRetentionPeriodInDays                                    = 10;
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
    _logger->info(__FILEREF__ + "MMSEngineProcessor thread started");

    bool endEvent = false;
    while(!endEvent)
    {
        // cout << "Calling getAndRemoveFirstEvent" << endl;
        shared_ptr<Event> event = _multiEventsSet->getAndRemoveFirstEvent(MMSENGINEPROCESSORNAME, blocking, milliSecondsToBlock);
        if (event == nullptr)
        {
            // cout << "No event found or event not yet expired" << endl;

            continue;
        }

        switch(event->getEventKey().first)
        {
            case MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTIONEVENT:	// 1
            {
                _logger->debug(__FILEREF__ + "Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKINGESTION");

                try
                {
        		handleCheckIngestionEvent ();
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleCheckIngestionEvent failed"
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event>(event);

            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT:	// 2
            {
                _logger->info(__FILEREF__ + "Received LOCALASSETINGESTIONEVENT");

                shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = dynamic_pointer_cast<LocalAssetIngestionEvent>(event);

                try
                {
                    handleLocalAssetIngestionEvent (localAssetIngestionEvent);
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleLocalAssetIngestionEvent failed"
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<LocalAssetIngestionEvent>(localAssetIngestionEvent);
            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_CHECKENCODINGEVENT:	// 3
            {
                _logger->debug(__FILEREF__ + "Received MMSENGINE_EVENTTYPEIDENTIFIER_CHECKENCODING");

                try
                {
        		handleCheckEncodingEvent ();
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleCheckEncodingEvent failed"
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<Event>(event);

            }
            break;
            case MMSENGINE_EVENTTYPEIDENTIFIER_GENERATEIMAGETOINGESTEVENT:	// 4
            {
                _logger->debug(__FILEREF__ + "Received MMSENGINE_EVENTTYPEIDENTIFIER_GENERATEIMAGETOINGESTEVENT");

                shared_ptr<GenerateImageToIngestEvent>    generateImageToIngestEvent =
                        dynamic_pointer_cast<GenerateImageToIngestEvent>(event);

                try
                {
                    handleGenerateImageToIngestEvent (generateImageToIngestEvent);
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "handleGenerateImageToIngestEvent failed"
                        + ", exception: " + e.what()
                    );
                }

                _multiEventsSet->getEventsFactory()->releaseEvent<GenerateImageToIngestEvent>(generateImageToIngestEvent);

            }
            break;
            default:
                throw runtime_error(string("Event type identifier not managed")
                        + to_string(event->getEventKey().first));
        }
    }

    _logger->info(__FILEREF__ + "MMSEngineProcessor thread terminated");
}

void MMSEngineProcessor::handleCheckIngestionEvent()
{
    
    vector<shared_ptr<Customer>> customers = _mmsEngineDBFacade->getCustomers();
    
    unsigned long ulCurrentCustomerIndex = 0;
    vector<shared_ptr<Customer>>::iterator itCustomers;
    
    for (itCustomers = customers.begin();
            itCustomers != customers.end() && ulCurrentCustomerIndex < _ulIngestionLastCustomerIndex;
            ++itCustomers, ulCurrentCustomerIndex++)
    {
    }

    for (; itCustomers != customers.end(); ++itCustomers)
    {
        shared_ptr<Customer> customer = *itCustomers;

        try
        {
            _logger->debug(__FILEREF__ + "Looking for ingestions"
                + ", customer->_customerKey: " + to_string(customer->_customerKey)
                + ", customer->_customerKey: " + customer->_name
            );
            _ulIngestionLastCustomerIndex++;

            string customerFTPDirectory = _mmsStorage->getCustomerFTPRepository(customer);

            shared_ptr<FileIO::Directory> directory = FileIO:: openDirectory(customerFTPDirectory);

            unsigned long ulCurrentCustomerIngestionsNumber		= 0;

            bool moreContentsToBeProcessed = true;
            while (moreContentsToBeProcessed)
            {
                FileIO:: DirectoryEntryType_t	detDirectoryEntryType;
                string directoryEntry;
                
                try
                {
                    directoryEntry = FileIO::readDirectory (directory,
                        &detDirectoryEntryType);
                }
                catch(DirectoryListFinished dlf)
                {
                    moreContentsToBeProcessed = false;

                    continue;
                }
                catch(...)
                {
                    _logger->error(__FILEREF__ + "FileIO::readDirectory failed");

                    moreContentsToBeProcessed = false;

                    break;
                }

                if (detDirectoryEntryType != FileIO:: TOOLS_FILEIO_REGULARFILE)
                {
                    continue;
                }

                // managing the FTP entry
                try
                {
                    string srcPathName(customerFTPDirectory);
                    srcPathName
                        .append("/")
                        .append(directoryEntry);

                    chrono::system_clock::time_point lastModificationTime = 
                        FileIO:: getFileTime (srcPathName);

                    if (chrono::duration_cast<chrono::hours>(chrono::system_clock::now() - lastModificationTime) 
                            >= chrono::hours(_ulRetentionPeriodInDays * 24))
                    {
                        _logger->info(__FILEREF__ + "Remove obsolete FTP file"
                            + ", srcPathName: " + srcPathName
                            + ", _ulRetentionPeriodInDays: " + to_string(_ulRetentionPeriodInDays)
                        );

                        FileIO::remove (srcPathName);

                        continue;
                    }

                    // check if the file has ".json" as extension.
                    // We do not accept also the ".json" file (without name)
                    string jsonExtension(".json");
                    string completedExtension(".completed");    // source media binary that is first uploaded/downloaded and then it is renamed to append .completed
                    if (directoryEntry.length() > completedExtension.length() 
                            && equal(completedExtension.rbegin(), completedExtension.rend(), directoryEntry.rbegin()))
                    {
                        // it is a completed media source binary
                        // let's see if metadata are already arrived/managed?
                        string mediaSourceFileName = directoryEntry.substr(0, directoryEntry.length() - completedExtension.length());
                        
        IMPLEMENTARE IL METODO SOTTO usando nella where 
        - gli stati SourceDownloadingInProgress e WaitingUploadSourceReference
        - considerare che quando nella where si usa SourceReference, puo' anche essereuna URL
                        pair<int64_t,string> ingestionJobKeyAndMetadataFileName = 
                                _mmsEngineDBFacade->getWaitingSourceReferenceIngestionJob (
                                customer->_customerKey, mediaSourceFileName);
                         
                        {
                            shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                                    ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

                            localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
                            localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
                            localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

                            localAssetIngestionEvent->setIngestionJobKey(ingestionJobKeyAndMetadataFileName.first);
                            localAssetIngestionEvent->setCustomer(customer);

                            localAssetIngestionEvent->setMetadataFileName(ingestionJobKeyAndMetadataFileName.second);
                            localAssetIngestionEvent->setMediaSourceFileName(mediaSourceFileName);

                            shared_ptr<Event>    event = dynamic_pointer_cast<Event>(localAssetIngestionEvent);
                            _multiEventsSet->addEvent(event);

                            _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
                                + ", mediaSourceFileName: " + mediaSourceFileName
                                + ", getEventKey().first: " + to_string(event->getEventKey().first)
                                + ", getEventKey().second: " + to_string(event->getEventKey().second));
                        }

                        continue;
                    }
                    else if (directoryEntry.length() <= jsonExtension.length() ||
                            !equal(jsonExtension.rbegin(), jsonExtension.rend(), directoryEntry.rbegin()))
                            continue;

                    // it's a json file
                    
                    if (chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - lastModificationTime) 
                            < chrono::seconds(_ulJsonToBeProcessedAfterSeconds))
                    {
                        // only the json files having the last modification older
                        // at least of _ulJsonToBeProcessedAfterSeconds seconds
                        // are considered

                        continue;
                    }

                    _logger->info(__FILEREF__ + "json to be processed"
                        + ", directoryEntry: " + directoryEntry
                    );

                    // check if directoryEntry was created by API. In this case
                    //  we already have an entry into DB (ingestionJobKey)
                    int64_t ingestionJobKey = -1;
                    try
                    {
                        string apiPrefix = "API-";
                        int ingestionJobKeyStart = apiPrefix.length();
                        int ingestionJobKeyEnd;
                        
                        if (directoryEntry.compare(0, apiPrefix.size(), apiPrefix) == 0 
                                && (ingestionJobKeyEnd = directoryEntry.find("-", ingestionJobKeyStart)) != string::npos)
                            ingestionJobKey = stol(directoryEntry.substr(ingestionJobKeyStart, ingestionJobKeyEnd));
                    }
                    catch(exception e)
                    {
                        _logger->error(__FILEREF__ + "IngestionJobKey not found"
                            + ", directoryEntry: " + directoryEntry
                        );
                    }

                    _mmsStorage->moveFTPRepositoryEntryToWorkingArea(customer, directoryEntry);

                    string      metadataFileContent;
                    pair<MMSEngineDBFacade::IngestionType,MMSEngineDBFacade::ContentType> ingestionTypeAndContentType;
                    Json::Value metadataRoot;
                    try
                    {
                        {
                            ifstream medatataFile(
                                _mmsStorage->getCustomerFTPWorkingMetadataPathName(customer, directoryEntry));
                            stringstream buffer;
                            buffer << medatataFile.rdbuf();

                            metadataFileContent = buffer.str();
                        }

                        ifstream ingestAssetJson(
                                _mmsStorage->getCustomerFTPWorkingMetadataPathName(customer, directoryEntry), 
                                std::ifstream::binary);
                        try
                        {
                            ingestAssetJson >> metadataRoot;
                        }
                        catch(...)
                        {
                            throw runtime_error(string("wrong ingestion metadata json format")
                                    + ", directoryEntry: " + directoryEntry
                                    );
                        }

                        ingestionTypeAndContentType = validateMetadata(metadataRoot);
                    }
                    catch(runtime_error e)
                    {
                        _logger->error(__FILEREF__ + "validateMetadata failed"
                                + ", exception: " + e.what()
                        );
                        string ftpDirectoryErrorEntryPathName =
                            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, directoryEntry);

                        string errorMessage = e.what();

                        if (ingestionJobKey == -1)
                        {
                            _logger->info(__FILEREF__ + "Adding IngestionJob"
                                + ", customer->_customerKey: " + to_string(customer->_customerKey)
                                + ", directoryEntry: " + directoryEntry
                                + ", SourceReference: " + ""
                                + ", IngestionType: " + "Unknown"
                                + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            _mmsEngineDBFacade->addIngestionJob (customer->_customerKey, 
                                directoryEntry, metadataFileContent, "", MMSEngineDBFacade::IngestionType::Unknown, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                _processorMMS, errorMessage);
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                _processorMMS, errorMessage);
                        }

                        throw e;
                    }
                    catch(exception e)
                    {
                        _logger->error(__FILEREF__ + "validateMetadata failed"
                                + ", exception: " + e.what()
                        );
                        string ftpDirectoryErrorEntryPathName =
                            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, directoryEntry);

                        string errorMessage = e.what();

                        if (ingestionJobKey == -1)
                        {
                            _logger->info(__FILEREF__ + "Adding IngestionJob"
                                + ", customer->_customerKey: " + to_string(customer->_customerKey)
                                + ", directoryEntry: " + directoryEntry
                                + ", SourceReference: " + ""
                                + ", IngestionType: " + "Unknown"
                                + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            _mmsEngineDBFacade->addIngestionJob (customer->_customerKey, 
                                directoryEntry, metadataFileContent, "", MMSEngineDBFacade::IngestionType::Unknown, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                _processorMMS, errorMessage);                            
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", IngestionStatus: " + "End_ValidationMetadataFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed, 
                                _processorMMS, errorMessage);
                        }

                        throw e;
                    }

                    bool mediaSourceToBeDownload;
                    bool localMediaSourceUploadCompleted;
                    string mediaSourceReference;
                    string mediaSourceFileName;
                    string md5FileCheckSum;
                    int fileSizeInBytes;
                    try
                    {
                        tuple<bool, bool, string, string, string, int> mediaSourceDetails;
                        
                        mediaSourceDetails = getMediaSourceDetails(customer,
                                ingestionTypeAndContentType.first, metadataRoot);

                        tie(mediaSourceToBeDownload, localMediaSourceUploadCompleted,
                                mediaSourceReference, mediaSourceFileName, 
                                md5FileCheckSum, fileSizeInBytes) = mediaSourceDetails;                        
                    }
                    catch(runtime_error e)
                    {
                        _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                                + ", exception: " + e.what()
                        );
                        string ftpDirectoryErrorEntryPathName =
                            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, directoryEntry);

                        string errorMessage = e.what();

                        if (ingestionJobKey == -1)
                        {
                            _logger->info(__FILEREF__ + "Adding IngestionJob"
                                + ", customer->_customerKey: " + to_string(customer->_customerKey)
                                + ", directoryEntry: " + directoryEntry
                                + ", SourceReference: " + ""
                                + ", IngestionType: " + MMSEngineDBFacade::toString(ingestionTypeAndContentType.first)
                                + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            _mmsEngineDBFacade->addIngestionJob (customer->_customerKey, 
                                directoryEntry, metadataFileContent, "", ingestionTypeAndContentType.first, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                _processorMMS, errorMessage);
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", SourceReference: " + ""
                                + ", IngestionType: " + MMSEngineDBFacade::toString(ingestionTypeAndContentType.first)
                                + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );                            
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                "", ingestionTypeAndContentType.first,
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                _processorMMS, errorMessage);
                        }

                        throw e;
                    }
                    catch(exception e)
                    {
                        _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                                + ", exception: " + e.what()
                        );
                        string ftpDirectoryErrorEntryPathName =
                            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, directoryEntry);

                        string errorMessage = e.what();

                        if (ingestionJobKey == -1)
                        {
                            _logger->info(__FILEREF__ + "Adding IngestionJob"
                                + ", customer->_customerKey: " + to_string(customer->_customerKey)
                                + ", directoryEntry: " + directoryEntry
                                + ", SourceReference: " + ""
                                + ", IngestionType: " + MMSEngineDBFacade::toString(ingestionTypeAndContentType.first)
                                + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            _mmsEngineDBFacade->addIngestionJob (customer->_customerKey, 
                                directoryEntry, metadataFileContent, "", ingestionTypeAndContentType.first, 
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                _processorMMS, errorMessage);
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", SourceReference: " + ""
                                + ", IngestionType: " + MMSEngineDBFacade::toString(ingestionTypeAndContentType.first)
                                + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );                            
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                "", ingestionTypeAndContentType.first,
                                MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed, 
                                _processorMMS, errorMessage);
                        }

                        throw e;
                    }

                    try
                    {
                        string errorMessage = "";
                        if (ingestionJobKey == -1)
                        {
                            _logger->info(__FILEREF__ + "Adding IngestionJob"
                                + ", customer->_customerKey: " + to_string(customer->_customerKey)
                                + ", directoryEntry: " + directoryEntry
                                + ", SourceReference: " + mediaSourceReference
                                + ", IngestionType: " + MMSEngineDBFacade::toString(ingestionTypeAndContentType.first)
                                + ", IngestionStatus: " + "StartIngestion"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );
                            ingestionJobKey = _mmsEngineDBFacade->addIngestionJob (customer->_customerKey, 
                                directoryEntry, metadataFileContent, mediaSourceReference, ingestionTypeAndContentType.first, 
                                MMSEngineDBFacade::IngestionStatus::StartIngestion, 
                                _processorMMS, errorMessage);
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", SourceReference: " + mediaSourceReference
                                + ", IngestionType: " + MMSEngineDBFacade::toString(ingestionTypeAndContentType.first)
                                + ", IngestionStatus: " + "StartIngestion"
                                + ", _processorMMS: " + _processorMMS
                                + ", errorMessage: " + errorMessage
                            );                            
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey,
                                    mediaSourceReference, ingestionTypeAndContentType.first,
                                    MMSEngineDBFacade::IngestionStatus::StartIngestion, 
                                    _processorMMS, errorMessage);
                        }
                    }
                    catch(exception e)
                    {
                        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->addIngestionJob failed"
                                + ", exception: " + e.what()
                        );

                        string ftpDirectoryErrorEntryPathName =
                            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, directoryEntry);
                    
                        throw e;
                    }

                    try
                    {
                        // mediaSourceReference could be a URL or a filename.
                        // In this last case, it will be the same as mediaSourceFileName
                        
                        if (mediaSourceToBeDownload)
                        {
                            thread downloadMediaSource(&MMSEngineProcessor::downloadMediaSourceFile, this, 
                                mediaSourceReference, ingestionJobKey, customer,
                                directoryEntry, mediaSourceFileName);
                            downloadMediaSource.detach();
                            
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", IngestionStatus: " + "SourceDownloadingInProgress"
                                + ", errorMessage: " + ""
                            );                            
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::SourceDownloadingInProgress, "");
                        }
                        else
                        {
                            _logger->info(__FILEREF__ + "Update IngestionJob"
                                + ", ingestionJobKey: " + to_string(ingestionJobKey)
                                + ", IngestionStatus: " + "SourceUploadingInProgress"
                                + ", errorMessage: " + ""
                            );                            
                            _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                                MMSEngineDBFacade::IngestionStatus::WaitingUploadSourceReference, "");
                        }
                    }
                    catch(exception e)
                    {
                        _logger->error(__FILEREF__ + "add LocalAssetIngestionEvent failed"
                                + ", exception: " + e.what()
                        );

                        string ftpDirectoryErrorEntryPathName =
                            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, directoryEntry);

                        _logger->info(__FILEREF__ + "Update IngestionJob"
                            + ", ingestionJobKey: " + to_string(ingestionJobKey)
                            + ", IngestionStatus: " + "End_IngestionFailure"
                            + ", errorMessage: " + e.what()
                        );                            
                        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
                    }

                    ulCurrentCustomerIngestionsNumber++;

                    if (ulCurrentCustomerIngestionsNumber >=
                        _ulMaxIngestionsNumberPerCustomerEachIngestionPeriod)
                        break;
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "Exception managing the FTP entry"
                        + ", exception: " + e.what()
                        + ", customerKey: " + to_string(customer->_customerKey)
                        + ", directoryEntry: " + directoryEntry
                    );
                }
            }

            FileIO:: closeDirectory (directory);
        }
        catch(...)
        {
            _logger->error(__FILEREF__ + "Error processing the Customer FTP"
                + ", customer->_name: " + customer->_name
                    );
        }
    }

    if (itCustomers == customers.end())
        _ulIngestionLastCustomerIndex	= 0;

}

void MMSEngineProcessor::handleLocalAssetIngestionEvent (
    shared_ptr<LocalAssetIngestionEvent> localAssetIngestionEvent)
{
    string relativePathToBeUsed;
    try
    {
        relativePathToBeUsed = _mmsEngineDBFacade->checkCustomerMaxIngestionNumber (
                localAssetIngestionEvent->getCustomer()->_customerKey);
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "checkCustomerMaxIngestionNumber failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_CustomerReachedHisMaxIngestionNumber"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_CustomerReachedHisMaxIngestionNumber,
                e.what());

        throw e;
    }
                    
    string      metadataFileContent;
    pair<MMSEngineDBFacade::IngestionType,MMSEngineDBFacade::ContentType> ingestionTypeAndContentType;
    Json::Value metadataRoot;
    try
    {
        {
            ifstream medatataFile(
                _mmsStorage->getCustomerFTPWorkingMetadataPathName(
                    localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName())
            );
            stringstream buffer;
            buffer << medatataFile.rdbuf();

            metadataFileContent = buffer.str();
        }

        ifstream ingestAssetJson(
                _mmsStorage->getCustomerFTPWorkingMetadataPathName(
                    localAssetIngestionEvent->getCustomer(), 
                    localAssetIngestionEvent->getMetadataFileName()), 
                std::ifstream::binary);
        try
        {
            ingestAssetJson >> metadataRoot;
        }
        catch(...)
        {
            throw runtime_error(string("wrong ingestion metadata json format")
                    + ", FTPWorkingMetadataPathName: " + _mmsStorage->getCustomerFTPWorkingMetadataPathName(
                        localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName())
                    );
        }

        ingestionTypeAndContentType = validateMetadata(metadataRoot);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "validateMetadata failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
            MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
            e.what());

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "validateMetadata failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMetadataFailed"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
            MMSEngineDBFacade::IngestionStatus::End_ValidationMetadataFailed,
            e.what());

        throw e;
    }

    tuple<bool, bool, string, string, string, int> mediaSourceDetails;
    try
    {
        mediaSourceDetails = getMediaSourceDetails(
                localAssetIngestionEvent->getCustomer(),
                ingestionTypeAndContentType.first, metadataRoot);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
            MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
            e.what());

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "getMediaSourceDetails failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
            MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
            e.what());

        throw e;
    }

    try
    {
        string md5FileCheckSum = get<4>(mediaSourceDetails);
        int fileSizeInBytes = get<5>(mediaSourceDetails);

        validateMediaSourceFile(_mmsStorage->getCustomerFTPMediaSourcePathName(
                    localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMediaSourceFileName()),
                md5FileCheckSum, fileSizeInBytes);
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "validateMediaSourceFile failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
            MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
            e.what());

        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "validateMediaSourceFile failed"
                + ", exception: " + e.what()
        );
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        string errorMessage = e.what();

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_ValidationMediaSourceFailed"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
            MMSEngineDBFacade::IngestionStatus::End_ValidationMediaSourceFailed,
            e.what());

        throw e;
    }

    unsigned long mmsPartitionIndexUsed;
    string mmsAssetPathName;
    try
    {                
        bool partitionIndexToBeCalculated   = true;
        bool deliveryRepositoriesToo        = true;
        string customerFTPMediaSourcePathNameCompleted =
                _mmsStorage->getCustomerFTPMediaSourcePathName(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMediaSourceFileName());
        customerFTPMediaSourcePathNameCompleted.append(".completed");
        mmsAssetPathName = _mmsStorage->moveAssetInMMSRepository(
            customerFTPMediaSourcePathNameCompleted,
            localAssetIngestionEvent->getCustomer()->_directoryName,
            localAssetIngestionEvent->getMediaSourceFileName(),
            relativePathToBeUsed,
            partitionIndexToBeCalculated,
            &mmsPartitionIndexUsed,
            deliveryRepositoriesToo,
            localAssetIngestionEvent->getCustomer()->_territories
            );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed");
        
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "_mmsStorage->moveAssetInMMSRepository failed");
        
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
        
        throw e;
    }

    int64_t videoOrAudioDurationInMilliSeconds = -1;
    int imageWidth = -1;
    int imageHeight = -1;
    if (ingestionTypeAndContentType.second == MMSEngineDBFacade::ContentType::Video 
            || ingestionTypeAndContentType.second == MMSEngineDBFacade::ContentType::Audio)
    {
        try
        {
            videoOrAudioDurationInMilliSeconds = 
                EncoderVideoAudioProxy::getVideoOrAudioDurationInMilliSeconds(
                    mmsAssetPathName);
        }
        catch(runtime_error e)
        {
            _logger->error(__FILEREF__ + "EncoderVideoAudioProxy::getVideoOrAudioDurationInMilliSeconds failed");

            _logger->info(__FILEREF__ + "Remove file"
                + ", mmsAssetPathName: " + mmsAssetPathName
            );
            FileIO::remove(mmsAssetPathName);

            string ftpDirectoryErrorEntryPathName =
                _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                    localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
            _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());

            throw e;
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "EncoderVideoAudioProxy::getVideoOrAudioDurationInMilliSeconds failed");

            _logger->info(__FILEREF__ + "Remove file"
                + ", mmsAssetPathName: " + mmsAssetPathName
            );
            FileIO::remove(mmsAssetPathName);

            string ftpDirectoryErrorEntryPathName =
                _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                    localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
            _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());

            throw e;
        }
    }
    else if (ingestionTypeAndContentType.second == MMSEngineDBFacade::ContentType::Image)
    {
        try
        {
            Magick:: Image      imageToEncode;

            imageToEncode. read (mmsAssetPathName.c_str());

            imageWidth	= imageToEncode. columns ();
            imageHeight	= imageToEncode. rows ();
        }
        catch(exception e)
        {
            _logger->error(__FILEREF__ + "ImageMagick failed to retrieve width and height failed");

            _logger->info(__FILEREF__ + "Remove file"
                + ", mmsAssetPathName: " + mmsAssetPathName
            );
            FileIO::remove(mmsAssetPathName);

            string ftpDirectoryErrorEntryPathName =
                _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                    localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

            _logger->info(__FILEREF__ + "Update IngestionJob"
                + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
                + ", IngestionStatus: " + "End_IngestionFailure"
                + ", errorMessage: " + e.what()
            );                            
            _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                    MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());

            throw e;
        }
    }

    int64_t mediaItemKey;
    try
    {
        bool inCaseOfLinkHasItToBeRead = false;
        unsigned long sizeInBytes = FileIO::getFileSizeInBytes(mmsAssetPathName,
                inCaseOfLinkHasItToBeRead);   

        _logger->info(__FILEREF__ + "_mmsEngineDBFacade->saveIngestedContentMetadata..."
        );

        pair<int64_t,int64_t> mediaItemKeyAndPhysicalPathKey =
                _mmsEngineDBFacade->saveIngestedContentMetadata (
                    localAssetIngestionEvent->getCustomer(),
                    localAssetIngestionEvent->getIngestionJobKey(),
                    metadataRoot,
                    relativePathToBeUsed,
                    localAssetIngestionEvent->getMediaSourceFileName(),
                    mmsPartitionIndexUsed,
                    sizeInBytes,
                    videoOrAudioDurationInMilliSeconds,
                    imageWidth,
                    imageHeight
        );
        
        mediaItemKey = mediaItemKeyAndPhysicalPathKey.first;
        
        _logger->info(__FILEREF__ + "Added a new ingested content"
            + ", mediaItemKey: " + to_string(mediaItemKeyAndPhysicalPathKey.first)
            + ", physicalPathKey: " + to_string(mediaItemKeyAndPhysicalPathKey.second)
        );
    }
    catch(runtime_error e)
    {
        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->saveIngestedContentMetadata failed");

        _logger->info(__FILEREF__ + "Remove file"
            + ", mmsAssetPathName: " + mmsAssetPathName
        );
        FileIO::remove(mmsAssetPathName);
        
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
        
        throw e;
    }
    catch(exception e)
    {
        _logger->error(__FILEREF__ + "_mmsEngineDBFacade->saveIngestedContentMetadata failed");

        _logger->info(__FILEREF__ + "Remove file"
            + ", mmsAssetPathName: " + mmsAssetPathName
        );
        FileIO::remove(mmsAssetPathName);
        
        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(
                localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(localAssetIngestionEvent->getIngestionJobKey())
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (localAssetIngestionEvent->getIngestionJobKey(),
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
        
        throw e;
    }
    
    string ftpDirectorySuccessEntryPathName =
        _mmsStorage->moveFTPRepositoryWorkingEntryToSuccessArea(
            localAssetIngestionEvent->getCustomer(), localAssetIngestionEvent->getMetadataFileName());

    // ingest Screenshots if present
    if (ingestionTypeAndContentType.second == MMSEngineDBFacade::ContentType::Video)
    {        
        string field = "ContentIngestion";
        Json::Value contentIngestion = metadataRoot[field]; 

        field = "Screenshots";
        if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
        {
            const Json::Value screenshots = contentIngestion[field];

            field = "Title";
            if (!_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
            string videoTitle = contentIngestion.get(field, "XXX").asString();

            _logger->info(__FILEREF__ + "Processing the screenshots"
                + ", screenshots.size(): " + to_string(screenshots.size())
            );
            
            for(int screenshotIndex = 0; screenshotIndex < screenshots.size(); screenshotIndex++) 
            {
                try
                {
                    Json::Value screenshot = screenshots[screenshotIndex];

                    field = "TimePositionInSeconds";
                    if (!_mmsEngineDBFacade->isMetadataPresent(screenshot, field))
                    {
                        string errorMessage = __FILEREF__ + "Field is not present or it is null"
                                + ", Field: " + field;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    double timePositionInSeconds = screenshot.get(field, "XXX").asDouble();

                    field = "EncodingProfilesSet";
                    string encodingProfilesSet;
                    if (!_mmsEngineDBFacade->isMetadataPresent(screenshot, field))
                        encodingProfilesSet = "customerDefault";
                    else
                        encodingProfilesSet = screenshot.get(field, "XXX").asString();

                    field = "SourceImageWidth";
                    if (!_mmsEngineDBFacade->isMetadataPresent(screenshot, field))
                    {
                        string errorMessage = __FILEREF__ + "Field is not present or it is null"
                                + ", Field: " + field;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    int sourceImageWidth = screenshot.get(field, "XXX").asInt();

                    field = "SourceImageHeight";
                    if (!_mmsEngineDBFacade->isMetadataPresent(screenshot, field))
                    {
                        string errorMessage = __FILEREF__ + "Field is not present or it is null"
                                + ", Field: " + field;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    int sourceImageHeight = screenshot.get(field, "XXX").asInt();

                    if (videoOrAudioDurationInMilliSeconds < timePositionInSeconds * 1000)
                    {
                        string errorMessage = __FILEREF__ + "Screenshot was not generated because timePositionInSeconds is bigger than the video duration"
                                + ", video mediaItemKey: " + to_string(mediaItemKey)
                                + ", timePositionInSeconds: " + to_string(timePositionInSeconds)
                                + ", videoOrAudioDurationInMilliSeconds: " + to_string(videoOrAudioDurationInMilliSeconds)
                        ;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                    
                    string imageFileName;
                    {
                        size_t fileExtensionIndex = localAssetIngestionEvent->getMediaSourceFileName().find_last_of(".");
                        if (fileExtensionIndex == string::npos)
                            imageFileName.append(localAssetIngestionEvent->getMediaSourceFileName());
                        else
                            imageFileName.append(localAssetIngestionEvent->getMediaSourceFileName().substr(0, fileExtensionIndex));
                        imageFileName
                                .append("_")
                                .append(to_string(screenshotIndex + 1))
                                .append(".jpg")
                        ;
                    }

                    {
                        shared_ptr<GenerateImageToIngestEvent>    generateImageToIngestEvent = _multiEventsSet->getEventsFactory()
                                ->getFreeEvent<GenerateImageToIngestEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_GENERATEIMAGETOINGESTEVENT);

                        generateImageToIngestEvent->setSource(MMSENGINEPROCESSORNAME);
                        generateImageToIngestEvent->setDestination(MMSENGINEPROCESSORNAME);
                        generateImageToIngestEvent->setExpirationTimePoint(chrono::system_clock::now());

                        generateImageToIngestEvent->setCmsVideoPathName(mmsAssetPathName);
                        generateImageToIngestEvent->setCustomerFTPRepository(_mmsStorage->getCustomerFTPRepository(localAssetIngestionEvent->getCustomer()));
                        generateImageToIngestEvent->setImageFileName(imageFileName);
                        generateImageToIngestEvent->setImageTitle(videoTitle + " image #" + to_string(screenshotIndex + 1));

                        generateImageToIngestEvent->setTimePositionInSeconds(timePositionInSeconds);
                        generateImageToIngestEvent->setEncodingProfilesSet(encodingProfilesSet);
                        generateImageToIngestEvent->setSourceImageWidth(sourceImageWidth);
                        generateImageToIngestEvent->setSourceImageHeight(sourceImageHeight);

                        shared_ptr<Event>    event = dynamic_pointer_cast<Event>(generateImageToIngestEvent);
                        _multiEventsSet->addEvent(event);

                        _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (GENERATEIMAGETOINGESTEVENT) to generate the screenshot"
                            + ", imageFileName: " + imageFileName
                            + ", getEventKey().first: " + to_string(event->getEventKey().first)
                            + ", getEventKey().second: " + to_string(event->getEventKey().second));
                    }
                }
                catch(exception e)
                {
                    _logger->error(__FILEREF__ + "Prepare generation image to ingest failed"
                        + ", screenshotIndex: " + to_string(screenshotIndex)
                    );
                }
            }
        }
    }
}

void MMSEngineProcessor::handleGenerateImageToIngestEvent (
    shared_ptr<GenerateImageToIngestEvent> generateImageToIngestEvent)
{
    string imagePathName = generateImageToIngestEvent->getCustomerFTPRepository()
            + "/"
            + generateImageToIngestEvent->getImageFileName()
            + ".completed"
    ;

    size_t extensionIndex = generateImageToIngestEvent->getImageFileName().find_last_of(".");
    if (extensionIndex == string::npos)
    {
        string errorMessage = __FILEREF__ + "No extension find in the image file name"
                + ", generateImageToIngestEvent->getImageFileName(): " + generateImageToIngestEvent->getImageFileName();
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }
    string metadataImagePathName = generateImageToIngestEvent->getCustomerFTPRepository()
            + "/"
            + generateImageToIngestEvent->getImageFileName().substr(0, extensionIndex)
            + ".json"
    ;

    EncoderVideoAudioProxy::generateScreenshotToIngest(
            imagePathName,
            generateImageToIngestEvent->getTimePositionInSeconds(),
            generateImageToIngestEvent->getSourceImageWidth(),
            generateImageToIngestEvent->getSourceImageHeight(),
            generateImageToIngestEvent->getCmsVideoPathName()
    );
    
    _logger->info(__FILEREF__ + "Generated Screenshot to ingest"
        + ", imagePathName: " + imagePathName
    );

    generateImageMetadataToIngest(
            metadataImagePathName,
            generateImageToIngestEvent->getImageTitle(),
            generateImageToIngestEvent->getImageFileName(),
            generateImageToIngestEvent->getEncodingProfilesSet()
    );

    _logger->info(__FILEREF__ + "Generated Image metadata to ingest"
        + ", metadataImagePathName: " + metadataImagePathName
    );
}

void MMSEngineProcessor::generateImageMetadataToIngest(
        string metadataImagePathName,
        string title,
        string sourceImageFileName,
        string encodingProfilesSet
)
{
    string imageMetadata = string("")
        + "{"
            + "\"Type\": \"ContentIngestion\","
            + "\"Version\": \"1.0\","
            + "\"ContentIngestion\": {"
                + "\"Title\": \"" + title + "\","
                + "\"Ingester\": \"MMSEngine\","
                + "\"SourceReference\": \"" + sourceImageFileName + "\","
                + "\"ContentType\": \"image\","
                + "\"EncodingProfilesSet\": \"" + encodingProfilesSet + "\""
            + "}"
        + "}"
    ;

    ofstream metadataFileStream(metadataImagePathName, ofstream::trunc);
    metadataFileStream << imageMetadata;
}

void MMSEngineProcessor::handleCheckEncodingEvent ()
{
    vector<shared_ptr<MMSEngineDBFacade::EncodingItem>> encodingItems;
    
    bool resetToBeDone = _firstGetEncodingJob ? true : false;
    
    _mmsEngineDBFacade->getEncodingJobs(resetToBeDone,
        _processorMMS, encodingItems);

    _firstGetEncodingJob = false;

    _pActiveEncodingsManager->addEncodingItems(encodingItems);
}

pair<MMSEngineDBFacade::IngestionType,MMSEngineDBFacade::ContentType> MMSEngineProcessor::validateMetadata(
    Json::Value metadataRoot)
{
    pair<MMSEngineDBFacade::IngestionType,MMSEngineDBFacade::ContentType> ingestionTypeAndContentType;
    
    MMSEngineDBFacade::IngestionType    ingestionType;
    MMSEngineDBFacade::ContentType      contentType;

    string field = "Type";
    if (!_mmsEngineDBFacade->isMetadataPresent(metadataRoot, field))
    {
        string errorMessage = __FILEREF__ + "Field is not present or it is null"
                + ", Field: " + field;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

    string type = metadataRoot.get("Type", "XXX").asString();
    if (type == "ContentIngestion")
        ingestionType = MMSEngineDBFacade::IngestionType::ContentIngestion;
    /*
    else if (type == "ContentUpdate")
        ingestionType = MMSEngineDBFacade::IngestionType::ContentUpdate;
    else if (type == "ContentRemove")
        ingestionType = MMSEngineDBFacade::IngestionType::ContentRemove;
    */
    else
    {
        string errorMessage = __FILEREF__ + "Field 'Type' is wrong"
                + ", Type: " + type;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

    field = "Version";
    if (!_mmsEngineDBFacade->isMetadataPresent(metadataRoot, field))
    {
        string errorMessage = __FILEREF__ + "Field is not present or it is wrong"
                + ", Field: " + field;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

    // MMSEngineDBFacade::ContentType contentType = MMSEngineDBFacade::ContentType::Video;
    if (ingestionType == MMSEngineDBFacade::IngestionType::ContentIngestion)
    {
        string field = "ContentIngestion";
        if (!_mmsEngineDBFacade->isMetadataPresent(metadataRoot, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        Json::Value contentIngestion = metadataRoot[field]; 

        contentType = validateContentIngestionMetadata(contentIngestion);
    }
    
    ingestionTypeAndContentType.first = ingestionType;
    ingestionTypeAndContentType.second = contentType;

    
    return ingestionTypeAndContentType;
}

MMSEngineDBFacade::ContentType MMSEngineProcessor::validateContentIngestionMetadata(
    Json::Value contentIngestion)
{
    // see sample in directory samples
    
    MMSEngineDBFacade::ContentType         contentType;
    
    vector<string> contentIngestionMandatoryFields = {
        "Title",
        "SourceReference",
        "ContentType",
        "EncodingProfilesSet"
    };
    for (string contentIngestionField: contentIngestionMandatoryFields)
    {
        if (!_mmsEngineDBFacade->isMetadataPresent(contentIngestion, contentIngestionField))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + contentIngestionField;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }
    }

    string sContentType = contentIngestion.get("ContentType", "XXX").asString();
    try
    {
        contentType = MMSEngineDBFacade::toContentType(sContentType);
    }
    catch(exception e)
    {
        string errorMessage = __FILEREF__ + "Field 'ContentType' is wrong"
                + ", sContentType: " + sContentType;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

    string field;
    if (contentType == MMSEngineDBFacade::ContentType::Video 
            || contentType == MMSEngineDBFacade::ContentType::Audio)
    {
        field = "EncodingPriority";
        if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
        {
            string encodingPriority = contentIngestion.get(field, "XXX").asString();
            try
            {
                MMSEngineDBFacade::toEncodingPriority(encodingPriority);    // it generate an exception in case of wrong string
            }
            catch(exception e)
            {
                string errorMessage = __FILEREF__ + "Field 'EncodingPriority' is wrong"
                        + ", EncodingPriority: " + encodingPriority;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
    }

    /*
    // Territories
    {
        field = "Territories";
        if (isMetadataPresent(contentIngestion, field))
        {
            const Json::Value territories = contentIngestion[field];
            
            for( Json::ValueIterator itr = territories.begin() ; itr != territories.end() ; itr++ ) 
            {
                Json::Value territory = territories[territoryIndex];
            }
        
    }
    */
    
    // Screenshots
    if (contentType == MMSEngineDBFacade::ContentType::Video)
    {
        field = "Screenshots";
        if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
        {
            const Json::Value screenshots = contentIngestion[field];
            
            for(int screenshotIndex = 0; screenshotIndex < screenshots.size(); screenshotIndex++) 
            {
                Json::Value screenshot = screenshots[screenshotIndex];
                
                vector<string> screenshotMandatoryFields = {
                    "TimePositionInSeconds",
                    // "EncodingProfilesSet",
                    "SourceImageWidth",
                    "SourceImageHeight"
                };
                for (string screenshotField: screenshotMandatoryFields)
                {
                    if (!_mmsEngineDBFacade->isMetadataPresent(screenshot, screenshotField))
                    {
                        string errorMessage = __FILEREF__ + "Field is not present or it is null"
                                + ", Field: " + screenshotField;
                        _logger->error(errorMessage);

                        throw runtime_error(errorMessage);
                    }
                }
            }
        }
    }
    
    field = "Delivery";
    if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field) && contentIngestion.get(field, "XXX").asString() == "FTP")
    {
        field = "FTP";
        if (!_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        Json::Value ftp = contentIngestion[field]; 

        vector<string> mandatoryFields = {
            "Hostname",
            "User",
            "Password"
        };
        for (string field: mandatoryFields)
        {
            if (!_mmsEngineDBFacade->isMetadataPresent(ftp, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
    }

    field = "Notification";
    if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field) && contentIngestion.get(field, "XXX").asString() == "EMail")
    {
        field = "EMail";
        if (!_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
        {
            string errorMessage = __FILEREF__ + "Field is not present or it is null"
                    + ", Field: " + field;
            _logger->error(errorMessage);

            throw runtime_error(errorMessage);
        }

        Json::Value email = contentIngestion["EMail"]; 

        vector<string> mandatoryFields = {
            "Address"
        };
        for (string field: mandatoryFields)
        {
            if (!_mmsEngineDBFacade->isMetadataPresent(email, field))
            {
                string errorMessage = __FILEREF__ + "Field is not present or it is null"
                        + ", Field: " + field;
                _logger->error(errorMessage);

                throw runtime_error(errorMessage);
            }
        }
    }
    
    return contentType;
}

tuple<bool, bool, string, string, string, int> MMSEngineProcessor::getMediaSourceDetails(
        shared_ptr<Customer> customer, MMSEngineDBFacade::IngestionType ingestionType,
        Json::Value root)        
{
    bool mediaSourceToBeDownload;
    string mediaSourceReference;    // URL or local file name
    string mediaSourceFileName;
    
    string field = "ContentIngestion";
    Json::Value contentIngestion = root[field]; 

    if (ingestionType == MMSEngineDBFacade::IngestionType::ContentIngestion)
    {
        field = "SourceReference";
        mediaSourceReference = contentIngestion.get(field, "XXX").asString();
        
        string httpPrefix ("http");
        string ftpPrefix ("ftp");
        if (!mediaSourceReference.compare(0, httpPrefix.size(), httpPrefix)
                || !mediaSourceReference.compare(0, ftpPrefix.size(), ftpPrefix))
        {
            mediaSourceToBeDownload = true;
            
            // mediaSourceFileName
            {
                smatch m;
                
                regex e (R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)", std::regex::extended);
                
                if (!regex_search(mediaSourceReference, m, e))
                {
                    string errorMessage = __FILEREF__ + "mediaSourceReference URL format is wrong"
                            + ", mediaSourceReference: " + mediaSourceReference;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }
                
                string path = m[5].str();
                if (path.back() == '/')
                    path.pop_back();
                
                size_t fileNameIndex = path.find_last_of("/");
                if (fileNameIndex == string::npos)
                {
                    string errorMessage = __FILEREF__ + "No fileName find in the mediaSourceReference"
                            + ", mediaSourceReference: " + mediaSourceReference;
                    _logger->error(errorMessage);

                    throw runtime_error(errorMessage);
                }

                mediaSourceFileName = path.substr(fileNameIndex + 1);
            }
        }
        else
        {
            mediaSourceFileName = mediaSourceReference;
            mediaSourceToBeDownload = false;
        }
    }   
    else
    {
        string errorMessage = __FILEREF__ + "ingestionType is wrong"
                + ", ingestionType: " + to_string(static_cast<int>(ingestionType));
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

    bool localMediaSourceUploadCompleted = false;
    if (!mediaSourceToBeDownload)
    {
        string ftpDirectoryMediaSourceFileName = _mmsStorage->getCustomerFTPMediaSourcePathName(
                    customer, mediaSourceFileName) + ".completed";
        
        localMediaSourceUploadCompleted = FileIO::fileExisting(ftpDirectoryMediaSourceFileName);
    }

    string md5FileCheckSum;
    field = "MD5FileCheckSum";
    if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
    {
        MD5         md5;
        char        md5RealDigest [32 + 1];

        md5FileCheckSum = contentIngestion.get(field, "XXX").asString();
    }

    int fileSizeInBytes = -1;
    field = "FileSizeInBytes";
    if (_mmsEngineDBFacade->isMetadataPresent(contentIngestion, field))
        fileSizeInBytes = contentIngestion.get(field, 3).asInt();

    tuple<bool, bool, string, string, string, int> mediaSourceDetails;
    get<0>(mediaSourceDetails) = mediaSourceToBeDownload;
    get<1>(mediaSourceDetails) = localMediaSourceUploadCompleted;
    get<2>(mediaSourceDetails) = mediaSourceReference;
    get<3>(mediaSourceDetails) = mediaSourceFileName;
    get<4>(mediaSourceDetails) = md5FileCheckSum;
    get<5>(mediaSourceDetails) = fileSizeInBytes;

    _logger->info(__FILEREF__ + "media source file to be processed"
        + ", mediaSourceToBeDownload: " + to_string(get<0>(mediaSourceDetails))
        + ", localMediaSourceUploadCompleted: " + to_string(get<1>(mediaSourceDetails))
        + ", mediaSourceReference: " + get<2>(mediaSourceDetails)
        + ", mediaSourceFileName: " + get<3>(mediaSourceDetails)
        + ", md5FileCheckSum: " + get<4>(mediaSourceDetails)
        + ", fileSizeInBytes: " + to_string(get<5>(mediaSourceDetails))
    );

    
    return mediaSourceDetails;
}

void MMSEngineProcessor::validateMediaSourceFile (string ftpDirectoryMediaSourceFileName,
        string md5FileCheckSum, int fileSizeInBytes)
{
    if (!FileIO::fileExisting(ftpDirectoryMediaSourceFileName))
    {
        string errorMessage = __FILEREF__ + "Media Source file does not exist (it was not uploaded yet)"
                + ", ftpDirectoryMediaSourceFileName: " + ftpDirectoryMediaSourceFileName;
        _logger->error(errorMessage);

        throw runtime_error(errorMessage);
    }

    if (md5FileCheckSum != "")
    {
        MD5         md5;
        char        md5RealDigest [32 + 1];

        strcpy (md5RealDigest, md5.digestFile((char *) ftpDirectoryMediaSourceFileName.c_str()));

        if (md5FileCheckSum != md5RealDigest)
        {
            string errorMessage = __FILEREF__ + "MD5 check failed"
                + ", ftpDirectoryMediaSourceFileName: " + ftpDirectoryMediaSourceFileName
                + ", md5FileCheckSum: " + md5FileCheckSum
                + ", md5RealDigest: " + md5RealDigest
                    ;
            _logger->error(errorMessage);
            throw runtime_error(errorMessage);
        }
    }
    
    if (fileSizeInBytes != -1)
    {
        bool inCaseOfLinkHasItToBeRead = false;
        unsigned long realFileSizeInBytes = 
            FileIO:: getFileSizeInBytes (ftpDirectoryMediaSourceFileName, inCaseOfLinkHasItToBeRead);

        if (fileSizeInBytes != realFileSizeInBytes)
        {
            string errorMessage = __FILEREF__ + "FileSize check failed"
                + ", ftpDirectoryMediaSourceFileName: " + ftpDirectoryMediaSourceFileName
                + ", fileSizeInBytes: " + to_string(fileSizeInBytes)
                + ", realFileSizeInBytes: " + to_string(realFileSizeInBytes)
            ;
            _logger->error(errorMessage);
            throw runtime_error(errorMessage);
        }
    }    
}

void MMSEngineProcessor::downloadMediaSourceFile(string sourceReferenceURL,
        int64_t ingestionJobKey, shared_ptr<Customer> customer,
        string metadataFileName, string mediaSourceFileName)
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
 
        
    for (int attemptIndex = 0; attemptIndex < _maxDownloadAttemptNumber && !downloadingCompleted; attemptIndex++)
    {
        bool downloadingStoppedByUser = false;
        
        try 
        {
            _logger->info(__FILEREF__ + "Downloading"
                + ", sourceReferenceURL: " + sourceReferenceURL
                + ", attempt: " + to_string(attemptIndex + 1)
                + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
            );
            
            if (attemptIndex == 0)
            {
                ofstream mediaSourceFileStream(
                    _mmsStorage->getCustomerFTPMediaSourcePathName(customer, mediaSourceFileName));

                curlpp::Cleanup cleaner;
                curlpp::Easy request;

                // Set the writer callback to enable cURL 
                // to write result in a memory area
                request.setOpt(new curlpp::options::WriteStream(&mediaSourceFileStream));

                // Setting the URL to retrive.
                request.setOpt(new curlpp::options::Url(sourceReferenceURL));

                chrono::system_clock::time_point lastProgressUpdate = chrono::system_clock::now();
                int lastPercentageUpdated = -1;
                curlpp::types::ProgressFunctionFunctor functor = bind(&MMSEngineProcessor::progressCallback, this,
                        ingestionJobKey, lastProgressUpdate, lastPercentageUpdated, downloadingStoppedByUser,
                        placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
                request.setOpt(new curlpp::options::ProgressFunction(curlpp::types::ProgressFunctionFunctor(functor)));
                request.setOpt(new curlpp::options::NoProgress(0L));

                request.setOpt(new curlpp::options::NoProgress(0L));

                _logger->info(__FILEREF__ + "Downloading media file"
                    + ", sourceReferenceURL: " + sourceReferenceURL
                );
                request.perform();
            }
            else
            {
                _logger->warn(__FILEREF__ + "Coming from a download failure, trying to Resume");
                
                ofstream mediaSourceFileStream(
                        _mmsStorage->getCustomerFTPMediaSourcePathName(customer, mediaSourceFileName),
                        ofstream::app);

                curlpp::Cleanup cleaner;
                curlpp::Easy request;

                // Set the writer callback to enable cURL 
                // to write result in a memory area
                request.setOpt(new curlpp::options::WriteStream(&mediaSourceFileStream));

                // Setting the URL to retrive.
                request.setOpt(new curlpp::options::Url(sourceReferenceURL));

                chrono::system_clock::time_point lastTimeProgressUpdate = chrono::system_clock::now();
                int lastPercentageUpdated = -1;
                curlpp::types::ProgressFunctionFunctor functor = bind(&MMSEngineProcessor::progressCallback, this,
                        ingestionJobKey, lastTimeProgressUpdate, lastPercentageUpdated, downloadingStoppedByUser,
                        placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4);
                request.setOpt(new curlpp::options::ProgressFunction(curlpp::types::ProgressFunctionFunctor(functor)));
                request.setOpt(new curlpp::options::NoProgress(0L));

                long long fileSize = mediaSourceFileStream.tellp();
                if (fileSize > 2 * 1000 * 1000 * 1000)
                    request.setOpt(new curlpp::options::ResumeFromLarge(fileSize));
                else
                    request.setOpt(new curlpp::options::ResumeFrom(fileSize));
                
                _logger->info(__FILEREF__ + "Resume Download media file"
                    + ", sourceReferenceURL: " + sourceReferenceURL
                    + ", resuming from fileSize: " + to_string(fileSize)
                );
                request.perform();
            }

            // rename to .completed
            {
                string customerFTPMediaSourcePathNameCompleted =
                    _mmsStorage->getCustomerFTPMediaSourcePathName(customer, mediaSourceFileName);
                customerFTPMediaSourcePathNameCompleted.append(".completed");
                FileIO::moveFile (
                        _mmsStorage->getCustomerFTPMediaSourcePathName(customer, mediaSourceFileName), 
                        customerFTPMediaSourcePathNameCompleted);
            }

            downloadingCompleted = true;
        }
        catch (curlpp::LogicError & e) 
        {
            _logger->error(__FILEREF__ + "Download failed (LogicError)"
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", sourceReferenceURL: " + sourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                string ftpDirectoryErrorEntryPathName =
                    _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->info(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    string ftpDirectoryErrorEntryPathName =
                        _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", sourceReferenceURL: " + sourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
        catch (curlpp::RuntimeError & e) 
        {
            _logger->error(__FILEREF__ + "Download failed (RuntimeError)"
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", sourceReferenceURL: " + sourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                string ftpDirectoryErrorEntryPathName =
                    _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->info(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    string ftpDirectoryErrorEntryPathName =
                        _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", sourceReferenceURL: " + sourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
        catch (exception e)
        {
            _logger->error(__FILEREF__ + "Download failed (exception)"
                + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                + ", sourceReferenceURL: " + sourceReferenceURL 
                + ", exception: " + e.what()
            );

            if (downloadingStoppedByUser)
            {
                string ftpDirectoryErrorEntryPathName =
                    _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

                downloadingCompleted = true;
            }
            else
            {
                if (attemptIndex + 1 == _maxDownloadAttemptNumber)
                {
                    _logger->info(__FILEREF__ + "Reached the max number of download attempts"
                        + ", _maxDownloadAttemptNumber: " + to_string(_maxDownloadAttemptNumber)
                    );
                    
                    string ftpDirectoryErrorEntryPathName =
                        _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

                    _logger->info(__FILEREF__ + "Update IngestionJob"
                        + ", ingestionJobKey: " + to_string(ingestionJobKey)
                        + ", IngestionStatus: " + "End_IngestionFailure"
                        + ", errorMessage: " + e.what()
                    );                            
                    _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                        MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
                }
                else
                {
                    _logger->info(__FILEREF__ + "Download failed. sleeping before to attempt again"
                        + ", ingestionJobKey: " + to_string(ingestionJobKey) 
                        + ", sourceReferenceURL: " + sourceReferenceURL 
                        + ", _secondsWaitingAmongDownloadingAttempt: " + to_string(_secondsWaitingAmongDownloadingAttempt)
                    );
                    this_thread::sleep_for(chrono::seconds(_secondsWaitingAmongDownloadingAttempt));
                }
            }
        }
    }

    try 
    {
        shared_ptr<LocalAssetIngestionEvent>    localAssetIngestionEvent = _multiEventsSet->getEventsFactory()
                ->getFreeEvent<LocalAssetIngestionEvent>(MMSENGINE_EVENTTYPEIDENTIFIER_LOCALASSETINGESTIONEVENT);

        localAssetIngestionEvent->setSource(MMSENGINEPROCESSORNAME);
        localAssetIngestionEvent->setDestination(MMSENGINEPROCESSORNAME);
        localAssetIngestionEvent->setExpirationTimePoint(chrono::system_clock::now());

        localAssetIngestionEvent->setIngestionJobKey(ingestionJobKey);
        localAssetIngestionEvent->setCustomer(customer);

        localAssetIngestionEvent->setMetadataFileName(metadataFileName);
        localAssetIngestionEvent->setMediaSourceFileName(mediaSourceFileName);

        shared_ptr<Event>    event = dynamic_pointer_cast<Event>(localAssetIngestionEvent);
        _multiEventsSet->addEvent(event);

        _logger->info(__FILEREF__ + "addEvent: EVENT_TYPE (INGESTASSETEVENT)"
            + ", mediaSourceFileName: " + mediaSourceFileName
            + ", getEventKey().first: " + to_string(event->getEventKey().first)
            + ", getEventKey().second: " + to_string(event->getEventKey().second));
    }
    catch (runtime_error & e) 
    {
        _logger->error(__FILEREF__ + "sending INGESTASSETEVENT failed"
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", sourceReferenceURL: " + sourceReferenceURL 
            + ", exception: " + e.what()
        );

        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
    }
    catch (exception e)
    {
        _logger->error(__FILEREF__ + "sending INGESTASSETEVENT failed"
            + ", ingestionJobKey: " + to_string(ingestionJobKey) 
            + ", sourceReferenceURL: " + sourceReferenceURL 
            + ", exception: " + e.what()
        );

        string ftpDirectoryErrorEntryPathName =
            _mmsStorage->moveFTPRepositoryWorkingEntryToErrorArea(customer, metadataFileName);

        _logger->info(__FILEREF__ + "Update IngestionJob"
            + ", ingestionJobKey: " + to_string(ingestionJobKey)
            + ", IngestionStatus: " + "End_IngestionFailure"
            + ", errorMessage: " + e.what()
        );                            
        _mmsEngineDBFacade->updateIngestionJob (ingestionJobKey, 
                MMSEngineDBFacade::IngestionStatus::End_IngestionFailure, e.what());
    }
}

int MMSEngineProcessor::progressCallback(
        int64_t ingestionJobKey,
        chrono::system_clock::time_point& lastTimeProgressUpdate, 
        int& lastPercentageUpdated, bool& downloadingStoppedByUser,
        double dltotal, double dlnow,
        double ultotal, double ulnow)
{

    chrono::system_clock::time_point now = chrono::system_clock::now();
            
    if (dltotal != 0 &&
            (dltotal == dlnow 
            || now - lastTimeProgressUpdate >= chrono::seconds(_progressUpdatePeriodInSeconds)))
    {
        double progress = (dlnow / dltotal) * 100;
        int downloadingPercentage = floorf(progress * 100) / 100;

        _logger->info(__FILEREF__ + "Download still running"
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