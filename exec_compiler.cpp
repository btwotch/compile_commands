#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <cstring>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>

#include "util.h"

namespace fs = std::filesystem;

static std::string execLogPrefix{"exec.log."};

static std::set<std::string> compilerInvocations{"clang", "clang++", "gcc", "cc", "c++", "g++"};

static fs::path ownPath() {
	char buf[4096];

	ssize_t length = readlink("/proc/self/exe", static_cast<char*>(buf), sizeof(buf));
	if (length > sizeof(buf)) {
		std::cerr << "link of /proc/self/exe is too long" << std::endl;
		exit(1);
	}

	buf[length] = '\0';

	fs::path exePath{buf};

	return exePath;
}

static fs::path ownDir() {
	return ownPath().parent_path();
}

fs::path getOriginalPath(const std::string &exe) {
	fs::path ownExeDir = ownDir();

	std::set<fs::path> envPaths;

	std::string pathEnvVar = std::string{getenv("PATH")};

	std::stringstream ss(pathEnvVar);
	std::string pathElem;
	//envPaths.insert("/bin");
	while(std::getline(ss, pathElem, ':')) {
		if (pathElem != ownExeDir) {
			envPaths.insert(pathElem);
		}
	}

	fs::path newExePath;
	for (const fs::path &path : envPaths) {
		newExePath = path / exe;

		if (!access(newExePath.string().c_str(), X_OK)) {
			break;
		}
	}

	return fs::canonical(newExePath);
}

std::ofstream nextLogFileHandle() {
	static fs::path logDir;
	if (logDir.empty()) {
		char *env = getenv("CC_LOGDIR");
		if (env == nullptr) {
			std::cerr << "CC_LOGDIR not set" << std::endl;
			exit(-1);
		}
		logDir = fs::path{getenv("CC_LOGDIR")};
	}


	std::ofstream nextLogFileHandle;
	fs::path lockFile = logDir / "ec.lock";
	uint32_t highestProcessNumber = 0;

	int fd = open(lockFile.string().c_str(), O_WRONLY | O_CREAT, 0600);
	if (fd < 0) {
		std::cerr << "could not open: " << lockFile << ": " << std::strerror(errno) << std::endl;
		exit(-1);
	}

	if (flock(fd, LOCK_EX) != 0) {
		std::cerr << "locking: " << lockFile << " failed: " << std::strerror(errno) << std::endl;
		exit(-1);
	}

	for (const auto &entry : fs::directory_iterator(logDir)) {
		if (entry.path().filename().string().rfind(execLogPrefix, 0)) {
			continue;
		}
		std::string processNumberString = entry.path().filename().string().erase(0, execLogPrefix.size());
		uint32_t processNumber = std::stoi(processNumberString);
		if (processNumber > highestProcessNumber) {
			highestProcessNumber = processNumber;
		}
	}

	std::string logFilename = execLogPrefix + std::to_string(highestProcessNumber + 1);
	nextLogFileHandle.open(logDir / logFilename);
	if (close(fd) < 0) {
		std::cerr << "closing: " << lockFile << " failed: " << std::strerror(errno) << std::endl;
		exit(-1);
	}

	return nextLogFileHandle;
}

fs::path detectFileFromArgv(char **argv) {
	*argv++;
	while (*argv != NULL) {
		if (*argv[0] == '-') {
			// let's hope nobody uses files that begin with '-'
			*argv++;
			continue;
		}

		if (fs::exists(*argv)) {
			return fs::path{*argv};
		}
		*argv++;
	}

	return fs::path{};
}

void logExec(const fs::path &exe, char **argv) {
	std::ofstream execLogFile = nextLogFileHandle();

	char *currentDir = get_current_dir_name();

	fs::path file = detectFileFromArgv(argv);
	if (file.empty()) {
		file = fs::path{currentDir};
	}

	execLogFile << "CWD: " << currentDir << '\n';
	execLogFile << "FILE: " << file << '\n';
	execLogFile << "CMD: " << exe.string();
	*argv++;
	while (*argv != NULL) {
		execLogFile << " " << *argv;
		*argv++;
	}

	execLogFile << '\n';

	free(currentDir);
}

