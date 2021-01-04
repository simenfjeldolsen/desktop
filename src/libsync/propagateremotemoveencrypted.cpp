#include "propagateremotemoveencrypted.h"
#include "clientsideencryptionjobs.h"
#include "clientsideencryption.h"
#include "owncloudpropagator.h"
#include "account.h"
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QFileInfo>
#include <QDir>

using namespace OCC;

Q_LOGGING_CATEGORY(PROPAGATE_REMOTE_MOVE_ENCRYPTED, "nextcloud.sync.propagator.remote.move.encrypted")

PropagateRemoteMoveEncrypted::PropagateRemoteMoveEncrypted(OwncloudPropagator *propagator, SyncFileItemPtr item, QObject *parent)
    : QObject(parent)
    , _propagator(propagator)
    , _item(item)
{

}

QByteArray PropagateRemoteMoveEncrypted::folderToken()
{
    return _folderToken;
}

void PropagateRemoteMoveEncrypted::start()
{
    if (_propagator->_abortRequested)
        return;

    QString origin = _propagator->adjustRenamedPath(_item->_file);
    qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << origin << _item->_renameTarget;

    QString targetFile(_propagator->fullLocalPath(_item->_renameTarget));

    if (origin == _item->_renameTarget) {
        // The parent has been renamed already so there is nothing more to do.
        emit finished(true);
        return;
    }

    QString remoteSource = _propagator->fullRemotePath(origin);
    QString remoteDestination = QDir::cleanPath(_propagator->account()->davUrl().path() + _propagator->fullRemotePath(_item->_renameTarget));

    if (!_item->_encryptedFileName.isEmpty()) {
        QFileInfo info(_item->_encryptedFileName);
        QString root_dir = info.path();
        QFileInfo infoRenamed(_item->_renameTarget);
        QString renameFileName = infoRenamed.fileName();
        auto job = new LsColJob(_propagator->account(), root_dir, this);
        job->setProperties({"resourcetype", "http://owncloud.org/ns:fileid"});
        connect(job, &LsColJob::directoryListingSubfolders, this, &PropagateRemoteMoveEncrypted::slotFolderEncryptedIdReceived);
        connect(job, &LsColJob::finishedWithError, this, &PropagateRemoteMoveEncrypted::taskFailed);

        job->start();
    } else if (_item->_isEncrypted) {

        QFileInfo info("/" + QString::fromUtf8(_item->_fileId));
        QString path = info.path();
        auto job = new LsColJob(_propagator->account(), remoteSource, this);
        job->setProperties({"resourcetype", "http://owncloud.org/ns:fileid"});
        connect(job, &LsColJob::directoryListingSubfolders, this, [=](const QStringList &items) {

            auto job = qobject_cast<LsColJob *>(sender());
            const auto& folderInfo = job->_folderInfos.value(items.first());

            _folderId = folderInfo.fileId;

            auto lockJob = new LockEncryptFolderApiJob(_propagator->account(), folderInfo.fileId, this);
            connect(lockJob, &LockEncryptFolderApiJob::success, this, [=] (const QByteArray& fileId, const QByteArray& token) {
                Q_UNUSED(fileId) // Should we skip file deletion in case of failure?
                _folderToken = token;
                auto job = new GetMetadataApiJob(_propagator->account(), _item->_fileId);
                connect(job, &GetMetadataApiJob::jsonReceived, this, [=] (const QJsonDocument &json, int statusCode) {
                    Q_UNUSED(json) // Should we skip file deletion in case of failure?
                    Q_UNUSED(statusCode) // Should we skip file deletion in case of failure?
                    Q_UNUSED(fileId) // Should we skip file deletion in case of failure?

                    FolderMetadata* folderMetadata = new FolderMetadata(_propagator->account(), json.toJson(QJsonDocument::Compact), statusCode);

                    QFileInfo info(_propagator->fullLocalPath(_item->_file));
                    const QString fileName = info.fileName();

                    // Find existing metadata for this file
                    bool found = false;
                    EncryptedFile encryptedFile;
                    const QVector<EncryptedFile> files = folderMetadata->files();

                    for(const EncryptedFile &file : files) {
                      if (file.originalFilename == fileName) {
                        encryptedFile = file;
                        found = true;
                      }
                    }

                    folderMetadata->removeEncryptedFile(encryptedFile);

                    if (found) {
                        encryptedFile.originalFilename = _item->_renameTarget;
                    }

                    folderMetadata->addEncryptedFile(encryptedFile);

                    if (!_folderId.isEmpty() && !_folderToken.isEmpty()) {
                        auto updateMetaDataJob = new UpdateMetadataApiJob(_propagator->account(),
                                                          _folderId,
                                                          folderMetadata->encryptedMetadata(),
                                                          _folderToken);

                        connect(updateMetaDataJob, &UpdateMetadataApiJob::success, this, [=](const QByteArray& fileId) {
                            Q_UNUSED(fileId)
                            /*_job = new MoveJob(_propagator->account(), remoteSource, remoteDestination, this);
                            connect(_job.data(), &MoveJob::finishedSignal, this, &PropagateRemoteMove::slotMoveJobFinished);
                            _propagator->_activeJobList.append(this);
                            _job->start();*/
                        });
                        connect(updateMetaDataJob, &UpdateMetadataApiJob::error, this, [=](const QByteArray& fileId, int httpReturnCode) {
                            Q_UNUSED(fileId) // Should we skip file deletion in case of failure?
                            Q_UNUSED(httpReturnCode) // Should we skip file deletion in case of failure?

                            if (!_folderToken.isEmpty()) {
                                auto unlockJob = new UnlockEncryptFolderApiJob(_propagator->account(),
                                                                               folderInfo.fileId, _folderToken, this);

                                connect(unlockJob, &UnlockEncryptFolderApiJob::success, [=](const QByteArray& fileId) {
                                    Q_UNUSED(fileId)
                                    int a = 5;
                                    a = 6;
                                });
                                connect(unlockJob, &UnlockEncryptFolderApiJob::error, this, [=] (const QByteArray& fileId, int httpReturnCode) {
                                    int a = 5;
                                    a = 6;
                                });

                                unlockJob->start();

                                _folderToken = "";
                                _folderId = "";
                            }
                        });
                        job->start();
                        updateMetaDataJob->start();
                    }

                });
                connect(job, &GetMetadataApiJob::error, this, [=](const QByteArray& fileId, int httpReturnCode) {
                    Q_UNUSED(fileId) // Should we skip file deletion in case of failure?
                    Q_UNUSED(httpReturnCode) // Should we skip file deletion in case of failure?
                    if (!_folderToken.isEmpty()) {
                        auto unlockJob = new UnlockEncryptFolderApiJob(_propagator->account(),
                                                                       folderInfo.fileId, _folderToken, this);

                        connect(unlockJob, &UnlockEncryptFolderApiJob::success, [=] (const QByteArray& fileId){
                            Q_UNUSED(fileId)
                            int a = 5;
                            a = 6;
                        });
                        connect(unlockJob, &UnlockEncryptFolderApiJob::error, this, [=] (const QByteArray& fileId, int httpReturnCode) {
                            int a = 5;
                            a = 6;
                        });

                        unlockJob->start();

                        _folderToken = "";
                        _folderId = "";
                    }
                });
                job->start();
            });
            connect(lockJob, &LockEncryptFolderApiJob::error, this, [=] (const QByteArray& fileId, int httpdErrorCode) {
                Q_UNUSED(fileId) // Should we skip file deletion in case of failure?
               /* Q_UNUSED(httpdErrorCode) // Should we skip file deletion in case of failure?
                _job = new MoveJob(_propagator->account(), remoteSource, remoteDestination, this);
                connect(_job.data(), &MoveJob::finishedSignal, this, &PropagateRemoteMove::slotMoveJobFinished);
                _propagator->_activeJobList.append(this);
                _job->start();*/
            });
            lockJob->start();

        });
        connect(job, &LsColJob::finishedWithError, this, [](QNetworkReply *reply) {
            auto replyError = reply->error();
            auto replyErrorString = reply->errorString();
            int a = 5;
            a = 6;
        });
        job->start();

        //unlockJob->start();

       /* auto job = new OCC::UnSetEncryptionFlagApiJob(_propagator->account(), _item->_folderId, this);
        connect(job, &OCC::UnSetEncryptionFlagApiJob::success, this, [this, remoteSource, remoteDestination] (const QByteArray fileId) {
            Q_UNUSED(fileId) // Should we skip file deletion in case of failure?
            _job = new MoveJob(_propagator->account(), remoteSource, remoteDestination, this);
            connect(_job.data(), &MoveJob::finishedSignal, this, &PropagateRemoteMove::slotMoveJobFinished);
            _propagator->_activeJobList.append(this);
            _job->start();
        });
        connect(job, &OCC::UnSetEncryptionFlagApiJob::error, this, [this] (const QByteArray fileId, int httpReturnCode) {
            Q_UNUSED(fileId)
            Q_UNUSED(httpReturnCode)

            int a = 5;
            a = 6;
        });
        //job->start();*/
    }
}

