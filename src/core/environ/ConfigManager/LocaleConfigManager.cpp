#include "LocaleConfigManager.h"
#include "ConfigFiles.h"
#include "CCFileUtils.h"
#include "GlobalConfigManager.h"
#ifndef KRKR_BEVY_PORT // bevy port: no cocos2d UI widgets
#include "ui/UIText.h"
#include "ui/UIButton.h"
#endif

LocaleConfigManager::LocaleConfigManager() {

}

std::string LocaleConfigManager::GetFilePath() {
	std::string pathprefix = "locale/"; // constant file in app package
	std::string fullpath = pathprefix + currentLangCode + ".xml"; // exp. "local/en_us.xml"
	if (!cocos2d::FileUtils::getInstance()->isFileExist(fullpath)) {
#ifdef KRKR_BEVY_PORT
		// bevy port: the locale files are optional (texts fall back to their
		// ids), so do not recurse forever when en_us.xml is missing too.
		if (currentLangCode == "en_us") return std::string();
#endif
		currentLangCode = "en_us"; // restore to default language config(must exist)
		return GetFilePath();
	}
	return cocos2d::FileUtils::getInstance()->fullPathForFilename(fullpath);
}

LocaleConfigManager* LocaleConfigManager::GetInstance() {
	static LocaleConfigManager instance;
	return &instance;
}

const std::string &LocaleConfigManager::GetText(const std::string &tid) {
	auto it = AllConfig.find(tid);
	if (it == AllConfig.end()) {
		AllConfig[tid] = tid;
		return AllConfig[tid];
	}
	return it->second;
}

void LocaleConfigManager::Initialize(const std::string &sysLang) {
	// override by global configured lang
	currentLangCode = GlobalConfigManager::GetInstance()->GetValue<std::string>("user_language", "");
	if (currentLangCode.empty()) currentLangCode = sysLang;
	std::string xmlData = cocos2d::FileUtils::getInstance()->getStringFromFile(GetFilePath());
	AllConfig = Config::LoadLocale(xmlData);
}

#ifndef KRKR_BEVY_PORT // bevy port: no cocos2d UI widgets
bool LocaleConfigManager::initText(cocos2d::ui::Text *ctrl) {
	if (!ctrl) return false;
	return initText(ctrl, ctrl->getString());
}

bool LocaleConfigManager::initText(cocos2d::ui::Button *ctrl)
{
	if (!ctrl) return false;
	return initText(ctrl, ctrl->getTitleText());
}

bool LocaleConfigManager::initText(cocos2d::ui::Text *ctrl, const std::string &tid) {
	if (!ctrl) return false;

	std::string txt = GetText(tid);
	if (txt.empty()) {
		ctrl->setString(tid);
		ctrl->setColor(cocos2d::Color3B::RED);
		return false;
	}

	ctrl->setString(txt);
	return true;
}

bool LocaleConfigManager::initText(cocos2d::ui::Button *ctrl, const std::string &tid) {
	if (!ctrl) return false;

	std::string txt = GetText(tid);
	if (txt.empty()) {
		ctrl->setTitleText(tid);
		ctrl->setTitleColor(cocos2d::Color3B::RED);
		return false;
	}

	ctrl->setTitleText(txt);
	return true;
}
#endif // KRKR_BEVY_PORT
