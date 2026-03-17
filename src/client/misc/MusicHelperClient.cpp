#include "pch.h"
#include "MusicHelperClient.h"

#include "client/Omoti.h"
#include "client/resource/InitResources.h"

namespace {
	constexpr wchar_t kPipeName[] = LR"(\\.\pipe\OmotiMusicHelperPipe_v1)";
	constexpr wchar_t kHelperExeName[] = L"OmotiMusicHelper.exe";
	constexpr wchar_t kHelperVersionFileName[] = L"embedded_version.txt";

	struct EmbeddedHelperFile {
		const wchar_t* fileName;
		Resource resource;
	};

	auto const& getEmbeddedHelperFiles() {
		static const std::array<EmbeddedHelperFile, 6> files = {{
			{ L"OmotiMusicHelper.exe", GET_RESOURCE(helper_OmotiMusicHelper_exe) },
			{ L"D3DCompiler_47_cor3.dll", GET_RESOURCE(helper_D3DCompiler_47_cor3_dll) },
			{ L"PenImc_cor3.dll", GET_RESOURCE(helper_PenImc_cor3_dll) },
			{ L"PresentationNative_cor3.dll", GET_RESOURCE(helper_PresentationNative_cor3_dll) },
			{ L"vcruntime140_cor3.dll", GET_RESOURCE(helper_vcruntime140_cor3_dll) },
			{ L"wpfgfx_cor3.dll", GET_RESOURCE(helper_wpfgfx_cor3_dll) }
		}};

		return files;
	}

	std::filesystem::path getFallbackRuntimeBasePath() {
		wchar_t dllPath[MAX_PATH]{};
		auto len = GetModuleFileNameW(Omoti::get().dllInst, dllPath, MAX_PATH);
		if (len > 0 && len < MAX_PATH) {
			return std::filesystem::path(dllPath).parent_path() / L"OmotiRuntime";
		}

		return std::filesystem::temp_directory_path() / L"OmotiRuntime";
	}

	std::wstring getEmbeddedHelperVersion() {
		return util::StrToWStr(std::string(Omoti::version.data(), Omoti::version.size()));
	}

	bool writeResourceToFile(std::filesystem::path const& path, Resource const& resource, std::wstring& error) {
		auto tempPath = path;
		tempPath += L".tmp";

		{
			std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				error = std::format(L"Could not open {} for writing", tempPath.wstring());
				return false;
			}

			out.write(resource.data(), static_cast<std::streamsize>(resource.size()));
			if (!out.good()) {
				error = std::format(L"Could not write embedded helper file {}", path.filename().wstring());
				return false;
			}
		}

		if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
			error = std::format(L"Could not finalize embedded helper file {} ({})", path.filename().wstring(), GetLastError());
			DeleteFileW(tempPath.c_str());
			return false;
		}

		return true;
	}

	bool readTextFile(std::filesystem::path const& path, std::string& outText) {
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open()) return false;

		outText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		return true;
	}

	std::wstring readPipeLine(HANDLE pipe) {
		std::string response;
		char buffer[512]{};
		DWORD bytesRead = 0;
		while (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
			response.append(buffer, buffer + bytesRead);
			if (response.find('\n') != std::string::npos) break;
		}

		if (auto newline = response.find('\n'); newline != std::string::npos) {
			response.resize(newline);
		}

		return util::StrToWStr(response);
	}

	bool writePipeLine(HANDLE pipe, std::string const& line) {
		DWORD bytesWritten = 0;
		return WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr) && bytesWritten == line.size();
	}
}

MusicHelperClient& MusicHelperClient::get() {
	static MusicHelperClient instance;
	return instance;
}

MusicHelperStatus MusicHelperClient::getStatus(bool force) {
	std::scoped_lock lock(clientMutex);
	auto now = GetTickCount64();
	if (!force && cachedStatus.helperAvailable && (now - lastStatusTick) < 150) {
		return cachedStatus;
	}

	cachedStatus = requestStatus();
	lastStatusTick = now;
	return cachedStatus;
}