void PropagateRemoteMoveEncrypted::slotFolderEncryptedIdReceived(const QStringList &list)
{
    auto job = qobject_cast<LsColJob *>(sender());
    const auto& folderInfo = job->_folderInfos.value(list.first());

    slotTryLock(folderInfo.fileId);
}

void PropagateRemoteMoveEncrypted::slotTryLock(const QByteArray &folderId)
{
    auto lockJob = new LockEncryptFolderApiJob(_propagator->account(), folderId, this);
    connect(lockJob, &LockEncryptFolderApiJob::success, this, &PropagateRemoteMoveEncrypted::slotFolderLockedSuccessfully);
    connect(lockJob, &LockEncryptFolderApiJob::error, this, &PropagateRemoteMoveEncrypted::taskFailed);
    lockJob->start();
}

void PropagateRemoteMoveEncrypted::slotFolderLockedSuccessfully(const QByteArray &folderId, const QByteArray &token)
{
    qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Folder id" << folderId << "Locked Successfully for Upload, Fetching Metadata";
    _folderLocked = true;
    _folderToken = token;
    _folderId = folderId;

    auto job = new GetMetadataApiJob(_propagator->account(), _folderId);

    connect(job, &GetMetadataApiJob::jsonReceived, this, &PropagateRemoteMoveEncrypted::slotFolderEncryptedMetadataReceived);
    connect(job, &GetMetadataApiJob::error, this, &PropagateRemoteMoveEncrypted::taskFailed);

    job->start();
}

