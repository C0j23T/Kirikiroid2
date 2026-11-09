// The preference and locale files the config managers read and write.
//
// `krkr_shim` owns the format, the file I/O and the walk over the records
// (crates/krkr_shim/src/xml.rs); the engine only says what to do with one.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "krkr_shim.h"

namespace Config {

/// A preference file's contents, in file order.
struct Entries {
	std::vector<std::pair<std::string, std::string>> Items;
	std::vector<std::pair<std::string, std::string>> CustomArguments;
	/// Key code before and after the remap.
	std::vector<std::pair<int, int>> KeyMap;
};

namespace detail {

extern "C" inline void CollectEntry(void *userdata, int list, const char *key,
	const char *value) {
	Entries *entries = static_cast<Entries *>(userdata);
	if (list == KRKR_PREFERENCE_ITEM) {
		entries->Items.emplace_back(key, value);
	} else if (list == KRKR_PREFERENCE_CUSTOM) {
		entries->CustomArguments.emplace_back(key, value);
	}
}

extern "C" inline void CollectKeyMap(void *userdata, int key, int value) {
	static_cast<Entries *>(userdata)->KeyMap.emplace_back(key, value);
}

extern "C" inline void CollectText(void *userdata, const char *id, const char *text) {
	auto *texts = static_cast<std::unordered_map<std::string, std::string> *>(userdata);
	(*texts)[id] = text;
}

} // namespace detail

/// A missing, unreadable or malformed file leaves `entries` empty.
inline Entries LoadPreference(const std::string &path) {
	Entries entries;
	KrkrPreferenceVisitor visitor;
	visitor.userdata = &entries;
	visitor.entry = detail::CollectEntry;
	visitor.keymap = detail::CollectKeyMap;
	krkr_preference_read(path.c_str(), &visitor);
	return entries;
}

/// False when the file cannot be written, e.g. into a read-only directory.
inline bool SavePreference(const std::string &path, const Entries &entries) {
	std::vector<KrkrPreferenceRecord> records;
	records.reserve(entries.Items.size() + entries.CustomArguments.size() + entries.KeyMap.size());
	for (const auto &item : entries.Items) {
		records.push_back(KrkrPreferenceRecord{
			KRKR_PREFERENCE_ITEM, 0, 0, item.first.c_str(), item.second.c_str()});
	}
	for (const auto &argument : entries.CustomArguments) {
		records.push_back(KrkrPreferenceRecord{
			KRKR_PREFERENCE_CUSTOM, 0, 0, argument.first.c_str(), argument.second.c_str()});
	}
	for (const auto &key : entries.KeyMap) {
		records.push_back(KrkrPreferenceRecord{
			KRKR_PREFERENCE_KEYMAP, key.first, key.second, nullptr, nullptr});
	}
	return krkr_preference_write(path.c_str(), records.data(), records.size()) == 0;
}

/// The file can live inside an archive, so the caller reads it and passes the
/// contents.
inline std::unordered_map<std::string, std::string> LoadLocale(const std::string &xml) {
	std::unordered_map<std::string, std::string> texts;
	KrkrTextVisitor visitor;
	visitor.userdata = &texts;
	visitor.text = detail::CollectText;
	krkr_locale_read(xml.data(), xml.size(), &visitor);
	return texts;
}

} // namespace Config
