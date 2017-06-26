// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#include "LogManagerPrivatePCH.h"

#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/ExceptionHandling.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDeviceRedirector.h"

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

    CurrentLogDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(
        *FString::Printf(TEXT("%s%s %s"), *GameLogDir, *GameName, *SystemTime));

    // Adds default filter
    DefaultLogFilename =
        FString::Printf(TEXT("%s/%s%s"), *CurrentLogDir,
            bHasLogFileName ? LogFilename : *GameName,
            bHasLogFileName ? TEXT("") : TEXT(".log"));

    FLogFilter DefaultFiter;
    DefaultFiter.AsyncWriter = CreateAsyncWriter(DefaultLogFilename);
	DefaultFiter.FlushOn =
		FParse::Param(FCommandLine::Get(), TEXT("FORCELOGFLUSH")) ? ELogVerbosity::All : ELogVerbosity::Warning;
    LogFilters.AddUnique(DefaultFiter);

    if (!GUseCrashReportClient)
    {
        FCString::Strcpy(MiniDumpFilenameW,
            *FString::Printf(TEXT("%s/%s"), *CurrentLogDir, *FPaths::GetCleanFilename(MiniDumpFilenameW)));
    }
}

void FLogManager::StartupModule()
{
    // This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)
    if (GLog)
    {
        if (GLog)
        {
            FOutputDevice* OutputLog = FPlatformOutputDevices::GetLog();
            GLog->RemoveOutputDevice(OutputLog);

            if (OutputLog)
            {
                OutputLog->TearDown();
            }

            IFileManager::Get().Delete(*FPlatformOutputDevices::GetAbsoluteLogFilename(), false, true, true);
        }

        GLog->AddOutputDevice(this);
        GLog->SerializeBacklog(this);
    }
}


void FLogManager::ShutdownModule()
{
    if (GLog)
    {
        GLog->RemoveOutputDevice(this);
    }

    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
    TearDown();
    
    FOutputDeviceFile* OutputLogFile = static_cast<FOutputDeviceFile*>(FPlatformOutputDevices::GetLog());
    if (OutputLogFile)
    {
        OutputLogFile->SetFilename(*DefaultLogFilename);
        GLog->AddOutputDevice(OutputLogFile);
    }
}

void FLogManager::AddFilter(const FString& Category, ELogVerbosity::Type FlushOn)
{
    if (!Category.IsEmpty())
    {
        FLogFilter LogFilter{ Category, nullptr, FlushOn };

        if (INDEX_NONE == LogFilters.Find(LogFilter))
        {
            const FString Filename = FString::Printf(TEXT("%s/%s.log"), *CurrentLogDir, *Category);
            
            LogFilter.AsyncWriter = CreateAsyncWriter(Filename);
            LogFilters.AddUnique(LogFilter);
        }
    }
}

void FLogManager::ChangeLogFlushOnLevel(const FString& Category, ELogVerbosity::Type FlushOn)
{
	int32 FoundIndex = INDEX_NONE;
	FLogFilter LogFilter{ Category, nullptr, ELogVerbosity::All };

	if (LogFilters.Find(LogFilter, FoundIndex))
	{
		LogFilters[FoundIndex].FlushOn = FlushOn;
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

void FLogManager::RemainsLogCount(int32 LogFolderCount)
{
    // Delete old log directory
    if (LogFolderCount >= 0)
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

        if (LogVisitor.LogFolders.Num() >= LogFolderCount)
        {
            const int32 ExpiredFolderCount = LogVisitor.LogFolders.Num() - LogFolderCount;
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
            FLogFilter LogFilter{ Category.ToString(), nullptr, ELogVerbosity::All };
            int32 FoundIndex = INDEX_NONE;

            FLogAsyncWriter* AsyncWriter = nullptr;
			ELogVerbosity::Type FlushOn = ELogVerbosity::Warning;
            bool bUseCategory = false;

            if (LogFilters.Find(LogFilter, FoundIndex))
            {
                AsyncWriter = LogFilters[FoundIndex].AsyncWriter;
				FlushOn = LogFilters[FoundIndex].FlushOn;
            }
            else if (LogFilters.Num() > 0)
            {
                AsyncWriter = LogFilters[0].AsyncWriter;
				FlushOn = LogFilters[0].FlushOn;
                bUseCategory = true;
            }

            if (AsyncWriter)
            {
                WriteDataToArchive(AsyncWriter, Data, Verbosity, Time, bUseCategory ? Category : NAME_None);

                if (Verbosity <= FlushOn)
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

FString FLogManager::FormatLogLine(ELogVerbosity::Type Verbosity, const class FName& Category, const TCHAR* Message /*= nullptr*/, ELogTimes::Type LogTime /*= ELogTimes::None*/, const double Time /*= -1.0*/)
{
    const bool bShowCategory = GPrintLogCategory && Category != NAME_None;

    FString Format = FString::Printf(TEXT("[%s][%3d]"), *FDateTime::Now().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")), GFrameCounter % 1000);

    if (bShowCategory)
    {
        if (Verbosity != ELogVerbosity::Log)
        {
            Format += Category.ToString();
            Format += TEXT(":");
            Format += FOutputDeviceHelper::VerbosityToString(Verbosity);
            Format += TEXT(": ");
        }
        else
        {
            Format += Category.ToString();
            Format += TEXT(": ");
        }
    }
    else
    {
        if (Verbosity != ELogVerbosity::Log)
        {
#if !HACK_HEADER_GENERATOR
            Format += FOutputDeviceHelper::VerbosityToString(Verbosity);
            Format += TEXT(": ");
#endif
        }
    }

    if (Message)
    {
        Format += Message;
    }

    return Format;
}

void FLogManager::WriteDataToArchive(FLogAsyncWriter* AsyncWriter, const TCHAR* Data,
    ELogVerbosity::Type Verbosity, const double Time, const class FName& Category)
{
#if PLATFORM_LINUX
    const FString Message = FString::Printf(TEXT("%s%s"), Data, bAutoEmitLineTerminator ? TEXT("\r\n") : TEXT(""));
#else
    const FString Message = FString::Printf(TEXT("%s%s"), Data, bAutoEmitLineTerminator ? LINE_TERMINATOR : TEXT(""));
#endif // PLATFORM_LINUX

    const FString LogLine = FormatLogLine(Verbosity, Category, *Message, GPrintLogTimes, Time);
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
