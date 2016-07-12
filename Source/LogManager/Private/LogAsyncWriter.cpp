// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#include "LogManagerPrivatePCH.h"

FLogAsyncWriter::FLogAsyncWriter(FArchive& InAr)
    : Thread(nullptr)
    , Ar(InAr)
    , BufferStartPos(0)
    , BufferEndPos(0)
{
    Buffer.AddUninitialized(InitialBufferSize);
    BufferDirtyEvent = FPlatformProcess::GetSynchEventFromPool();
    BufferEmptyEvent = FPlatformProcess::GetSynchEventFromPool(true);
    BufferEmptyEvent->Trigger();

    if (FPlatformProcess::SupportsMultithreading())
    {
        FString WriterName = FString::Printf(TEXT("FAsyncWriter_%s"), *FPaths::GetBaseFilename(Ar.GetArchiveName()));
        Thread = FRunnableThread::Create(this, *WriterName, 0, TPri_BelowNormal);
    }
}

FLogAsyncWriter::~FLogAsyncWriter()
{
    Flush();

    if (Thread)
    {
        delete Thread;
        Thread = nullptr;
    }

    FPlatformProcess::ReturnSynchEventToPool(BufferDirtyEvent);
    BufferDirtyEvent = nullptr;
    FPlatformProcess::ReturnSynchEventToPool(BufferEmptyEvent);
    BufferEmptyEvent = nullptr;
}

void FLogAsyncWriter::Serialize(uint8* Data, int32 Length)
{
    if (!Data || Length <= 0)
    {
        return;
    }

    BufferEmptyEvent->Reset();
    FScopeLock WriteLock(&BufferPosCritical);

    // Store the local copy of the current buffer start pos. It may get moved by the worker thread but we don't
    // care about it too much because we only modify BufferEndPos. Copy should be atomic enough. We only use it
    // for checking the remaining space in the buffer so underestimating is ok.
    int32 ThisThreadStartPos = BufferStartPos;
    // Calculate the remaining size in the ring buffer
    int32 BufferFreeSize = ThisThreadStartPos <= BufferEndPos ? (Buffer.Num() - BufferEndPos + ThisThreadStartPos) : (ThisThreadStartPos - BufferEndPos);
    if (BufferFreeSize >= Length)
    {
        // There's enough space in the buffer to copy data
        int32 WritePos = BufferEndPos;
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
            FMemory::Memcpy(Buffer.GetData() + 0, Data + BufferSizeToEnd, Length - BufferSizeToEnd);
        }
    }
    else
    {
        // Force the async thread to call SerializeBufferToArchive even if it's currently empty
        BufferDirtyEvent->Trigger();
        FlushBuffer();
        // Resize the buffer if needed
        if (Length > Buffer.Num())
        {
            Buffer.AddUninitialized(Length - Buffer.Num());
        }
        // Now we can copy to the empty buffer
        FMemory::Memcpy(Buffer.GetData(), Data, Length);
    }
    // Update the end position and let the async thread know we need to write to disk
    BufferEndPos = (BufferEndPos + Length) % Buffer.Num();
    BufferDirtyEvent->Trigger();

    // No async thread? Serialize now.
    if (!Thread)
    {
        SerializeBufferToArchive();
    }
}

void FLogAsyncWriter::Flush()
{
    FScopeLock WriteLock(&BufferPosCritical);
    FlushBuffer();
    Ar.Flush();
}

bool FLogAsyncWriter::Init()
{
    return true;
}

uint32 FLogAsyncWriter::Run()
{
    while (StopTaskCounter.GetValue() == 0)
    {
        if (BufferDirtyEvent->Wait(10))
        {
            SerializeBufferToArchive();
        }
    }
    return 0;
}

void FLogAsyncWriter::Stop()
{
    StopTaskCounter.Increment();
}

void FLogAsyncWriter::SerializeBufferToArchive()
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
    // Let the client threads know we're done (this is used by FlushBuffer).
    BufferEmptyEvent->Trigger();
}

void FLogAsyncWriter::FlushBuffer()
{
    BufferDirtyEvent->Trigger();
    if (!Thread)
    {
        SerializeBufferToArchive();
    }
    BufferEmptyEvent->Wait();
}