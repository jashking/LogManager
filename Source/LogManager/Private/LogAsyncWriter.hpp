// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#pragma once

#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/OutputDeviceFile.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/Event.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeCounter.h"
#include "Serialization/Archive.h"

class FLogAsyncWriter : public FRunnable, public FArchive
{
    enum EConstants
    {
        InitialBufferSize = 128 * 1024
    };

    /** Thread to run the worker FRunnable on. Serializes the ring buffer to disk. */
    FRunnableThread* Thread;
    /** Stops this thread */
    FThreadSafeCounter StopTaskCounter;

    /** Writer archive */
    FArchive& Ar;
    /** Data ring buffer */
    TArray<uint8> Buffer;
    /** [WRITER THREAD] Position where the unserialized data starts in the buffer */
    int32 BufferStartPos;
    /** [CLIENT THREAD] Position where the unserialized data ends in the buffer (such as if (BufferEndPos > BufferStartPos) Length = BufferEndPos - BufferStartPos; */
    int32 BufferEndPos;
    /** [CLIENT THREAD] Sync object for the buffer pos */
    FCriticalSection BufferPosCritical;
    /** [CLIENT/WRITER THREAD] Outstanding serialize request counter. This is to make sure we flush all requests. */
    FThreadSafeCounter SerializeRequestCounter;

    /** [WRITER THREAD] Last time the archive was flushed. used in threaded situations to flush the underlying archive at a certain maximum rate. */
    double LastArchiveFlushTime;

    /** [WRITER THREAD] Archive flush interval. */
    double ArchiveFlushIntervalSec;

    /** [WRITER THREAD] Serialize the contents of the ring buffer to disk */
    void SerializeBufferToArchive()
    {
        while (SerializeRequestCounter.GetValue() > 0)
        {
            // Grab a local copy of the end pos. It's ok if it changes on the client thread later on.
            // We won't be modifying it anyway and will later serialize new data in the next iteration.
            // Here we only serialize what we know exists at the beginning of this function.
            int32 ThisThreadEndPos = BufferEndPos;

            if (ThisThreadEndPos >= BufferStartPos)
            {
                Ar.Serialize(Buffer.GetData() + BufferStartPos, ThisThreadEndPos - BufferStartPos);
            }
            else
            {
                // Data is wrapped around the ring buffer
                Ar.Serialize(Buffer.GetData() + BufferStartPos, Buffer.Num() - BufferStartPos);
                Ar.Serialize(Buffer.GetData(), BufferEndPos);
            }
            // Modify the start pos. Only the worker thread modifies this value so it's ok to not guard it with a critical section.
            BufferStartPos = ThisThreadEndPos;

            // Flush the archive periodically if running on a separate thread
            if (Thread)
            {
                if ((FPlatformTime::Seconds() - LastArchiveFlushTime) > ArchiveFlushIntervalSec)
                {
                    Ar.Flush();
                    LastArchiveFlushTime = FPlatformTime::Seconds();
                }
            }

            // Decrement the request counter, we now know we serialized at least one request.
            // We might have serialized more requests but it's irrelevant, the counter will go down to 0 eventually
            SerializeRequestCounter.Decrement();
        }
    }

    /** [CLIENT THREAD] Flush the memory buffer (doesn't force the archive to flush). Can only be used from inside of BufferPosCritical lock. */
    void FlushBuffer()
    {
        SerializeRequestCounter.Increment();
        if (!Thread)
        {
            SerializeBufferToArchive();
        }
        while (SerializeRequestCounter.GetValue() != 0)
        {
            FPlatformProcess::SleepNoStats(0);
        }
        // Make sure there's been no unexpected concurrency
        check(SerializeRequestCounter.GetValue() == 0);
    }

public:

