/*********************************************************************************
**
** Copyright (c) 2017 The University of Notre Dame
** Copyright (c) 2017 The Regents of the University of California
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
** list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice, this
** list of conditions and the following disclaimer in the documentation and/or other
** materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its contributors may
** be used to endorse or promote products derived from this software without specific
** prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
** SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
** IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
**
***********************************************************************************/

// Contributors:
// Written by Peter Sempolinski, for the Natural Hazard Modeling Laboratory, director: Ahsan Kareem, at Notre Dame

#include "agavehandler.h"
#include "agavetaskguide.h"
#include "agavetaskreply.h"

#include "../filemetadata.h"
#include "../remotejobdata.h"

//TODO: need to do more double checking of valid file paths

AgaveHandler::AgaveHandler(QObject * parent) :
        RemoteDataInterface(parent), networkHandle(0), SSLoptions()
{
    SSLoptions.setProtocol(QSsl::SecureProtocols);
    clearAllAuthTokens();

    setupTaskGuideList();
    QObject::connect(&networkHandle, SIGNAL(finished(QNetworkReply*)), this, SLOT(finishedOneTask(QNetworkReply*)));
}

void AgaveHandler::finishedOneTask(QNetworkReply *)
{
    pendingRequestCount--;
    if (pendingRequestCount < 0)
    {
        emit sendFatalErrorMessage("Request count is less than 0");
    }
    if (pendingRequestCount == 0)
    {
        emit finishedAllTasks();
    }
}

AgaveHandler::~AgaveHandler()
{
    if ((performingShutdown == true) || (authGained == true))
    {
        emit sendFatalErrorMessage("Agave Handler destroyed without proper shutdown");
    }
    foreach (AgaveTaskGuide * aTaskGuide , validTaskList)
    {
        delete aTaskGuide;
    }
}

QString AgaveHandler::getUserName()
{
    if (authGained)
    {
        return authUname;
    }
    QString empty;
    return empty;
}

bool AgaveHandler::inShutdownMode()
{
    return performingShutdown;
}

RemoteDataReply * AgaveHandler::setCurrentRemoteWorkingDirectory(QString cd)
{
    QString tmp = getPathReletiveToCWD(cd);

    AgaveTaskReply * passThru = new AgaveTaskReply(retriveTaskGuide("changeDir"),NULL,this, (QObject *)this);

    if (tmp.isEmpty())
    {
        passThru->delayedPassThruReply(RequestState::FAIL, &pwd);
    }
    else
    {
        pwd = tmp;
        passThru->delayedPassThruReply(RequestState::GOOD, &pwd);
    }

    passThru->getTaskParamList()->insert("cd",cd);

    return (RemoteDataReply *) passThru;
}

QString AgaveHandler::getPathReletiveToCWD(QString inputPath)
{
    //TODO: check validity of input path
    QString cleanedInput = FileMetaData::cleanPathSlashes(inputPath);

    QStringList retList;

    if (inputPath.at(0) == '/')
    {
        QStringList oldList = cleanedInput.split('/');

        for (auto itr = oldList.cbegin(); itr != oldList.cend(); itr++)
        {
            if ((*itr) == ".") {}
            else if ((*itr) == "..")
            {
                retList.removeLast();
            }
            else if (!(*itr).isEmpty())
            {
                retList.append(*itr);
            }
        }
    }
    else
    {
        QStringList oldList = pwd.split('/');
        QStringList newList = cleanedInput.split('/');

        for (auto itr = oldList.cbegin(); itr != oldList.cend(); itr++)
        {
            if ((*itr) == ".") {}
            else if ((*itr) == "..")
            {
                retList.removeLast();
            }
            else if (!(*itr).isEmpty())
            {
                retList.append(*itr);
            }
        }

        for (auto itr = newList.cbegin(); itr != newList.cend(); itr++)
        {
            if ((*itr) == ".") {}
            else if ((*itr) == "..")
            {
                retList.removeLast();
            }
            else if (!(*itr).isEmpty())
            {
                retList.append(*itr);
            }
        }
    }

    QString ret;

    for (auto itr = retList.cbegin(); itr != retList.cend(); itr++)
    {
        if (!(*itr).isEmpty())
        {
            ret.append('/');
            ret.append(*itr);
        }
    }

    return ret;
}

