/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   Validator.h
 * Author: giuliano
 *
 * Created on March 29, 2018, 6:27 AM
 */

#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "MMSEngineDBFacade.h"

class Validator {
public:
    enum class DependencyType {
        MediaItemKey,
        PhysicalPathKey,
        IngestionJobKey
    };
    
public:
    Validator(            
            shared_ptr<spdlog::logger> logger, 
            shared_ptr<MMSEngineDBFacade> mmsEngineDBFacade
    );
    
    Validator(const Validator& orig);
    virtual ~Validator();
    
    bool isVideoAudioFileFormat(string fileFormat);
    
    bool isImageFileFormat(string fileFormat);

    bool isFontTypeValid(string fontType);
    
    bool isFontColorValid(string fontColor);

    void validateRootMetadata(int64_t workspaceKey, Json::Value root);

    void validateGroupOfTasksMetadata(int64_t workspaceKey, Json::Value groupOfTasksRoot);

    vector<pair<int64_t,Validator::DependencyType>> validateSingleTaskMetadata(int64_t workspaceKey, Json::Value taskRoot);

    void validateEvents(int64_t workspaceKey, Json::Value taskOrGroupOfTasksRoot);

    vector<pair<int64_t,Validator::DependencyType>> validateSingleTaskMetadata(int64_t workspaceKey,
        MMSEngineDBFacade::IngestionType ingestionType, Json::Value parametersRoot);

    void validateAddContentMetadata(Json::Value parametersRoot);

    void validateRemoveContentMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);
    
    void validateEncodeMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateFrameMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validatePeriodicalFramesMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);
    
    void validateIFramesMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateSlideshowMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);
    
    void validateConcatDemuxerMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateCutMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateOverlayImageOnVideoMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateOverlayTextOnVideoMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t, DependencyType>>& dependencies);

    void validateEmailNotificationMetadata(
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateFTPDeliveryMetadata(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);

    void validateEncodingProfilesSetRootMetadata(
        MMSEngineDBFacade::ContentType contentType, 
        Json::Value encodingProfilesSetRoot);
        
    void validateEncodingProfileRootMetadata(
        MMSEngineDBFacade::ContentType contentType,
        Json::Value encodingProfileRoot);

    static bool isMetadataPresent(Json::Value root, string field);
    
private:
    shared_ptr<spdlog::logger>          _logger;
    shared_ptr<MMSEngineDBFacade>       _mmsEngineDBFacade;

    void validateEncodingProfileRootVideoMetadata(
        Json::Value encodingProfileRoot);
    
    void validateEncodingProfileRootAudioMetadata(
        Json::Value encodingProfileRoot);
    
    void validateEncodingProfileRootImageMetadata(
        Json::Value encodingProfileRoot);
    
    void fillDependencies(int64_t workspaceKey,
        Json::Value parametersRoot, vector<pair<int64_t,DependencyType>>& dependencies);
};

#endif /* VALIDATOR_H */

