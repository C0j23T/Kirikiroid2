#include "GlobalConfigManager.h"
#include "ConfigFiles.h"
#include "platform/CCFileUtils.h"
#include "Platform.h"
#include "LocaleConfigManager.h"

GlobalConfigManager::GlobalConfigManager() {
	Initialize();
}

GlobalConfigManager* GlobalConfigManager::GetInstance() {
	static GlobalConfigManager instance;
	return &instance;
}

void iSysConfigManager::Initialize() {
	AllConfig.clear();
	ConfigUpdated = false;
	Config::Entries entries = Config::LoadPreference(GetFilePath());
	for (const auto &item : entries.Items) AllConfig[item.first] = item.second;
	CustomArguments = std::move(entries.CustomArguments);
	KeyMap.insert(entries.KeyMap.begin(), entries.KeyMap.end());
}

void iSysConfigManager::SaveToFile() {
	if (!ConfigUpdated) return;
	std::string filepath = GetFilePath();
	if (filepath.empty()) return;
	Config::Entries entries;
	entries.Items.assign(AllConfig.begin(), AllConfig.end());
	entries.CustomArguments = CustomArguments;
	for (const auto &key : KeyMap) {
		if (key.first && key.second) entries.KeyMap.emplace_back(key.first, key.second);
	}
	if (!Config::SavePreference(filepath, entries)) {
		TVPShowSimpleMessageBox(
			LocaleConfigManager::GetInstance()->GetText("cannot_create_preference"),
			LocaleConfigManager::GetInstance()->GetText("readonly_storage"));
	}
	ConfigUpdated = false;
}

bool iSysConfigManager::IsValueExist(const std::string &name)
{
	auto it = AllConfig.find(name);
	return it != AllConfig.end();
}

std::string GlobalConfigManager::GetFilePath() {
	return TVPGetInternalPreferencePath() + "GlobalPreference.xml";
}

template<>
bool iSysConfigManager::GetValue<bool>(const std::string &name, const bool& defVal) {
	return !!GetValue<int>(name, defVal);
}

template<>
int iSysConfigManager::GetValue<int>(const std::string &name, const int& defVal) {
	auto it = AllConfig.find(name);
	if (it == AllConfig.end()) {
		SetValueInt(name, defVal);
		return defVal;
	}
	return atoi(it->second.c_str());
}

template<>
float iSysConfigManager::GetValue<float>(const std::string &name, const float& defVal) {
	auto it = AllConfig.find(name);
	if (it == AllConfig.end()) {
		SetValueFloat(name, defVal);
		return defVal;
	}
	return atof(it->second.c_str());
}

template<>
std::string iSysConfigManager::GetValue<std::string>(const std::string &name, const std::string& defVal) {
	auto it = AllConfig.find(name);
	if (it == AllConfig.end()) {
		SetValue(name, defVal);
		return defVal;
	}
	return it->second;
}

void iSysConfigManager::SetValueInt(const std::string &name, int val) {
	char buf[16];
	sprintf(buf, "%d", val);
	AllConfig[name] = buf;
	ConfigUpdated = true;
}

void iSysConfigManager::SetValueFloat(const std::string &name, float val) {
	char buf[24];
	sprintf(buf, "%g", val);
	AllConfig[name] = buf;
	ConfigUpdated = true;
}

void iSysConfigManager::SetValue(const std::string &name, const std::string & val) {
	AllConfig[name] = val;
	ConfigUpdated = true;
}

void iSysConfigManager::SetKeyMap(int k/* 0 means remove */, int v)
{
	if (v == 0) {
		KeyMap.erase(k);
	} else {
		KeyMap[k] = v;
	}
}

std::vector<std::string> iSysConfigManager::GetCustomArgumentsForPush() {
	std::vector<std::string> ret;
	for (auto arg : CustomArguments) {
		std::string line("-");
		line += arg.first;
		line += "=";
		line += arg.second;
		ret.emplace_back(line);
	}
	return ret;
}

