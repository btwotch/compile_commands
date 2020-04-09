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
#include <unistd.h>
#include <fcntl.h>

namespace fs = std::filesystem;

static std::string execLogPrefix{"exec.log."};

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
	fs::path pathToExec = getOriginalPath(argv[0]);

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
	argv[0] = strdup(pathToExec.string().c_str());
	execvp(pathToExec.string().c_str(), argv);

	free(argv[0]);

	return 0;
}

void bindMount(const fs::path &from, const fs::path &to) {

}

int invocateBuild(char **argv) {
	int status = -1;
	pid_t pid = -1;

	char *tmpDir = strdup("cc-logdir-XXXXXX");

	if (mkdtemp(tmpDir) == nullptr) {
		std::cerr << "mkdtemp failed: " << strerror(errno) << std::endl;
		exit(-1);
	}
	fs::path tmpDirPath{tmpDir};
	tmpDirPath = fs::canonical(tmpDirPath);

	pid = fork();
	if (pid == 0) {
		std::string pathEnvVar = std::string{getenv("PATH")};
		// memory leak ahead
		pathEnvVar = std::string{get_current_dir_name()} + ":" + pathEnvVar;
		setenv("PATH", pathEnvVar.c_str(), 1);
		setenv("CC_LOGDIR", tmpDirPath.string().c_str(), 1);
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
	std::cout << tmpDirPath << std::endl;

	int count = 1;
	while (count < std::numeric_limits<int>::max()) {
		fs::path logfile = tmpDirPath / (execLogPrefix + std::to_string(count));
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

	fs::remove_all(tmpDirPath);

	return status;
}

int main(int argc, char **argv) {
	std::set<std::string> compilerInvocations{"clang", "clang++", "gcc", "cc", "c++", "g++"};

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