int execCompiler(int argc, char **argv) {
	fs::path ccBinDirPath = fs::path{getenv("CC_BINDIR")};
	fs::path argvPath = fs::path{argv[0]};
	fs::path pathToExec = ccBinDirPath / argvPath.filename();

	if (pathToExec.empty()) {
		errno = ENOENT;
		return -1;
	}
	logExec(pathToExec, argv);

#if 0
	char **newArgv;
	newArgv = argv;
	while (*newArgv != NULL) {
		printf("-- %s\n", *newArgv);
		*newArgv++;
	}
#endif

	// gcc has a weird bug if argv[0] == "./gcc"
	argv[0] = strdup(pathToExec.filename().string().c_str());
	execvp(pathToExec.string().c_str(), argv);

	free(argv[0]);

	return 0;
}

void bindMount(const fs::path &from, const fs::path &to) {
	if (!fs::exists(to)) {
		std::ofstream toFileHandle(to);
		toFileHandle << "";
	}
	std::cout << "bindMount: " << from << " --> " << to << std::endl;
	mount(from.string().c_str(), to.string().c_str(), "none", MS_BIND, NULL);
}

int invocateBuild(char **argv) {
	int status = -1;
	pid_t pid = -1;

	TemporaryDir logDir("/tmp/cc-logdir-XXXXXX");
	TemporaryDir binDir("/tmp/cc-bindir-XXXXXX");

	uid_t uid = getuid();
	gid_t gid = getgid();
	pid = fork();
	if (pid == 0) {
		unshare(CLONE_NEWNS|CLONE_NEWUSER|CLONE_NEWPID);
		if (fork()) {
			int status = -1;
			wait(&status);
			exit(status);
		}
		//std::string pathEnvVar = std::string{getenv("PATH")};
		// memory leak ahead
		//pathEnvVar = std::string{get_current_dir_name()} + ":" + pathEnvVar;
		//setenv("PATH", pathEnvVar.c_str(), 1);
		mount("none", "/", NULL, MS_REC|MS_PRIVATE, NULL);
		mount("none", "/proc", NULL, MS_REC|MS_PRIVATE, NULL);
		char mappingBuf[512];
		int setgroupFd = open( "/proc/self/setgroups", O_WRONLY);
		write(setgroupFd, "deny", 4);
		close(setgroupFd);
		int uid_mapFd = open("/proc/self/uid_map", O_WRONLY);
		snprintf(mappingBuf, 512, "0 %d 1", uid);
		write(uid_mapFd, mappingBuf, strlen(mappingBuf));
		close(uid_mapFd);
		int gid_mapFd = open("/proc/self/gid_map", O_WRONLY);
		snprintf(mappingBuf, 512, "0 %d 1", gid);
		write(uid_mapFd, mappingBuf, strlen(mappingBuf));
		close(gid_mapFd);
		for (const auto &compilerBin : compilerInvocations) {
			fs::path origPath = getOriginalPath(compilerBin);
			if (origPath.empty()) {
				continue;
			}
			bindMount(origPath, binDir.path() / compilerBin);
		}
		for (const auto &compilerBin : compilerInvocations) {
			fs::path origPath = getOriginalPath(compilerBin);
			if (origPath.empty()) {
				continue;
			}
			bindMount(fs::canonical(fs::path{"ec"}), origPath);
		}
		setenv("CC_LOGDIR", logDir.string().c_str(), 1);
		setenv("CC_BINDIR", binDir.string().c_str(), 1);
		execvp(argv[0], argv);
	} else if (pid < 0) {
		std::cerr << "fork failed: " << strerror(errno) << std::endl;
		exit(-1);
	}

	if (wait(&status) < 0) {
		std::cerr << "wait failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	printf(">>> %d\n", status);
	std::cout << logDir.string() << std::endl;

	int count = 1;
	while (count < std::numeric_limits<int>::max()) {
		fs::path logfile = logDir.path() / (execLogPrefix + std::to_string(count));
		if (!fs::exists(logfile)) {
			break;
		}

		std::ifstream logfileStream;
		logfileStream.open(logfile);

		std::string line;
		while (std::getline(logfileStream, line)) {
			std::cout << ": " << line << std::endl;
		}
		count++;
	}

	return status;
}

int main(int argc, char **argv) {
	fs::path ownCmd = fs::path{argv[0]}.filename();
	if (compilerInvocations.count(ownCmd.string()) > 0) {
		std::cout << "starting: " << ownCmd << std::endl;
		return execCompiler(argc, argv);
	}

	if (ownCmd != "ec") {
		std::cout << "unknown cmd: " << ownCmd << std::endl;
		exit(-1);
	}
	*argv++;
	return invocateBuild(argv);
}
