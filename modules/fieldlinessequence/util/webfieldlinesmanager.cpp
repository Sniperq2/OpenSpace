/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2019                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/fieldlinessequence/util/webfieldlinesmanager.h>

#include <ghoul/logging/logmanager.h>
#include <openspace/util/httprequest.h>
#include <modules/sync/syncs/httpsynchronization.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/filesystem/file.h>
#include <openspace/util/timemanager.h>
#include <openspace/engine/globals.h>


namespace {
    constexpr const char* _loggerCat = "FieldlinesSequence[ Web FLs Manager ]";
    
} // namespace

namespace openspace{

    WebFieldlinesManager::WebFieldlinesManager(std::string syncDir){
        
        // change to parameter
        _syncDir = "/Users/shuy/Offline-dokument/OpenSpace/Spaceweather/OpenSpace/data/assets/testwsa/fl_pfss_io_25";
        _flsType = "PfssIo";
        _downloadMargin = 3;
        _timeTriggerDelta = 7200;
   
        getAvailableTriggertimes();
        
        setInitialSet(global::timeManager.time().j2000Seconds());
        
        LERROR("WebFieldlinesManager initialized");
    
    }
    
    // dowload files specified in _filestodownload
    // I'm thinking we can replace the parameters with pointers to the lists that will be
    // initialized in the constuctor instead
    void WebFieldlinesManager::downloadFieldlines(std::vector<std::string>& _sourceFiles, std::vector<double>& _startTimes, size_t& _nStates){
        LERROR("starting download");
        for (int index : _filesToDownload){
            

            std::string filename = _availableTriggertimes[index].second;
            double timetrigger = _availableTriggertimes[index].first;
            
            // download fieldlines file
            std::string destPath = downloadOsfls(filename);
            
            //add the timetrigger at the right place in the list
            int i = 0;
            while(timetrigger > _startTimes[i]){
                if( i == static_cast<int>(_nStates)) break;
                else i++;
            }
            _sourceFiles.insert(_sourceFiles.begin() + i, destPath);
            _startTimes.insert(_startTimes.begin() + i, timetrigger);
            _nStates += 1;
        }
    }
    
    // this function aint done
    void WebFieldlinesManager::update(std::vector<double> startTimes, int activeTriggerTimeIndex){
        // check how many are left until fieldlinessequence runs out - add direction information later
        double nextTheroticalTimeTrigger;
        double eps = 100;
        if(activeTriggerTimeIndex == startTimes.size()-1){
            // if it's at the last index, definetily start some downloading
            return;
        }
        for (int i = activeTriggerTimeIndex; i < startTimes.size(); i++){
            nextTheroticalTimeTrigger = startTimes[i] +_timeTriggerDelta;
            if(startTimes[i + 1] > (nextTheroticalTimeTrigger + eps)){
                // do some downloading
            }
            
        }
    }
    
    std::string WebFieldlinesManager::downloadOsfls(std::string triggertime){
        std::string url = "http://localhost:3000/WSA/" + triggertime;
        std::string destinationpath = absPath(_syncDir + '/' + triggertime.substr(6));
        AsyncHttpFileDownload ashd = AsyncHttpFileDownload(url, destinationpath, HttpFileDownload::Overwrite::Yes);
        HttpRequest::RequestOptions opt = {};
        opt.requestTimeoutSeconds = 0;
        ashd.start(opt);
        ashd.wait();
        if(ashd.hasSucceeded() == true ){
            LERROR("succeeeded: " + destinationpath);
        }
        if(ashd.hasFailed() == true ){
            LERROR("failed: " + destinationpath);
        }
        return destinationpath;
    }
    
    
    void WebFieldlinesManager::getAvailableTriggertimes(){
        std::string url = "http://localhost:3000/WSA/available/" + _flsType;
        SyncHttpMemoryDownload mmryDld = SyncHttpMemoryDownload(url);
        HttpRequest::RequestOptions opt = {};
        opt.requestTimeoutSeconds = 0;
        mmryDld.download(opt);
        
        // Put the results in a string
        std::string s;
        std::transform(mmryDld.downloadedData().begin(), mmryDld.downloadedData().end(), std::back_inserter(s),
                       [](char c) {
                           return c;
                       });
        parseTriggerTimesList(s);
        
        // sort ascending by trigger time
        std::sort(_availableTriggertimes.begin(), _availableTriggertimes.end());
    }
    
    void WebFieldlinesManager::setInitialSet(double openSpaceTime){
        
        int openspaceindex = -1;
        do openspaceindex++;
        while (openSpaceTime > _availableTriggertimes[openspaceindex].first);
        
        int startInd = openspaceindex - _downloadMargin;
        int endInd = openspaceindex + _downloadMargin;
        
        if(startInd < 0) startInd = 0;
        if(endInd >= _availableTriggertimes.size())
            endInd = _availableTriggertimes.size()-1;
        
        for(int i = startInd; i <= endInd; i++)
            _filesToDownload.push_back(i);
    }
    
    // TODO
    void WebFieldlinesManager::downloadInitialSequence(std::vector<double> triggertimes){
        
    }
    
    // TODO
    void WebFieldlinesManager::updateStartTimes(){
        
    }
    
    void WebFieldlinesManager::parseTriggerTimesList(std::string s){
        // Turn into stringstream to parse the comma-delimited string into vector
        std::stringstream ss(s);
        char c;
        std::string sub;
        while(ss >> c)
        {
            if (c == '[' || c == ']' || c == '"' ) continue;
            else if (c == ','){
                double tt = triggerTimeString2Int(sub.substr(6, 23));
                _availableTriggertimes.push_back(std::make_pair(tt, sub));
                sub.clear();
            }
            else sub += c;
        }
        double tt = triggerTimeString2Int(sub.substr(6, 23));
        _availableTriggertimes.push_back(std::make_pair(tt, sub));
    }
    
    int WebFieldlinesManager::triggerTimeString2Int(std::string s){
        s.replace(13, 1, ":");
        s.replace(16, 1, ":");
        Time time = Time();
        time.setTime(s);
        return static_cast<int> (time.j2000Seconds() - 69.185013294);
    }
    
    void WebFieldlinesManager::triggerTimeInt2String(int i, std::string& s){
        double temp = i + 69.185013294;
        Time time = Time();
        time.setTime(temp);
        s = time.ISO8601();
        s.replace(13, 1, "-");
        s.replace(16, 1, "-");
    }
    
} // namespace openspace
