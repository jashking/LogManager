// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#pragma once

class FLogAsyncWriter : public FRunnable
{
public:
    FLogAsyncWriter(FArchive& InAr);

    virtual ~FLogAsyncWriter();

    /** Serialize data to buffer that will later be saved to disk by the async thread */
    void Serialize(uint8* Data, int32 Length);

    /** Flush all buffers to disk */
    void Flush();

    //~ Begin FRunnable Interface.
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    //~ End FRunnable Interface

private:
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
    /** Position where the unserialized data starts in the buffer */
    int32 BufferStartPos;
    /** Position where the unserialized data ends in the buffer (such as if (BufferEndPos > BufferStartPos) Length = BufferEndPos - BufferStartPos; */
    int32 BufferEndPos;
    /** Sync object for the buffer pos */
    FCriticalSection BufferPosCritical;
    /** Event to let the worker thread know there is data to save to disk. */
    FEvent* BufferDirtyEvent;
    /** Event flagged when the worker thread finished processing data */
    FEvent* BufferEmptyEvent;

    /** Serialize the contents of the ring buffer to disk */
    void SerializeBufferToArchive();

    /** Flush the memory buffer (doesn't force the archive to flush) */
    void FlushBuffer();
};
