// Copyright 2016 wang jie(newzeadev@gmail.com). All Rights Reserved.

#pragma once

#include "ModuleManager.h"


/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class ILogManager : public IModuleInterface
{

public:

    /**
     * Singleton-like access to this module's interface.  This is just for convenience!
     * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
     *
     * @return Returns singleton instance, loading the module on demand if needed
     */
    static inline ILogManager& Get()
    {
        return FModuleManager::LoadModuleChecked<ILogManager>("LogManager");
    }

    /**
     * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
     *
     * @return True if the module is loaded and ready to use
     */
    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded("LogManager");
    }

    /**
     * @brief Adds a log filter to the list of filters.
     * @param Category - category name
     * @param FlushOn - flush log to file immediately when log level >= FlushOn
     */
    virtual void AddFilter(const FString& Category, ELogVerbosity::Type FlushOn) = 0;

	/**
	 * @brief Change a log category's flush-on log level.
	 * @param Category - category name
	 * @param FlushOn - flush log to file immediately when log level >= FlushOn
	 */
	virtual void ChangeLogFlushOnLevel(const FString& Category, ELogVerbosity::Type FlushOn) = 0;

    /**
     * @brief Gets current absolute log directory.
     */
    virtual const FString& GetCurrentLogDir() const = 0;

    /**
     * @brief Remains the number of log folders to LogFolderCount.
     */
    virtual void RemainsLogCount(int32 LogFolderCount) = 0;
};

