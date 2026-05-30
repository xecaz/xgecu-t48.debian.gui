// SPDX-License-Identifier: GPL-3.0-or-later
#include "Programmer.h"

#include "ProgrammerWorker.h"

Programmer::Programmer(QObject *parent)
    : QObject(parent), m_worker(new ProgrammerWorker)
{
    qRegisterMetaType<MemArea>("MemArea");
    qRegisterMetaType<DeviceInfo>("DeviceInfo");
    qRegisterMetaType<ReadResult>("ReadResult");
    qRegisterMetaType<VerifyResult>("VerifyResult");
    qRegisterMetaType<ChipIdResult>("ChipIdResult");
    qRegisterMetaType<WriteResult>("WriteResult");
    qRegisterMetaType<FuseSet>("FuseSet");
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &ProgrammerWorker::detected, this, &Programmer::detected);
    connect(m_worker, &ProgrammerWorker::detectionFailed, this, &Programmer::detectionFailed);
    connect(m_worker, &ProgrammerWorker::chipOpened, this, &Programmer::chipOpened);
    connect(m_worker, &ProgrammerWorker::chipOpenFailed, this, &Programmer::chipOpenFailed);
    connect(m_worker, &ProgrammerWorker::progress, this, &Programmer::progress);
    connect(m_worker, &ProgrammerWorker::readFinished, this, &Programmer::readFinished);
    connect(m_worker, &ProgrammerWorker::verifyFinished, this, &Programmer::verifyFinished);
    connect(m_worker, &ProgrammerWorker::chipIdFinished, this, &Programmer::chipIdFinished);
    connect(m_worker, &ProgrammerWorker::eraseFinished, this, &Programmer::eraseFinished);
    connect(m_worker, &ProgrammerWorker::writeFinished, this, &Programmer::writeFinished);
    connect(m_worker, &ProgrammerWorker::fusesAvailable, this, &Programmer::fusesAvailable);
    connect(m_worker, &ProgrammerWorker::fusesRead, this, &Programmer::fusesRead);
    connect(m_worker, &ProgrammerWorker::fuseWriteFinished, this, &Programmer::fuseWriteFinished);
    connect(m_worker, &ProgrammerWorker::error, this, &Programmer::error);

    m_thread.start();
}

Programmer::~Programmer()
{
    m_thread.quit();
    m_thread.wait();
}

void Programmer::setInfoicPath(const QString &path)
{
    QMetaObject::invokeMethod(m_worker, "setInfoicPath", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void Programmer::setLogicicPath(const QString &path)
{
    QMetaObject::invokeMethod(m_worker, "setLogicicPath", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void Programmer::detect()
{
    QMetaObject::invokeMethod(m_worker, "detect", Qt::QueuedConnection);
}

void Programmer::openChip(const QString &name)
{
    QMetaObject::invokeMethod(m_worker, "openChip", Qt::QueuedConnection,
                              Q_ARG(QString, name));
}

void Programmer::readMemory(MemArea area)
{
    QMetaObject::invokeMethod(m_worker, "readMemory", Qt::QueuedConnection,
                              Q_ARG(MemArea, area));
}

void Programmer::verifyMemory(MemArea area, const QByteArray &expected)
{
    QMetaObject::invokeMethod(m_worker, "verifyMemory", Qt::QueuedConnection,
                              Q_ARG(MemArea, area),
                              Q_ARG(QByteArray, expected));
}

void Programmer::writeMemory(MemArea area, const QByteArray &data,
                             bool force, bool autoVerify)
{
    QMetaObject::invokeMethod(m_worker, "writeMemory", Qt::QueuedConnection,
                              Q_ARG(MemArea, area),
                              Q_ARG(QByteArray, data),
                              Q_ARG(bool, force),
                              Q_ARG(bool, autoVerify));
}

void Programmer::detectChipId()
{
    QMetaObject::invokeMethod(m_worker, "detectChipId", Qt::QueuedConnection);
}

void Programmer::eraseChip(bool force)
{
    QMetaObject::invokeMethod(m_worker, "eraseChip", Qt::QueuedConnection,
                              Q_ARG(bool, force));
}

void Programmer::readFuses()
{
    QMetaObject::invokeMethod(m_worker, "readFuses", Qt::QueuedConnection);
}

void Programmer::writeFuses(const FuseSet &fuses)
{
    QMetaObject::invokeMethod(m_worker, "writeFuses", Qt::QueuedConnection,
                              Q_ARG(FuseSet, fuses));
}

void Programmer::requestCancel()
{
    m_worker->requestCancel();  // thread-safe atomic store
}
