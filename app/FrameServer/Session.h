#pragma once

namespace imqs {
namespace frameserver {

// A session is a single video that one client is busy streaming
class Session {
public:
	anno::VideoFile Video;
};
typedef std::shared_ptr<Session> SessionPtr;

// Stores the active sessions
class SessionStore {
public:
	uberlog::Logger* Log = nullptr;

	// All video paths are relative to this directory
	std::string RootPath;

	// After this much time has elapsed without any usage, a session expires
	time::Duration ExpiryTimeout = 60 * time::Second;

	SessionStore(std::string rootPath, uberlog::Logger* log);
	~SessionStore();

	// Create a new session
	Error CreateSession(std::string path, std::string& id);

	// Get a session from ID, or return null if session has expired or is invalid
	SessionPtr GetSession(std::string id);

	void PurgeExpiredSessions();

private:
	struct Item {
		time::Time LastUsed;
		SessionPtr Session;
	};
	std::mutex                    Lock;
	ohash::map<std::string, Item> Sessions;

	void PurgeExpiredSessions_Internal();
};

} // namespace frameserver
} // namespace imqs