bool MusicHelperClient::play(std::filesystem::path const& trackPath, float volume) {
	std::scoped_lock lock(clientMutex);
	json request = {
		{"command", "play"},
		{"path", util::WStrToStr(trackPath.wstring())},
		{"volume", std::clamp(volume, 0.f, 1.f)}
	};

	auto response = sendRequest(request);
	if (!response.has_value()) return false;

	cachedStatus = {};
	updateStatusFromJson(cachedStatus, *response);
	lastStatusTick = GetTickCount64();
	return cachedStatus.ok;
}

bool MusicHelperClient::pause() {
	std::scoped_lock lock(clientMutex);
	auto response = sendRequest({ {"command", "pause"} });
	if (!response.has_value()) return false;
	cachedStatus = {};
	updateStatusFromJson(cachedStatus, *response);
	lastStatusTick = GetTickCount64();
	return cachedStatus.ok;
}

bool MusicHelperClient::resume() {
	std::scoped_lock lock(clientMutex);
	auto response = sendRequest({ {"command", "resume"} });
	if (!response.has_value()) return false;
	cachedStatus = {};
	updateStatusFromJson(cachedStatus, *response);
	lastStatusTick = GetTickCount64();
	return cachedStatus.ok;
}

bool MusicHelperClient::stop() {
	std::scoped_lock lock(clientMutex);
	auto response = sendRequest({ {"command", "stop"} }, false);
	if (!response.has_value()) return false;
	cachedStatus = {};
	updateStatusFromJson(cachedStatus, *response);
	lastStatusTick = GetTickCount64();
	return cachedStatus.ok;
}

bool MusicHelperClient::shutdown() {
	std::scoped_lock lock(clientMutex);
	auto response = sendRequest({ {"command", "shutdown"} }, false);
	cachedStatus = {};
	lastStatusTick = 0;
	if (!response.has_value()) return false;
	return response->value("ok", false);
}

bool MusicHelperClient::seek(int targetMs) {
	std::scoped_lock lock(clientMutex);
	auto response = sendRequest({ {"command", "seek"}, {"ms", std::max(0, targetMs)} }, false);
	if (!response.has_value()) return false;
	cachedStatus = {};
	updateStatusFromJson(cachedStatus, *response);
	lastStatusTick = GetTickCount64();
	return cachedStatus.ok;
}

bool MusicHelperClient::setVolume(float volume) {
	std::scoped_lock lock(clientMutex);
	auto response = sendRequest({ {"command", "volume"}, {"volume", std::clamp(volume, 0.f, 1.f)} }, false);
	if (!response.has_value()) return false;
	cachedStatus = {};
	updateStatusFromJson(cachedStatus, *response);
	lastStatusTick = GetTickCount64();
	return cachedStatus.ok;
}

std::wstring MusicHelperClient::getLastError() const {
	std::scoped_lock lock(clientMutex);
	return lastError;
}

MusicHelperStatus MusicHelperClient::requestStatus() {
	MusicHelperStatus status;
	auto response = sendRequest({ {"command", "status"} }, false);
	if (!response.has_value()) {
		status.error = lastError;
		return status;
	}

	updateStatusFromJson(status, *response);
	return status;
}

std::optional<json> MusicHelperClient::sendRequest(json const& request, bool allowLaunch) {
	json requestPayload = request;
	requestPayload["ownerPid"] = static_cast<int>(GetCurrentProcessId());

	auto openPipe = [&]() -> HANDLE {
		if (!WaitNamedPipeW(kPipeName, 150)) {
			return INVALID_HANDLE_VALUE;
		}

		return CreateFileW(
			kPipeName,
			GENERIC_READ | GENERIC_WRITE,
			0,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr
		);
	};

	HANDLE pipe = openPipe();
	if (pipe == INVALID_HANDLE_VALUE && allowLaunch && ensureHelperStarted()) {
		pipe = openPipe();
	}
	if (pipe == INVALID_HANDLE_VALUE) {
		lastError = L"Music helper pipe is unavailable";
		return std::nullopt;
	}

	std::string payload = requestPayload.dump();
	payload.push_back('\n');

	if (!writePipeLine(pipe, payload)) {
		lastError = L"Could not write to music helper pipe";
		CloseHandle(pipe);
		return std::nullopt;
	}

	FlushFileBuffers(pipe);
	std::wstring responseLine = readPipeLine(pipe);
	CloseHandle(pipe);

	if (responseLine.empty()) {
		lastError = L"Music helper returned an empty response";
		return std::nullopt;
	}

	try {
		lastError.clear();
		return json::parse(util::WStrToStr(responseLine));
	}
	catch (...) {
		lastError = L"Music helper returned invalid JSON";
		return std::nullopt;
	}
}

