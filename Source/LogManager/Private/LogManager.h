// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#pragma once

#include "LogManagerPrivatePCH.h"

class FLogManager : public FOutputDevice, public ILogManager
{
public:
    /**
     * Constructor, initializing member variables.
     */
    FLogManager();

    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /**
     * @brief Adds a log filter to the list of filters.
     * @param Category - category name
     * @param ForceLogFlush - flush log to file immediately 
     */
    virtual void AddFilter(const FString& Category, ELogVerbosity::Type FlushOn) override;

	/**
	 * @brief Change a log category's flush-on log level.
	 * @param Category - category name
	 * @param FlushOn - flush log to file immediately when log level >= FlushOn
	 */
	virtual void ChangeLogFlushOnLevel(const FString& Category, ELogVerbosity::Type FlushOn) override;

    /**
     * @brief Gets current absolute log directory.
     */
    virtual const FString& GetCurrentLogDir() const override;

    /**
     * @brief Remains the number of log folders to LogFolderCount.
     */
    virtual void RemainsLogCount(int32 LogFolderCount) override;

    /**
     * @brief Removes a log filter from the list of filters.
     * @param Category - category name
     */
    void RemoveFilter(const FString& Category);

    /**
     * @brief Enable a log filter in the list of filters.
     * @param Category - category name
     */
    void EnableFilter(const FString& Category);

    /**
     * @brief Disable a log filter in the list of filters.
     * @param Category - category name
     */
    void DisableFilter(const FString& Category);

    //~ Begin FOutputDevice Interface.
    /**
    * Closes output device and cleans up. This can't happen in the destructor
    * as we have to call "delete" which cannot be done for static/ global
    * objects.
    */
    virtual void TearDown() override;

    /**
    * Flush the write cache so the file isn't truncated in case we crash right
    * after calling this function.
    */
    virtual void Flush() override;

    virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity,
        const class FName& Category, const double Time) override;
    
    virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category) override;
    
    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

protected:
    void WriteByteOrderMarkToArchive(FLogAsyncWriter* AsyncWriter, EByteOrderMark ByteOrderMark);

    void CastAndSerializeData(FLogAsyncWriter* AsyncWriter, const TCHAR* Data);

    static FString FormatLogLine(ELogVerbosity::Type Verbosity, const class FName& Category, const TCHAR* Message = nullptr, ELogTimes::Type LogTime = ELogTimes::None, const double Time = -1.0);

    void WriteDataToArchive(FLogAsyncWriter* AsyncWriter, const TCHAR* Data,
        ELogVerbosity::Type Verbosity, const double Time, const class FName& Category = TEXT(""));

    FLogAsyncWriter* CreateAsyncWriter(const FString& Filename);

private:
    struct FLogFilter
    {
        FString Category;
        FLogAsyncWriter* AsyncWriter;
		ELogVerbosity::Type FlushOn;

        friend bool operator==(const FLogFilter& Lhs, const FLogFilter& Rhs)
        {
            return Lhs.Category.Equals(Rhs.Category, ESearchCase::IgnoreCase);
        }
    };

    FString CurrentLogDir;
    FString DefaultLogFilename;
    TArray<FLogFilter> LogFilters;
};
