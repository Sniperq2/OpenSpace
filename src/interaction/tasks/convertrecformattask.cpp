/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2020                                                               *
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

#include <openspace/interaction/tasks/convertrecformattask.h>
#include <openspace/interaction/sessionrecording.h>
#include <openspace/documentation/verifier.h>

#include <openspace/engine/globals.h>
#include <ghoul/filesystem/file.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/logging/logmanager.h>
#include <filesystem>
#include <iomanip>

namespace {
    constexpr const char* _loggerCat = "ConvertRecFormatTask";

    constexpr const char* KeyInFilePath = "InputFilePath";
    constexpr const char* KeyOutFilePath = "OutputFilePath";
} // namespace

namespace openspace::interaction {

ConvertRecFormatTask::ConvertRecFormatTask(const ghoul::Dictionary& dictionary) {
    openspace::documentation::testSpecificationAndThrow(
        documentation(),
        dictionary,
        "ConvertRecFormatTask"
    );

    _inFilePath = absPath(dictionary.value<std::string>(KeyInFilePath));
    _outFilePath = absPath(dictionary.value<std::string>(KeyOutFilePath));

    ghoul_assert(FileSys.fileExists(_inFilePath), "The filename must exist");
    if (!FileSys.fileExists(_inFilePath)) {
        LERROR(fmt::format("Failed to load session recording file: {}", _inFilePath));
    }
    else {
        _iFile.open(_inFilePath, std::ifstream::in);
        determineFormatType();
    }
}

ConvertRecFormatTask::~ConvertRecFormatTask() {
    _iFile.close();
    _oFile.close();
}

std::string ConvertRecFormatTask::description() {
    std::string description = "Convert session recording file '" + _inFilePath + "' ";
    if (_fileFormatType == sessionrecording::DataMode::Ascii) {
        description += "(ascii format) ";
    }
    else if (_fileFormatType == sessionrecording::DataMode::Binary) {
        description += "(binary format) ";
    }
    else {
        description += "(UNKNOWN format) ";
    }
    description += "conversion to file '" + _outFilePath + "'.";
    return description;
}

void ConvertRecFormatTask::perform(const Task::ProgressCallback& progressCallback) {
    convert();
}

void ConvertRecFormatTask::convert() {
    std::string expectedFileExtension_in, expectedFileExtension_out;
    std::string currentFormat;
    if (_fileFormatType == sessionrecording::DataMode::Binary) {
        currentFormat = "binary";
        expectedFileExtension_in = SessionRecording::FileExtensionBinary;
        expectedFileExtension_out = SessionRecording::FileExtensionAscii;
    }
    else if (_fileFormatType == sessionrecording::DataMode::Ascii) {
        currentFormat = "ascii";
        expectedFileExtension_in = SessionRecording::FileExtensionAscii;
        expectedFileExtension_out = SessionRecording::FileExtensionBinary;
    }

    if (std::filesystem::path(_inFilePath).extension() != expectedFileExtension_in) {
        LWARNING(fmt::format(
            "Input filename doesn't have expected {} "
            "format file extension",
            currentFormat)
        );
    }
    if (std::filesystem::path(_outFilePath).extension() == expectedFileExtension_in) {
        LERROR(fmt::format(
            "Output filename has {} file extension, but is conversion from {}",
            currentFormat,
            currentFormat)
        );
        return;
    }
    else if (std::filesystem::path(_outFilePath).extension() != expectedFileExtension_out)
    {
        _outFilePath += expectedFileExtension_out;
    }

    if (_fileFormatType == sessionrecording::DataMode::Ascii) {
        _oFile.open(_outFilePath);
    }
    else if (_fileFormatType == sessionrecording::DataMode::Binary) {
        _oFile.open(_outFilePath, std::ios::binary);
    }
    _oFile.write(
        sessionrecording::Header::Title.data(),
        sessionrecording::Header::Title.size()
    );
    _oFile.write(_version.c_str(), SessionRecording::FileHeaderVersionLength);
    _oFile.close();

    if (_fileFormatType == sessionrecording::DataMode::Ascii) {
        convertToBinary();
    }
    else if (_fileFormatType == sessionrecording::DataMode::Binary) {
        convertToAscii();
    }
    else {
        //Add error output for file type not recognized
        LERROR("Session recording file unrecognized format type.");
    }
}

void ConvertRecFormatTask::determineFormatType() {
    _fileFormatType = sessionrecording::DataMode::Unknown;
    std::string line;

    line = readHeaderElement(_iFile, sizeof(sessionrecording::Header::Title));

    if (line.substr(0, sizeof(sessionrecording::Header::Title))
        != sessionrecording::Header::Title)
    {
        LERROR(fmt::format("Session recording file {} does not have expected header.",
            _inFilePath));
    }
    else {
        //Read version string and throw it away (and also line feed character at end)
        _version = readHeaderElement(_iFile, SessionRecording::FileHeaderVersionLength);
        line = readHeaderElement(_iFile, 1);
        readHeaderElement(_iFile, 1);

        if (line.at(0) == SessionRecording::DataFormatAsciiTag) {
            _fileFormatType = sessionrecording::DataMode::Ascii;
        }
        else if (line.at(0) == SessionRecording::DataFormatBinaryTag) {
            _fileFormatType = sessionrecording::DataMode::Binary;
        }
    }
}

void ConvertRecFormatTask::convertToAscii() {
    sessionrecording::Timestamps times;
    sessionrecording::CameraKeyframe ckf;
    sessionrecording::TimeKeyframe   tkf;
    sessionrecording::ScriptMessage  skf;
    int lineNum = 1;
    unsigned char frameType;
    _oFile.open(_outFilePath, std::ifstream::app);
    char tmpType = SessionRecording::DataFormatAsciiTag;
    _oFile.write(&tmpType, 1);
    _oFile.write("\n", 1);

    bool fileReadOk = true;
    while (fileReadOk) {
        frameType = readFromPlayback<unsigned char>(_iFile);
        // Check if have reached EOF
        if (!_iFile) {
            LINFO(fmt::format(
                "Finished converting {} entries from file {}",
                lineNum - 1, _inFilePath
            ));
            fileReadOk = false;
            break;
        }

        std::stringstream keyframeLine = std::stringstream();
        keyframeLine.str(std::string());
        if (frameType == SessionRecording::HeaderCameraBinary) {
            readCameraKeyframeBinary(times, ckf, _iFile, lineNum);
            saveHeaderAscii(times, SessionRecording::HeaderCameraAscii, keyframeLine);
            ckf.write(keyframeLine);
        }
        else if (frameType == SessionRecording::HeaderTimeBinary) {
            readTimeKeyframeBinary(times, tkf, _iFile, lineNum);
            saveHeaderAscii(times, SessionRecording::HeaderTimeAscii, keyframeLine);
            tkf.write(keyframeLine);
        }
        else if (frameType == SessionRecording::HeaderScriptBinary) {
            readScriptKeyframeBinary(times, skf, _iFile, lineNum);
            saveHeaderAscii(times, SessionRecording::HeaderScriptAscii, keyframeLine);
            skf.write(keyframeLine);
        }
        else {
            LERROR(fmt::format(
                "Unknown frame type @ index {} of playback file {}",
                lineNum - 1, _inFilePath
            ));
            break;
        }
        saveKeyframeToFile(keyframeLine.str(), _oFile);
        lineNum++;
    }
    _oFile.close();
}

void ConvertRecFormatTask::convertToBinary() {
    sessionrecording::Timestamps times;
    sessionrecording::CameraKeyframe ckf;
    sessionrecording::TimeKeyframe   tkf;
    sessionrecording::ScriptMessage  skf;
    int lineNum = 1;
    std::string lineContents;
    unsigned char keyframeBuffer[SessionRecording::_saveBufferMaxSize_bytes];
    _oFile.open(_outFilePath, std::ifstream::app | std::ios::binary);
    char tmpType = SessionRecording::DataFormatBinaryTag;
    _oFile.write(&tmpType, 1);
    _oFile.write("\n", 1);
    size_t idx = 0;

    while (std::getline(_iFile, lineContents)) {
        lineNum++;

        std::istringstream iss(lineContents);
        std::string entryType;
        if (!(iss >> entryType)) {
            LERROR(fmt::format(
                "Error reading entry type @ line {} of file {}",
                lineNum, _inFilePath
            ));
            break;
        }

        if (entryType == SessionRecording::HeaderCameraAscii) {
            readCameraKeyframeAscii(times, ckf, lineContents, lineNum);
            saveCameraKeyframeBinary(times, ckf, keyframeBuffer,
            _oFile);
        }
        else if (entryType == SessionRecording::HeaderTimeAscii) {
            readTimeKeyframeAscii(times, tkf, lineContents, lineNum);
            saveTimeKeyframeBinary(times, tkf, keyframeBuffer,
            _oFile);
        }
        else if (entryType == SessionRecording::HeaderScriptAscii) {
            readScriptKeyframeAscii(times, skf, lineContents, lineNum);
            saveScriptKeyframeBinary(times, skf, keyframeBuffer,
            _oFile);
        }
        else if (entryType.substr(0, 1) == SessionRecording::HeaderCommentAscii) {
            continue;
        }
        else {
            LERROR(fmt::format(
                "Unknown frame type {} @ line {} of file {}",
                entryType, lineContents, _inFilePath
            ));
            break;
        }
    }
    _oFile.close();
    LINFO(fmt::format(
        "Finished converting {} entries from file {}",
        lineNum, _inFilePath
    ));
}

std::string ConvertRecFormatTask::addFileSuffix(const std::string& filePath,
                                                const std::string& suffix)
{
    size_t lastdot = filePath.find_last_of(".");
    std::string extension = filePath.substr(0, lastdot);
    if (lastdot == std::string::npos) {
        return filePath + suffix;
    }
    else {
        return filePath.substr(0, lastdot) + suffix + extension;
    }
}

documentation::Documentation ConvertRecFormatTask::documentation() {
    using namespace documentation;
    return {
        "ConvertRecFormatTask",
        "convert_format_task",
        {
            {
                "Type",
                new StringEqualVerifier("ConvertRecFormatTask"),
                Optional::No,
                "The type of this task",
            },
            {
                KeyInFilePath,
                new StringAnnotationVerifier("A valid filename to convert"),
                Optional::No,
                "The filename to convert to the opposite format.",
            },
            {
                KeyOutFilePath,
                new StringAnnotationVerifier("A valid output filename"),
                Optional::No,
                "The filename containing the converted result.",
            },
        },
    };
}

}