void PropagateRemoteMoveEncrypted::slotFolderEncryptedMetadataReceived(const QJsonDocument &json, int statusCode)
{
    if (statusCode == 404) {
        qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Metadata not found, ignoring.";
        emit finished(true);
        return;
    }

    qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Metadata Received, Preparing it for the new file.";

    Q_UNUSED(json) // Should we skip file deletion in case of failure?
    Q_UNUSED(statusCode) // Should we skip file deletion in case of failure?
    Q_UNUSED(_folderId) // Should we skip file deletion in case of failure?

    FolderMetadata* folderMetadata = new FolderMetadata(_propagator->account(), json.toJson(QJsonDocument::Compact), statusCode);

    QFileInfo info(_propagator->fullLocalPath(_item->_encryptedFileName));
    const QString fileName = info.fileName();

    // Find existing metadata for this file
    bool found = false;
    EncryptedFile encryptedFile;
    const QVector<EncryptedFile> files = folderMetadata->files();

    for(const EncryptedFile &file : files) {
      if (file.encryptedFilename == fileName) {
        encryptedFile = file;
        found = true;
      }
    }

    folderMetadata->removeEncryptedFile(encryptedFile);

    if (found) {
        QFileInfo infoRenamed(_item->_renameTarget);
        QString renameFileName = infoRenamed.fileName();
        encryptedFile.originalFilename = renameFileName;
    }

    folderMetadata->addEncryptedFile(encryptedFile);

    if (!_folderId.isEmpty() && !_folderToken.isEmpty()) {
        auto job = new UpdateMetadataApiJob(_propagator->account(),
                                          _folderId,
                                          folderMetadata->encryptedMetadata(),
                                          _folderToken);

        connect(job, &UpdateMetadataApiJob::success, this, [=](const QByteArray& fileId) {
            Q_UNUSED(fileId)
            connect(this, &PropagateRemoteMoveEncrypted::folderUnlocked, this, [this] { emit finished(true); });
            unlockFolder();
        });
        connect(job, &UpdateMetadataApiJob::error, this, [=](const QByteArray& fileId, int httpReturnCode) {
            Q_UNUSED(fileId) // Should we skip file deletion in case of failure?
            Q_UNUSED(httpReturnCode) // Should we skip file deletion in case of failure?

            taskFailed();
        });

        // Encrypt File!
        FolderMetadata metadata(_propagator->account(), json.toJson(QJsonDocument::Compact), statusCode);

        QFileInfo info(_propagator->fullLocalPath(_item->_file));
        const QString fileName = info.fileName();

        // Find existing metadata for this file
        bool found = false;
        const QVector<EncryptedFile> files = metadata.files();
        for (const EncryptedFile &file : files) {
            if (file.originalFilename == fileName) {
                metadata.removeEncryptedFile(file);
                found = true;
                break;
            }
        }

        if (!found) {
            // The removed file was not in the JSON so nothing else to do
            taskFailed();
            return;
        }

        qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Metadata updated, sending to the server.";

        job->start();
    }
}

void PropagateRemoteMoveEncrypted::unlockFolder()
{
    if (!_folderLocked) {
        emit folderUnlocked();
        return;
    }

    qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Unlocking folder" << _folderId;
    auto unlockJob = new UnlockEncryptFolderApiJob(_propagator->account(),
                                                   _folderId, _folderToken, this);

    connect(unlockJob, &UnlockEncryptFolderApiJob::success, [this] {
        qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Folder successfully unlocked" << _folderId;
        _folderLocked = false;
        emit folderUnlocked();
    });
    connect(unlockJob, &UnlockEncryptFolderApiJob::error, this, &PropagateRemoteMoveEncrypted::taskFailed);
    unlockJob->start();
}

void PropagateRemoteMoveEncrypted::taskFailed()
{
    qCDebug(PROPAGATE_REMOTE_MOVE_ENCRYPTED) << "Task failed of job" << sender();
    if (_folderLocked) {
        connect(this, &PropagateRemoteMoveEncrypted::folderUnlocked, this, [this] { emit finished(false); });
        unlockFolder();
    } else {
        emit finished(false);
    }
}
