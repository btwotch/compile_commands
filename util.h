#include <iostream>
#include <filesystem>

#include <stdlib.h>

#pragma once

namespace fs = std::filesystem;

class TemporaryDir {
public:
        TemporaryDir(const std::string &pathTemplate) {
		pathTemplateChar = strdup(pathTemplate.c_str());
		mkdtemp(pathTemplateChar);
                tmpPath = fs::path(pathTemplateChar);
        }

        ~TemporaryDir() {
		if (cleanup) {
			fs::remove_all(tmpPath);
		}
		free(pathTemplateChar);
        }
        operator std::string() {
                return tmpPath.string();
        };
        operator fs::path() {
                return tmpPath;
        }
        std::string string() {
                return tmpPath.string();
        }
	fs::path path() {
		return tmpPath;
	}

	void disableCleanup() {
		cleanup = false;
	}
private:
        fs::path tmpPath;
	bool cleanup = true;
	char *pathTemplateChar;
};