    FLogAsyncWriter(FArchive& InAr)
        : Thread(nullptr)
        , Ar(InAr)
        , BufferStartPos(0)
        , BufferEndPos(0)
        , LastArchiveFlushTime(0.0)
        , ArchiveFlushIntervalSec(0.2)
    {
        Buffer.AddUninitialized(InitialBufferSize);

        float CommandLineInterval = 0.0;
        if (FParse::Value(FCommandLine::Get(), TEXT("LOGFLUSHINTERVAL="), CommandLineInterval))
        {
            ArchiveFlushIntervalSec = CommandLineInterval;
        }

        if (FPlatformProcess::SupportsMultithreading())
        {
            FString WriterName = FString::Printf(TEXT("FAsyncWriter_%s"), *FPaths::GetBaseFilename(Ar.GetArchiveName()));
            Thread = FRunnableThread::Create(this, *WriterName, 0, TPri_BelowNormal);
        }
    }

    virtual ~FLogAsyncWriter()
    {
        Flush();
        delete Thread;
        Thread = nullptr;
    }

    /** [CLIENT THREAD] Serialize data to buffer that will later be saved to disk by the async thread */
    virtual void Serialize(void* InData, int64 Length) override
    {
        if (!InData || Length <= 0)
        {
            return;
        }

        const uint8* Data = (uint8*)InData;

        FScopeLock WriteLock(&BufferPosCritical);

        // Store the local copy of the current buffer start pos. It may get moved by the worker thread but we don't
        // care about it too much because we only modify BufferEndPos. Copy should be atomic enough. We only use it
        // for checking the remaining space in the buffer so underestimating is ok.
        {
            const int32 ThisThreadStartPos = BufferStartPos;
            // Calculate the remaining size in the ring buffer
            const int32 BufferFreeSize = ThisThreadStartPos <= BufferEndPos ? (Buffer.Num() - BufferEndPos + ThisThreadStartPos) : (ThisThreadStartPos - BufferEndPos);
            // Make sure the buffer is BIGGER than we require otherwise we may calculate the wrong (0) buffer EndPos for StartPos = 0 and Length = Buffer.Num()
            if (BufferFreeSize <= Length)
            {
                // Force the async thread to call SerializeBufferToArchive even if it's currently empty
                FlushBuffer();

                // Resize the buffer if needed
                if (Length >= Buffer.Num())
                {
                    // Keep the buffer bigger than we require so that % Buffer.Num() does not return 0 for Lengths = Buffer.Num()
                    Buffer.SetNumUninitialized(Length + 1);
                }
            }
        }

        // We now know there's enough space in the buffer to copy data
        const int32 WritePos = BufferEndPos;
        if ((WritePos + Length) <= Buffer.Num())
        {
            // Copy straight into the ring buffer
            FMemory::Memcpy(Buffer.GetData() + WritePos, Data, Length);
        }
        else
        {
            // Wrap around the ring buffer
            int32 BufferSizeToEnd = Buffer.Num() - WritePos;
            FMemory::Memcpy(Buffer.GetData() + WritePos, Data, BufferSizeToEnd);
            FMemory::Memcpy(Buffer.GetData(), Data + BufferSizeToEnd, Length - BufferSizeToEnd);
        }

        // Update the end position and let the async thread know we need to write to disk
        BufferEndPos = (BufferEndPos + Length) % Buffer.Num();
        SerializeRequestCounter.Increment();

        // No async thread? Serialize now.
        if (!Thread)
        {
            SerializeBufferToArchive();
        }
    }

    /** Flush all buffers to disk */
    void Flush()
    {
        FScopeLock WriteLock(&BufferPosCritical);
        FlushBuffer();
        // At this point the serialize queue should be empty and the writer thread should no longer
        // be accessing the Archive so we should be safe to flush it from here.
        Ar.Flush();
    }

    //~ Begin FRunnable Interface.
    virtual bool Init()
    {
        return true;
    }
    virtual uint32 Run()
    {
        while (StopTaskCounter.GetValue() == 0)
        {
            if (SerializeRequestCounter.GetValue() > 0)
            {
                SerializeBufferToArchive();
            }
            else if ((FPlatformTime::Seconds() - LastArchiveFlushTime) > ArchiveFlushIntervalSec)
            {
                SerializeRequestCounter.Increment();
            }
            else
            {
                FPlatformProcess::Sleep(0.01f);
            }
        }
        return 0;
    }
    virtual void Stop()
    {
        StopTaskCounter.Increment();
    }
    //~ End FRunnable Interface
};
