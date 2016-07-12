// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#include "LogManagerPrivatePCH.h"

typedef uint8 UTF8BOMType[3];
static UTF8BOMType UTF8BOM = { 0xEF, 0xBB, 0xBF };

IMPLEMENT_MODULE(FLogManager, LogManager)

FLogManager::FLogManager()
{
    TCHAR LogFilename[128] = { 0 };
    TCHAR AbsoluteLogFilename[1024] = { 0 };

    FString GameLogDir = FPaths::GameLogDir();
    const FString GameName = FCString::Strlen(FApp::GetGameName()) != 0 ? FApp::GetGameName() : TEXT("UE4");
    const FString SystemTime = FDateTime::Now().ToString(TEXT("%Y.%m.%d-%H.%M.%S.%s"));

    const bool bHasLogFileName = FParse::Value(FCommandLine::Get(), TEXT("LOG="), LogFilename, ARRAY_COUNT(LogFilename));
    if (!bHasLogFileName &&
        FParse::Value(FCommandLine::Get(), TEXT("ABSLOG="), AbsoluteLogFilename, ARRAY_COUNT(AbsoluteLogFilename)))
    {
        GameLogDir = FPaths::GetPath(AbsoluteLogFilename).AppendChar(TEXT('/'));
    }

    CurrentLogDir = FString::Printf(TEXT("%s%s %s"), *GameLogDir, *GameName, *SystemTime);

    // Adds default filter
    const FString DefaultLogFilename =
        FString::Printf(TEXT("%s/%s%s"), *CurrentLogDir,
            bHasLogFileName ? LogFilename : *GameName,
            bHasLogFileName ? TEXT("") : TEXT(".log"));

    DefaultFiter.AsyncWriter = CreateAsyncWriter(DefaultLogFilename);
    DefaultFiter.ForceLogFlush = FParse::Param(FCommandLine::Get(), TEXT("FORCELOGFLUSH"));;
    LogFilters.AddUnique(DefaultFiter);
}

void FLogManager::StartupModule()
{
    // This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)
    if (GLog)
    {
        if (GLog)
        {
            GLog->RemoveOutputDevice(FPlatformOutputDevices::GetLog());
        }

        GLog->AddOutputDevice(this);
    }
}


void FLogManager::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
    TearDown();

    if (GLog)
    {
        GLog->RemoveOutputDevice(this);
    }
}

void FLogManager::AddFilter(const FString& Category, bool ForceLogFlush)
{
    if (!Category.IsEmpty())
    {
        FLogFilter LogFilter{ Category, nullptr, ForceLogFlush };

        if (INDEX_NONE == LogFilters.Find(LogFilter))
        {
            const FString Filename = FString::Printf(TEXT("%s/Filter%s.log"), *CurrentLogDir, *Category);
            
            LogFilter.AsyncWriter = CreateAsyncWriter(Filename);
            LogFilters.AddUnique(LogFilter);
        }
    }
}

void FLogManager::RemoveFilter(const FString& Category)
{

}

void FLogManager::EnableFilter(const FString& Category)
{

}

void FLogManager::DisableFilter(const FString& Category)
{

}

const FString& FLogManager::GetCurrentLogDir() const
{
    return CurrentLogDir;
}

void FLogManager::CleanLogFolder(int32 LogFolderNumber)
{
    // Delete old log directory
    if (LogFolderNumber >= 0)
    {
        const FString GameLogDir = FPaths::GetPath(CurrentLogDir).AppendChar(TEXT('/'));
        const FString GameName = FCString::Strlen(FApp::GetGameName()) != 0 ? FApp::GetGameName() : TEXT("UE4");

        class FLogDirectoryVisitor : public IPlatformFile::FDirectoryVisitor
        {
        public:
            FLogDirectoryVisitor(const FString& FolderPrefix)
                : LogFolderPrefix(FolderPrefix)
            {

            }

            //~ Begin FDirectoryVisitor Interface.
            virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
            {
                if (bIsDirectory)
                {
                    const FString FolderName = FPaths::GetCleanFilename(FilenameOrDirectory);
                    if (FolderName.StartsWith(LogFolderPrefix, ESearchCase::IgnoreCase))
                    {
                        LogFolders.Add(FolderName);
                    }
                }

                return true;
            }

            TArray<FString> LogFolders;
            FString LogFolderPrefix;
        } LogVisitor(GameName);

        IFileManager::Get().IterateDirectory(*GameLogDir, LogVisitor);

        if (LogVisitor.LogFolders.Num() >= LogFolderNumber)
        {
            const int32 ExpiredFolderCount = LogVisitor.LogFolders.Num() - LogFolderNumber;
            LogVisitor.LogFolders.Sort();

            for (int32 i = 0; i < ExpiredFolderCount; ++i)
            {
                const FString ExpiredLogPath = GameLogDir + LogVisitor.LogFolders[i];

                if (!CurrentLogDir.Equals(ExpiredLogPath, ESearchCase::IgnoreCase))
                {
                    IFileManager::Get().DeleteDirectory(*ExpiredLogPath, true, true);
                }
            }
        }
    }
}

