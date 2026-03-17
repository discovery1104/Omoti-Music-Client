#pragma once

struct MusicHelperStatus {
	bool ok = false;
	bool helperAvailable = false;
	std::wstring state = L"stopped";
	std::wstring path = {};
	std::wstring error = {};
	int positionMs = 0;
	int durationMs = -1;
	float volume = 1.f;
};

class MusicHelperClient {
public:
	static MusicHelperClient& get();

	MusicHelperStatus getStatus(bool force = false);
	bool play(std::filesystem::path const& trackPath, float volume);
	bool pause();
	bool resume();
	bool stop();
	bool shutdown();
	bool seek(int targetMs);
	bool setVolume(float volume);

	std::wstring getLastError() const;
	std::filesystem::path resolveHelperPath() const;

private:
	MusicHelperStatus requestStatus();
	std::optional<json> sendRequest(json const& request, bool allowLaunch = true);
	bool ensureHelperStarted();
	bool ensureEmbeddedHelperExtracted(std::filesystem::path const& helperPath);
	void updateStatusFromJson(MusicHelperStatus& status, json const& response) const;

	ULONGLONG lastStatusTick = 0;
	MusicHelperStatus cachedStatus = {};
	std::wstring lastError = {};
	mutable std::mutex clientMutex;
};
