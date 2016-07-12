# LogManager
A plugin to manage logs for Unrealengine4

### 虚幻4默认日志管理的问题 ###

* 虚幻4默认的日志都是输出到同一个文件中，如果你的项目比较大(一般游戏项目都不小)，功能模块比较多，这样调试起来就非常不方便，虽然一些文本编辑器也有关键字过滤的功能，但是如果能直接输出到不同的文件，还是要更加方便
* 当多次启动后，虚幻会将之前的log文件按照本次启动的时间重命名一下进行备份，这样就带来一个问题，如果需要用户提交log文件的话，很可能会拿错文件

### 日志插件功能 ###

* 每次启动生成一个以当前时间为名称的目录，本次运行过程中产生的所有log都生成在这个目录下
* 增加一个 `filter` 功能，把特定分类的log输出到单独的日志文件中

### 使用 ###

* 将插件代码复制到项目的 `Plugins` 目录下，并在编辑器中刷新 `Visual Studio` 工程，如图：
	！[](http://7xqxmb.com1.z0.glb.clouddn.com/blog/images/copy_src.png)

* 在项目的编译规则中，加入插件依赖，如图：
	！[](http://7xqxmb.com1.z0.glb.clouddn.com/blog/images/add_reference.png)

* 增加一个日志分类
	``` cpp
	// in .h
	DECLARE_LOG_CATEGORY_EXTERN(LogPluginTest, Log, All);

	// in .cpp
	DEFINE_LOG_CATEGORY(LogPluginTest)
	```

* 在项目的初始化中增加日志插件的初始化
	``` cpp
	#include "ILogManager.h"

	if (ILogManager::Get().IsAvailable())
    {
		// 保留最近的5个日志文件夹
        ILogManager::Get().CleanLogFolder(5);

		// 将LogPluginTest分类的log输出到单独的文件夹中
        ILogManager::Get().AddFilter(LogPluginTest.GetCategoryName().ToString(), true);
    }
	```

* 输出log
	``` cpp
	UE_LOG(LogPluginTest, Display, TEXT("Test log 1"));
	```

* 最终的效果，如图
	！[](http://7xqxmb.com1.z0.glb.clouddn.com/blog/images/folder_list.png)

	！[](http://7xqxmb.com1.z0.glb.clouddn.com/blog/images/log_detail.png)

* 如果想要使用虚幻默认的日志行为，则只需要禁用插件即可，不需要修改代码