void FLogManager::TearDown()
{
    for (auto LogFilter : LogFilters)
    {
        if (LogFilter.AsyncWriter)
        {
            WriteDataToArchive(
                LogFilter.AsyncWriter,
                *FString::Printf(TEXT("Log file closed, %s"), FPlatformTime::StrTimestamp()),
                ELogVerbosity::Display,
                -1.0f);

            LogFilter.AsyncWriter->Flush();

            delete LogFilter.AsyncWriter;
            LogFilter.AsyncWriter = nullptr;
        }
    }

    LogFilters.Empty();

    DefaultFiter.AsyncWriter = nullptr;
}

void FLogManager::Flush()
{
    for (auto LogFilter : LogFilters)
    {
        if (LogFilter.AsyncWriter)
        {
            LogFilter.AsyncWriter->Flush();
        }
    }
}

void FLogManager::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity,
    const class FName& Category, const double Time)
{
    static bool Entry = false;
    if (!GIsCriticalError || Entry)
    {
        if (Verbosity != ELogVerbosity::SetColor)
        {
            FLogFilter LogFilter{ Category.ToString(), nullptr, false };
            int32 FoundIndex = INDEX_NONE;

            FLogAsyncWriter* AsyncWriter = nullptr;
            bool ForceLogFlush = false;
            bool bUseCategory = false;

            if (LogFilters.Find(LogFilter, FoundIndex))
            {
                AsyncWriter = LogFilters[FoundIndex].AsyncWriter;
                ForceLogFlush = LogFilters[FoundIndex].ForceLogFlush;
            }
            else
            {
                AsyncWriter = DefaultFiter.AsyncWriter;
                ForceLogFlush = DefaultFiter.ForceLogFlush;
                bUseCategory = true;
            }

            if (AsyncWriter)
            {
                WriteDataToArchive(AsyncWriter, Data, Verbosity, Time, bUseCategory ? Category : NAME_None);

                if (ForceLogFlush)
                {
                    AsyncWriter->Flush();
                }
            }
        }
    }
    else
    {
        Entry = true;
        Serialize(Data, Verbosity, Category, Time);
        Entry = false;
    }
}

void FLogManager::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category)
{
    Serialize(Data, Verbosity, Category, -1.0);
}

void FLogManager::WriteByteOrderMarkToArchive(FLogAsyncWriter* AsyncWriter, EByteOrderMark ByteOrderMark)
{
    switch (ByteOrderMark)
    {
    case EByteOrderMark::UTF8:
        if (AsyncWriter)
        {
            AsyncWriter->Serialize(UTF8BOM, sizeof(UTF8BOM));
        }        
        break;

    case EByteOrderMark::Unspecified:
    default:
        break;
    }
}

void FLogManager::CastAndSerializeData(FLogAsyncWriter* AsyncWriter, const TCHAR* Data)
{
    if (AsyncWriter)
    {
        FTCHARToUTF8 ConvertedData(Data);
        AsyncWriter->Serialize((uint8*)(ConvertedData.Get()), ConvertedData.Length() * sizeof(ANSICHAR));
    }
}

void FLogManager::WriteDataToArchive(FLogAsyncWriter* AsyncWriter, const TCHAR* Data,
    ELogVerbosity::Type Verbosity, const double Time, const class FName& Category)
{
#if PLATFORM_LINUX
    const FString Message = FString::Printf(TEXT("%s%s"), Data, bAutoEmitLineTerminator ? TEXT("\r\n") : TEXT(""));
#else
    const FString Message = FString::Printf(TEXT("%s%s"), Data, bAutoEmitLineTerminator ? LINE_TERMINATOR : TEXT(""));
#endif // PLATFORM_LINUX

    const FString LogLine = FOutputDevice::FormatLogLine(Verbosity, Category, *Message, GPrintLogTimes, Time);
    CastAndSerializeData(AsyncWriter, *LogLine);
}

FLogAsyncWriter* FLogManager::CreateAsyncWriter(const FString& Filename)
{
    // Open log file.
    FArchive* Ar = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_AllowRead);
    FLogAsyncWriter* AsyncWriter = nullptr;

    if (Ar)
    {
        AsyncWriter = new FLogAsyncWriter(*Ar);

        if (AsyncWriter)
        {
            WriteByteOrderMarkToArchive(AsyncWriter, EByteOrderMark::UTF8);
            WriteDataToArchive(
                AsyncWriter,
                *FString::Printf(TEXT("Log file open, %s"), FPlatformTime::StrTimestamp()),
                ELogVerbosity::Display,
                -1.0f);
        }
    }

    return AsyncWriter;
}