RemoteDataReply * AgaveHandler::performAuth(QString uname, QString passwd)
{   
    if (attemptingAuth || authGained)
    {
        return NULL;
    }
    authUname = uname;
    authPass = passwd;

    authEncloded = "Basic ";
    QByteArray rawAuth(uname.toLatin1());
    rawAuth.append(":");
    rawAuth.append(passwd);
    authEncloded.append(rawAuth.toBase64());

    AgaveTaskReply * parentReply = new AgaveTaskReply(retriveTaskGuide("fullAuth"),NULL,this,(QObject *)this);
    AgaveTaskReply * tmp = performAgaveQuery("authStep1",(QObject *)parentReply);

    if (tmp == NULL)
    {
        parentReply->deleteLater();
        return NULL;
    }
    attemptingAuth = true;
    parentReply->getTaskParamList()->insert("uname", uname);
    parentReply->getTaskParamList()->insert("passwd", passwd);
    return (RemoteDataReply *) parentReply;
}

RemoteDataReply * AgaveHandler::remoteLS(QString dirPath)
{
    QString tmp = getPathReletiveToCWD(dirPath);
    if ((tmp.isEmpty()) || (tmp == "/") || (tmp == ""))
    {
        tmp = "/";
        tmp.append(authUname);
    }

    AgaveTaskReply * theReply = performAgaveQuery("dirListing", tmp);
    theReply->getTaskParamList()->insert("dirPath", tmp);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::deleteFile(QString toDelete)
{
    QString toCheck = getPathReletiveToCWD(toDelete);

    AgaveTaskReply * theReply = performAgaveQuery("fileDelete", toCheck);
    theReply->getTaskParamList()->insert("toDelete", toCheck);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::moveFile(QString from, QString to)
{
    QString fromCheck = getPathReletiveToCWD(from);
    QString toCheck = getPathReletiveToCWD(to);
    //TODO: check stuff is valid
    AgaveTaskReply * theReply = performAgaveQuery("fileMove", fromCheck, toCheck);
    theReply->getTaskParamList()->insert("from", fromCheck);
    theReply->getTaskParamList()->insert("to", toCheck);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::copyFile(QString from, QString to)
{
    QString fromCheck = getPathReletiveToCWD(from);
    QString toCheck = getPathReletiveToCWD(to);
    //TODO: check stuff is valid
    AgaveTaskReply * theReply = performAgaveQuery("fileCopy", fromCheck, toCheck);
    theReply->getTaskParamList()->insert("from", fromCheck);
    theReply->getTaskParamList()->insert("to", toCheck);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::renameFile(QString fullName, QString newName)
{
    QString toCheck = getPathReletiveToCWD(fullName);
    //TODO: check that path and new name is valid
    AgaveTaskReply * theReply = performAgaveQuery("renameFile", toCheck, newName);
    theReply->getTaskParamList()->insert("fullName", toCheck);
    theReply->getTaskParamList()->insert("newName", newName);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::mkRemoteDir(QString location, QString newName)
{
    QString toCheck = getPathReletiveToCWD(location);
    //TODO: check that path and new name is valid
    AgaveTaskReply * theReply = performAgaveQuery("newFolder", toCheck, newName);
    theReply->getTaskParamList()->insert("location", toCheck);
    theReply->getTaskParamList()->insert("newName", newName);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::uploadFile(QString location, QString localFileName)
{
    QString toCheck = getPathReletiveToCWD(location);
    //TODO: check that path and local file exists
    AgaveTaskReply * theReply = performAgaveQuery("fileUpload", toCheck, localFileName);
    theReply->getTaskParamList()->insert("location", toCheck);
    theReply->getTaskParamList()->insert("localFileName", localFileName);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::uploadBuffer(QString location, QByteArray fileData)
{
    QString toCheck = getPathReletiveToCWD(location);
    //TODO: check that path and local file exists
    AgaveTaskReply * theReply = performAgaveQuery("filePipeUpload", toCheck, fileData);
    theReply->getTaskParamList()->insert("location", toCheck);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::downloadFile(QString localDest, QString remoteName)
{
    //TODO: check path and local path
    QString toCheck = getPathReletiveToCWD(remoteName);
    AgaveTaskReply * theReply = performAgaveQuery("fileDownload", toCheck, localDest);
    theReply->getTaskParamList()->insert("remoteName", toCheck);
    theReply->getTaskParamList()->insert("localDest", localDest);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::downloadBuffer(QString remoteName)
{
    //TODO: check path
    QString toCheck = getPathReletiveToCWD(remoteName);
    AgaveTaskReply * theReply = performAgaveQuery("filePipeDownload", toCheck);
    theReply->getTaskParamList()->insert("remoteName", toCheck);

    return (RemoteDataReply *) theReply;
}

AgaveTaskReply *AgaveHandler::getAgaveAppList()
{
    return performAgaveQuery("getAgaveList", NULL, NULL, NULL);
}

RemoteDataReply * AgaveHandler::runRemoteJob(QString jobName, QMultiMap<QString, QString> jobParameters, QString remoteWorkingDir)
{
    //This function is only for Agave Jobs
    AgaveTaskGuide * guideToCheck = retriveTaskGuide(jobName);
    if (guideToCheck == NULL)
    {
        return NULL;
    }
    if (guideToCheck->getRequestType() != AgaveRequestType::AGAVE_APP)
    {
        return NULL;
    }
    QString fullAgaveName = guideToCheck->getAgaveFullName();
    if (fullAgaveName.isEmpty())
    {
        emit sendFatalErrorMessage("Agave App does not have a full name");
        return NULL;
    }

    QJsonDocument rawJSONinput;
    QJsonObject rootObject;
    rootObject.insert("appId",QJsonValue(fullAgaveName));
    rootObject.insert("name",QJsonValue(fullAgaveName.append("-run")));
    QJsonObject inputList;
    QJsonObject paramList;

    QStringList expectedInputs = guideToCheck->getAgaveInputList();
    QStringList expectedParams = guideToCheck->getAgaveParamList();

    if ((!guideToCheck->getAgavePWDparam().isEmpty()) && (!remoteWorkingDir.isEmpty()))
    {
        //TODO: check path
        QString realPath = getPathReletiveToCWD(remoteWorkingDir);
        jobParameters.insert(guideToCheck->getAgavePWDparam(),realPath);
    }

    QList<QString> keyList = jobParameters.keys();
    for (auto itr = keyList.cbegin(); itr != keyList.cend(); itr++)
    {
        QList<QString> valueList = jobParameters.values(*itr);
        QJsonObject * objectToAddTo;

        if (expectedParams.contains(*itr))
        {
            objectToAddTo = &paramList;
        }
        else if (expectedInputs.contains(*itr))
        {
            objectToAddTo = &inputList;
        }
        else
        {
            return NULL;
        }

        if (valueList.size() > 1)
        {
            QJsonArray inList;
            for (int i = 0; i < valueList.size(); i++)
            {
                inList.append(QJsonValue(valueList.at(i)));
            }
            objectToAddTo->insert(*itr,QJsonValue(inList));
        }
        else
        {
            objectToAddTo->insert(*itr,QJsonValue(valueList.at(0)));
        }
    }
    QJsonValue inputListValue(inputList);
    QJsonValue paramListValue(paramList);
    rootObject.insert("inputs",inputListValue);
    rootObject.insert("parameters",paramListValue);
    rawJSONinput.setObject(rootObject);

    qDebug("%s",qPrintable(rawJSONinput.toJson()));

    AgaveTaskReply * theReply = performAgaveQuery("agaveAppStart", QString(rawJSONinput.toJson()));
    theReply->getTaskParamList()->insert("jobName", jobName);
    theReply->getTaskParamList()->insert("remoteWorkingDir", remoteWorkingDir);
    *(theReply->getTaskParamList()) += jobParameters;

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::getListOfJobs()
{
    return (RemoteDataReply *) performAgaveQuery("getJobList", NULL, NULL, NULL);
}

RemoteDataReply * AgaveHandler::getJobDetails(QString IDstr)
{
    AgaveTaskReply * theReply = performAgaveQuery("getJobDetails", IDstr);
    theReply->getTaskParamList()->insert("IDstr", IDstr);

    return (RemoteDataReply *) theReply;
}

RemoteDataReply * AgaveHandler::stopJob(QString IDstr)
{
    AgaveTaskReply * theReply = performAgaveQuery("stopJob", IDstr);
    theReply->getTaskParamList()->insert("IDstr", IDstr);

    return (RemoteDataReply *) theReply;
}

void AgaveHandler::registerAgaveAppInfo(QString agaveAppName, QString fullAgaveName, QStringList parameterList, QStringList inputList, QString workingDirParameter)
{
    AgaveTaskGuide * toInsert = new AgaveTaskGuide(agaveAppName, AgaveRequestType::AGAVE_APP);
    toInsert->setAgaveFullName(fullAgaveName);
    toInsert->setAgaveParamList(parameterList);
    toInsert->setAgaveInputList(inputList);
    toInsert->setAgavePWDparam(workingDirParameter);
    insertAgaveTaskGuide(toInsert);
}

RemoteDataReply * AgaveHandler::closeAllConnections()
{
    //Note: relogin is not yet supported
    AgaveTaskReply * waitHandle = new AgaveTaskReply(retriveTaskGuide("waitAll"),NULL,this,(QObject *)this);
    performingShutdown = true;
    if (waitHandle == NULL)
    {
        emit sendFatalErrorMessage("Unable to create shutdown object");
    }
    if ((clientEncoded != "") && (token != ""))
    {
        qDebug("Closing all connections sequence begins");
        performAgaveQuery("authRevoke", token);
        //maybe TODO: Remove client entry?
    }
    else
    {
        qDebug("Not logged in: quick shutdown");
        clearAllAuthTokens();
    }
    QObject::connect(this, SIGNAL(finishedAllTasks()), waitHandle, SLOT(rawTaskComplete()));
    if (pendingRequestCount == 0)
    {
        waitHandle->delayedPassThruReply(RequestState::GOOD);
    }

    return waitHandle;
}

void AgaveHandler::clearAllAuthTokens()
{
    attemptingAuth = false;
    authGained = false;

    authEncloded = "";
    clientEncoded = "";
    token = "";
    refreshToken = "";

    authUname = "";
    authPass = "";
    clientKey = "";
    clientSecret = "";
}

void AgaveHandler::setupTaskGuideList()
{
    AgaveTaskGuide * toInsert = NULL;

    toInsert = new AgaveTaskGuide("changeDir", AgaveRequestType::AGAVE_NONE);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fullAuth", AgaveRequestType::AGAVE_NONE);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("waitAll", AgaveRequestType::AGAVE_NONE);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep1", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/clients/v2/%1").arg(clientName));
    toInsert->setHeaderType(AuthHeaderType::PASSWD);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep1a", AgaveRequestType::AGAVE_DELETE);
    toInsert->setURLsuffix(QString("/clients/v2/%1").arg(clientName));
    toInsert->setHeaderType(AuthHeaderType::PASSWD);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep2", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/clients/v2/"));
    toInsert->setHeaderType(AuthHeaderType::PASSWD);
    toInsert->setPostParams(QString("clientName=%1&description=Client ID for SimCenter Wind GUI App").arg(clientName),0);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authStep3", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/token"));
    toInsert->setHeaderType(AuthHeaderType::CLIENT);
    toInsert->setPostParams("username=%1&password=%2&grant_type=password&scope=PRODUCTION",2);
    toInsert->setTokenFormat(true);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authRefresh", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/token"));
    toInsert->setHeaderType(AuthHeaderType::CLIENT);
    toInsert->setPostParams("grant_type=refresh_token&scope=PRODUCTION&refresh_token=%1",1);
    toInsert->setTokenFormat(true);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("authRevoke", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/revoke"));
    toInsert->setHeaderType(AuthHeaderType::CLIENT);
    toInsert->setPostParams("token=%1",1);
    toInsert->setAsInternal();
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("dirListing", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix((QString("/files/v2/listings/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileUpload", AgaveRequestType::AGAVE_UPLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileDownload", AgaveRequestType::AGAVE_DOWNLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("filePipeUpload", AgaveRequestType::AGAVE_PIPE_UPLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("filePipeDownload", AgaveRequestType::AGAVE_PIPE_DOWNLOAD);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileDelete", AgaveRequestType::AGAVE_DELETE);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("newFolder", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setPostParams("action=mkdir&path=%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("renameFile", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setPostParams("action=rename&path=%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileCopy", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setPostParams("action=copy&path=%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("fileMove", AgaveRequestType::AGAVE_PUT);
    toInsert->setURLsuffix((QString("/files/v2/media/system/%1/")).arg(storageNode));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setPostParams("action=move&path=%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("agaveAppStart", AgaveRequestType::AGAVE_PIPE_UPLOAD);
    toInsert->setURLsuffix(QString("/jobs/v2"));
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("getAgaveList", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/apps/v2"));
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("getJobList", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/jobs/v2"));
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("getJobDetails", AgaveRequestType::AGAVE_GET);
    toInsert->setURLsuffix(QString("/jobs/v2/"));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);

    toInsert = new AgaveTaskGuide("stopJob", AgaveRequestType::AGAVE_POST);
    toInsert->setURLsuffix(QString("/jobs/v2/"));
    toInsert->setDynamicURLParams("%1",1);
    toInsert->setPostParams("action=stop",0);
    toInsert->setHeaderType(AuthHeaderType::TOKEN);
    insertAgaveTaskGuide(toInsert);
}

void AgaveHandler::insertAgaveTaskGuide(AgaveTaskGuide * newGuide)
{
    QString taskName = newGuide->getTaskID();
    if (validTaskList.contains(taskName))
    {
        emit sendFatalErrorMessage("Invalid Task Guide List: Duplicate Name");
        return;
    }
    validTaskList.insert(taskName,newGuide);
}

AgaveTaskGuide * AgaveHandler::retriveTaskGuide(QString taskID)
{
    AgaveTaskGuide * ret;
    if (!validTaskList.contains(taskID))
    {
        emit sendFatalErrorMessage("Non-existant request requested.1");
        return NULL;
    }
    ret = validTaskList.value(taskID);
    if (taskID != ret->getTaskID())
    {
        emit sendFatalErrorMessage("Task Guide format error.");
    }
    return ret;
}

void AgaveHandler::forwardReplyToParent(AgaveTaskReply * agaveReply, RequestState replyState, QString * param1)
{
    AgaveTaskReply * parentReply = (AgaveTaskReply*)agaveReply->parent();
    if (parentReply == NULL)
    {
        return;
    }
    parentReply->delayedPassThruReply(replyState, param1);
}

void AgaveHandler::handleInternalTask(AgaveTaskReply * agaveReply, QNetworkReply * rawReply)
{
    if (agaveReply->getTaskGuide()->getTaskID() == "authRevoke")
    {
        qDebug("Auth revoke procedure complete");
        clearAllAuthTokens();
        return;
    }

    const QByteArray replyText = rawReply->readAll();

    QJsonParseError parseError;
    QJsonDocument parseHandler = QJsonDocument::fromJson(replyText, &parseError);

    if (parseHandler.isNull())
    {
        if (agaveReply->getTaskGuide()->getTaskID() == "getJobList")
        {
            qDebug("Job Listing failed");
        }
        else
        {
            forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
        }
        return;
    }

    qDebug("%s",qPrintable(parseHandler.toJson()));

    RequestState prelimResult = AgaveTaskReply::standardSuccessFailCheck(agaveReply->getTaskGuide(), &parseHandler);

    QString taskID = agaveReply->getTaskGuide()->getTaskID();

    if (prelimResult == RequestState::NO_CONNECT)
    {
        forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
        if ((taskID == "authStep1") || (taskID == "authStep1a") || (taskID == "authStep2") || (taskID == "authStep3"))
        {
            clearAllAuthTokens();
        }
    }

    if (taskID == "authStep1")
    {
        if (prelimResult == RequestState::GOOD)
        {
            if (performAgaveQuery("authStep1a", agaveReply->parent()) == NULL)
            {
                forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
                clearAllAuthTokens();
            }
        }
        else
        {
            QString messageData = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "message").toString();
            if (messageData == "Application not found")
            {
                if (performAgaveQuery("authStep2", agaveReply->parent()) == NULL)
                {
                    forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
                    clearAllAuthTokens();
                }
            }
            else if (messageData == "Login failed.Please recheck the username and password and try again.")
            {
                clearAllAuthTokens();
                forwardReplyToParent(agaveReply, RequestState::FAIL);
            }
            else
            {
                clearAllAuthTokens();
                forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
            }
        }
    }
    else if (taskID == "authStep1a")
    {
        if (prelimResult == RequestState::GOOD)
        {
            if (performAgaveQuery("authStep2", agaveReply->parent()) == NULL)
            {
                forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
                clearAllAuthTokens();
            }
        }
        else
        {
            clearAllAuthTokens();
            forwardReplyToParent(agaveReply, RequestState::FAIL);
        }
    }
    else if (taskID == "authStep2")
    {
        if (prelimResult == RequestState::GOOD)
        {
            clientKey = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, {"result", "consumerKey"}).toString();
            clientSecret = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, {"result", "consumerSecret"}).toString();

            if (clientKey.isEmpty() || clientSecret.isEmpty())
            {
                emit sendFatalErrorMessage("Client success does not yeild client auth data.");
                return;
            }

            clientEncoded = "Basic ";
            QByteArray rawAuth(clientKey.toLatin1());
            rawAuth.append(":");
            rawAuth.append(clientSecret);
            clientEncoded.append(rawAuth.toBase64());

            QStringList authList = {authUname, authPass};
            if (performAgaveQuery("authStep3", &authList, NULL, agaveReply->parent()) == NULL)
            {
                forwardReplyToParent(agaveReply, RequestState::NO_CONNECT);
                clearAllAuthTokens();
            }
        }
        else
        {
            clearAllAuthTokens();
            forwardReplyToParent(agaveReply, RequestState::FAIL);
        }
    }
    else if (taskID == "authStep3")
    {
        if (prelimResult == RequestState::GOOD)
        {
            token = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "access_token").toString().toLatin1();
            refreshToken = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "refresh_token").toString().toLatin1();

            if (token.isEmpty() || refreshToken.isEmpty())
            {
                clearAllAuthTokens();
                forwardReplyToParent(agaveReply, RequestState::FAIL);
            }
            else
            {
                tokenHeader = (QString("Bearer ").append(token)).toLatin1();

                authGained = true;
                attemptingAuth = false;

                forwardReplyToParent(agaveReply, RequestState::GOOD);
                qDebug("Login success.");
            }
        }
        else
        {
            clearAllAuthTokens();
            forwardReplyToParent(agaveReply, RequestState::FAIL);
        }
    }
    else if (taskID == "authRefresh")
    {
        if (prelimResult == RequestState::GOOD)
        {
            token = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "access_token").toString().toLatin1();
            refreshToken = AgaveTaskReply::retriveMainAgaveJSON(&parseHandler, "refresh_token").toString().toLatin1();

            if (token.isEmpty() || refreshToken.isEmpty())
            {
                emit sendFatalErrorMessage("Token refresh failure.");
                return;
            }
            //TODO: Will need more info here based on when, how and where refreshes are requested
        }
    }
    else
    {
        emit sendFatalErrorMessage("Non-existant internal request requested.");
    }
}

AgaveTaskReply * AgaveHandler::performAgaveQuery(QString queryName, QObject * parentReq)
{
    return performAgaveQuery(queryName, NULL, NULL, parentReq);
}

AgaveTaskReply * AgaveHandler::performAgaveQuery(QString queryName, QString param1, QObject * parentReq)
{
    QStringList paramList1 = {param1};
    return performAgaveQuery(queryName, &paramList1, NULL, parentReq);
}

AgaveTaskReply * AgaveHandler::performAgaveQuery(QString queryName, QString param1, QString param2, QObject * parentReq)
{
    QStringList paramList1 = {param1};
    QStringList paramList2 = {param2};
    return performAgaveQuery(queryName, &paramList1, &paramList2, parentReq);
}

AgaveTaskReply * AgaveHandler::performAgaveQuery(QString queryName, QStringList * paramList0, QStringList * paramList1, QObject * parentReq)
{
    //The network availabilty flag seems innacurate cross-platform
    //Failed task invocations return NULL from this function.
    /*
    if (networkHandle.networkAccessible() == QNetworkAccessManager::NotAccessible)
    {
        emit sendFatalErrorMessage("Network not available");
        return NULL;
    }
    */

    if ((performingShutdown) && (queryName != "authRevoke"))
    {
        qDebug("Rejecting request given during shutdown.");
        return NULL;
    }

    AgaveTaskGuide * taskGuide = retriveTaskGuide(queryName);

    if ((!authGained) && (taskGuide->getHeaderType() == AuthHeaderType::TOKEN))
    {
        return NULL;
    }

    QNetworkReply * qReply = internalQueryMethod(taskGuide, paramList0, paramList1);

    if (qReply == NULL)
    {
        return NULL;
    }
    pendingRequestCount++;

    QObject * parentObj = (QObject *) this;
    if (parentReq != NULL)
    {
        parentObj = parentReq;
    }

    AgaveTaskReply * ret = new AgaveTaskReply(taskGuide,qReply,this, parentObj);

    if (taskGuide->isInternal())
    {
        QObject::connect(ret, SIGNAL(haveInternalTaskReply(AgaveTaskReply*,QNetworkReply*)), this, SLOT(handleInternalTask(AgaveTaskReply*,QNetworkReply*)));
    }

    return ret;
}

QNetworkReply * AgaveHandler::internalQueryMethod(AgaveTaskGuide * taskGuide, QStringList * paramList1, QStringList * paramList2)
{
    QStringList * URLParams = NULL;
    QStringList * postParams = NULL;

    if (taskGuide->usesURLParams())
    {
        URLParams = paramList1;
        if (taskGuide->usesPostParms())
        {
            postParams = paramList2;
        }
    }
    else
    {
        if (taskGuide->usesPostParms())
        {
            postParams = paramList1;
        }
    }

    QStringList emptyList;
    if (URLParams == NULL)
    {
        URLParams = &emptyList;
    }

    if (postParams == NULL)
    {
        postParams = &emptyList;
    }

    QByteArray realURLsuffix = taskGuide->getURLsuffix().toLatin1();
    realURLsuffix.append(taskGuide->fillURLArgList(URLParams));
    QByteArray clientPostData = taskGuide->fillPostArgList(postParams);

    QByteArray * authHeader = NULL;
    if (taskGuide->getHeaderType() == AuthHeaderType::CLIENT)
    {
        authHeader = &clientEncoded;
    }
    else if (taskGuide->getHeaderType() == AuthHeaderType::PASSWD)
    {
        authHeader = &authEncloded;
    }
    else if (taskGuide->getHeaderType() == AuthHeaderType::REFRESH)
    {
        //TODO
    }
    else if (taskGuide->getHeaderType() == AuthHeaderType::TOKEN)
    {
        authHeader = &tokenHeader;
    }

    if ((taskGuide->getRequestType() == AgaveRequestType::AGAVE_POST) || (taskGuide->getRequestType() == AgaveRequestType::AGAVE_PUT))
    {
        //Note: For a put, the post data for this function is used as the put data for the HTTP request
        qDebug("Post data: %s", qPrintable(clientPostData));
        return finalizeAgaveRequest(taskGuide, realURLsuffix,
                         authHeader, clientPostData);
    }
    else if ((taskGuide->getRequestType() == AgaveRequestType::AGAVE_GET) || (taskGuide->getRequestType() == AgaveRequestType::AGAVE_DELETE))
    {

        qDebug("URL Req: %s", qPrintable(realURLsuffix));
        return finalizeAgaveRequest(taskGuide, realURLsuffix,
                         authHeader);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_UPLOAD)
    {
        //For agave upload, instead of post params, we have the full local file name
        QString fullFileName = clientPostData;
        QFile * fileHandle = new QFile(fullFileName);
        if (!fileHandle->open(QIODevice::ReadOnly))
        {
            fileHandle->deleteLater();
            return NULL;
        }
        qDebug("URL Req: %s", qPrintable(realURLsuffix));
        QByteArray filePostData = fullFileName.toLatin1();

        return finalizeAgaveRequest(taskGuide, realURLsuffix,
                         authHeader, filePostData, fileHandle);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_DOWNLOAD)
    {
        //For agave download, instead of post params, we have the full local file name
        QString fullFileName = clientPostData;
        QFile * fileHandle = new QFile(fullFileName);
        if (fileHandle->open(QIODevice::ReadOnly))
        {
            //If the file already exists, we do not overwrite
            //This should be checked by calling client, but we check it here too
            fileHandle->deleteLater();
            return NULL;
        }
        fileHandle->deleteLater();
        qDebug("URL Req: %s", qPrintable(realURLsuffix));
        QByteArray emptyPostData;

        return finalizeAgaveRequest(taskGuide, realURLsuffix,
                         authHeader, emptyPostData);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_DOWNLOAD)
    {
        QByteArray emptyPostData;

        return finalizeAgaveRequest(taskGuide, realURLsuffix,
                         authHeader, emptyPostData);
    }
    else if (taskGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_UPLOAD)
    {
        qDebug("Post Data: \n%s", qPrintable(clientPostData));
        QByteArray * uploadData = new QByteArray(clientPostData);
        //TODO: find a way to clean up the uploadData when no longer needed
        QBuffer * pipedData = new QBuffer(uploadData);
        pipedData->open(QBuffer::ReadOnly);
        qDebug("URL Req: %s", qPrintable(realURLsuffix));
        QByteArray filePostData = "JSON";

        return finalizeAgaveRequest(taskGuide, realURLsuffix,
                         authHeader, filePostData, pipedData);
    }
    else
    {
        emit sendFatalErrorMessage("Non-existant Agave request type requested.");
        return NULL;
    }

    return NULL;
}

QNetworkReply * AgaveHandler::finalizeAgaveRequest(AgaveTaskGuide * theGuide, QString urlAppend, QByteArray * authHeader, QByteArray postData, QIODevice * fileHandle)
{
    QNetworkReply * clientReply = NULL;

    QString activeURL = tenantURL;
    activeURL.append(urlAppend);

    QNetworkRequest * clientRequest = new QNetworkRequest();
    clientRequest->setUrl(QUrl(activeURL));

    //clientRequest->setRawHeader("User-Agent", "SimCenterWindGUI");
    if (theGuide->getRequestType() == AgaveRequestType::AGAVE_POST)
    {
        clientRequest->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    }

    if (authHeader != NULL)
    {
        if (authHeader->isEmpty())
        {
            emit sendFatalErrorMessage("Authorization request reply has no data in it");
            return NULL;
        }
        clientRequest->setRawHeader(QByteArray("Authorization"), *authHeader);
    }

    //Note: to suppress SSL warning for not having obsolete SSL versions, use
    // QT_LOGGING_RULES in the project build environment variables. Set to:
    // qt.network.ssl.warning=false
    clientRequest->setSslConfiguration(SSLoptions);

    qDebug("%s", qPrintable(clientRequest->url().url()));

    if ((theGuide->getRequestType() == AgaveRequestType::AGAVE_GET) || (theGuide->getRequestType() == AgaveRequestType::AGAVE_DOWNLOAD)
            || (theGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_DOWNLOAD))
    {
        clientReply = networkHandle.get(*clientRequest);
    }
    else if (theGuide->getRequestType() == AgaveRequestType::AGAVE_POST)
    {
        clientReply = networkHandle.post(*clientRequest, postData);
    }
    else if (theGuide->getRequestType() == AgaveRequestType::AGAVE_PUT)
    {
        clientReply = networkHandle.put(*clientRequest, postData);
    }
    else if (theGuide->getRequestType() == AgaveRequestType::AGAVE_DELETE)
    {
        clientReply = networkHandle.deleteResource(*clientRequest);
    }
    else if ((theGuide->getRequestType() == AgaveRequestType::AGAVE_UPLOAD) || (theGuide->getRequestType() == AgaveRequestType::AGAVE_PIPE_UPLOAD))
    {
        QHttpMultiPart * fileUpload = new QHttpMultiPart(QHttpMultiPart::FormDataType);

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-strem"));
        QString tempString = "form-data; name=\"fileToUpload\"; filename=\"%1\"";
        tempString = tempString.arg(QString(postData));
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(tempString));

        filePart.setBodyDevice(fileHandle);
        //Following line is to insure deletion of the file handle later, when the parent is deleted
        fileHandle->setParent(fileUpload);

        fileUpload->append(filePart);

        clientReply = networkHandle.post(*clientRequest, fileUpload);

        //Following line insures Mulipart object deleted when the network reply is
        fileUpload->setParent(clientReply);
    }

    return clientReply;
}

QString AgaveHandler::getTenantURL()
{
    return tenantURL;
}

void AgaveHandler::forwardAgaveError(QString errorText)
{
    emit sendFatalErrorMessage(errorText);
}