bool MusicHelperClient::ensureHelperStarted() {
	auto helperPath = resolveHelperPath();
	if (helperPath.empty() || !ensureEmbeddedHelperExtracted(helperPath) || !std::filesystem::exists(helperPath)) {
		if (lastError.empty()) lastError = L"Embedded OmotiMusicHelper package could not be prepared";
		return false;
	}

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};

	std::wstring commandLine = L"\"" + helperPath.wstring() + L"\"";
	if (!CreateProcessW(
		helperPath.c_str(),
		commandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW | DETACHED_PROCESS,
		nullptr,
		helperPath.parent_path().c_str(),
		&si,
		&pi
	)) {
		lastError = std::format(L"Could not start helper process ({})", GetLastError());
		return false;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	for (int i = 0; i < 40; i++) {
		Sleep(100);
		auto response = sendRequest({ {"command", "ping"} }, false);
		if (response.has_value()) return true;
	}

	if (lastError.empty()) lastError = L"Music helper did not start in time";
	return false;
}

bool MusicHelperClient::ensureEmbeddedHelperExtracted(std::filesystem::path const& helperPath) {
	auto helperDir = helperPath.parent_path();
	std::error_code ec;
	std::filesystem::create_directories(helperDir, ec);
	if (ec) {
		lastError = std::format(L"Could not create helper runtime directory ({})", ec.value());
		return false;
	}

	auto versionText = std::string(Omoti::version.data(), Omoti::version.size());
	auto versionMarkerPath = helperDir / kHelperVersionFileName;

	bool needsRewrite = true;
	std::string existingVersion;
	if (readTextFile(versionMarkerPath, existingVersion) && existingVersion == versionText) {
		needsRewrite = false;

		for (auto const& file : getEmbeddedHelperFiles()) {
			auto targetPath = helperDir / file.fileName;
			if (!std::filesystem::exists(targetPath, ec) || ec) {
				needsRewrite = true;
				break;
			}

			auto size = std::filesystem::file_size(targetPath, ec);
			if (ec || size != static_cast<uintmax_t>(file.resource.size())) {
				needsRewrite = true;
				break;
			}
		}
	}

	if (!needsRewrite) {
		return true;
	}

	for (auto const& file : getEmbeddedHelperFiles()) {
		auto targetPath = helperDir / file.fileName;
		if (!writeResourceToFile(targetPath, file.resource, lastError)) {
			return false;
		}
	}

	{
		auto tempMarkerPath = versionMarkerPath;
		tempMarkerPath += L".tmp";
		std::ofstream out(tempMarkerPath, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			lastError = L"Could not write helper version marker";
			return false;
		}

		out.write(versionText.data(), static_cast<std::streamsize>(versionText.size()));
		if (!out.good()) {
			lastError = L"Could not finalize helper version marker";
			return false;
		}

		out.close();
		if (!MoveFileExW(tempMarkerPath.c_str(), versionMarkerPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
			lastError = std::format(L"Could not finalize helper version marker ({})", GetLastError());
			DeleteFileW(tempMarkerPath.c_str());
			return false;
		}
	}

	return true;
}

void MusicHelperClient::updateStatusFromJson(MusicHelperStatus& status, json const& response) const {
	status.ok = response.value("ok", false);
	status.helperAvailable = true;
	status.state = util::StrToWStr(response.value("state", std::string("stopped")));
	status.path = util::StrToWStr(response.value("path", std::string("")));
	status.error = util::StrToWStr(response.value("error", std::string("")));
	status.positionMs = response.value("positionMs", 0);
	status.durationMs = response.value("durationMs", -1);
	status.volume = response.value("volume", 1.f);
}

std::filesystem::path MusicHelperClient::resolveHelperPath() const {
	auto runtimeBase = util::GetOmotiPath();
	if (runtimeBase.empty()) {
		runtimeBase = getFallbackRuntimeBasePath();
	}

	auto versionFolder = getEmbeddedHelperVersion();
	return runtimeBase / L"Runtime" / L"EmbeddedHelper" / versionFolder / kHelperExeName;
}
