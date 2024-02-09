// Licensed under CC0 1.0
// License terms: https://creativecommons.org/publicdomain/zero/1.0/

// The implementation is based on the findings of
// https://www.watto.org/specs.html?specs=Archive_RWD_TGCK
// Many thanks to them!

import std;

#include <stdio.h>
#include <stdlib.h>


using namespace std;
namespace fs = filesystem;

static_assert(endian::native == endian::little, "This program only supports LE systems.");

#pragma pack(push)
#pragma pack(1)
struct IntroSection
{
	struct
	{
		char signature[4];
		char uknown1[4];
		char uknown2[4];
		char uknown3[4];
		uint16_t descriptionLength;
	} partOne;

	u16string description;

	struct
	{
		char zeros[4];
		char unknown[4];
	} partTwo;
};


struct HeaderSection
{
	char text[64];
	uint64_t headerOffset;
	uint64_t headerLength1;
	char unknown1[4];
	char unknown2[4];
	uint64_t headerLength2;
};

struct FilesSection
{
	char text[64];
	uint64_t fileDataOffset;
	uint64_t fileDataLength1;
	char unknown1[4];
	char unknown2[4];
	uint64_t fileDataLength2;
};

struct FooterSection
{
	char text[64];
	uint64_t directoryOffset;
	uint64_t directoryLength1;
	char unknown1[4];
	char unknown2[4];
	uint64_t directoryLength2;
};

struct Metadata
{
	char padding[4];
	HeaderSection header;
	FilesSection files;
	FooterSection footer;
};
#pragma pack(pop)


struct FileInfo
{
	char typeId[4];
	uint16_t filenameLength;
	u16string filename;
	uint64_t offsetFromStartOfFileData;
	uint64_t size;
	char zeros[4];
};


auto readIntro(ifstream& rwdFile)
{
	rwdFile.seekg(0);

	auto intro = make_unique<IntroSection>();

	auto& partOne = intro->partOne;
	auto& partTwo = intro->partTwo;

	rwdFile.read(reinterpret_cast<char*>(&partOne), sizeof(partOne));
	intro->description.resize(partOne.descriptionLength);
	rwdFile.read(reinterpret_cast<char*>(intro->description.data()), partOne.descriptionLength * 2);
	rwdFile.read(reinterpret_cast<char*>(&partTwo), sizeof(partTwo));

	if (strncmp(partOne.signature, "TGCK", 4))
		throw runtime_error("invalid RWD file: \"TGCK\" signature missing");

	return intro;
}

void writeIntro(ostream& rwdOut, const IntroSection& intro)
{
	auto& partOne = intro.partOne;
	auto& partTwo = intro.partTwo;

	rwdOut.write(reinterpret_cast<const char*>(&partOne), sizeof(partOne));
	rwdOut.write(reinterpret_cast<const char*>(intro.description.c_str()), intro.description.size() * 2);
	rwdOut.write(reinterpret_cast<const char*>(&partTwo), sizeof(partTwo));
}

auto readMetadata(ifstream& rwdFile)
{
	rwdFile.seekg(-static_cast<streamoff>(sizeof(Metadata)), ifstream::end);

	auto metadata = make_unique<Metadata>();

	rwdFile.read(reinterpret_cast<char*>(metadata.get()), sizeof(Metadata));

	if (metadata->header.headerLength1 != metadata->header.headerLength2)
		throw runtime_error("invalid RWD file: mismatched 'Header' lengths");

	if (metadata->files.fileDataLength1 != metadata->files.fileDataLength2)
		throw runtime_error("invalid RWD file: mismatched 'Files' lengths");

	if (metadata->footer.directoryLength1 != metadata->footer.directoryLength2)
		throw runtime_error("invalid RWD file: mismatched 'Footer' lengths");

	return metadata;
}

void writeMetadata(ostream& rwdOut, const Metadata& metadata)
{
	rwdOut.write(reinterpret_cast<const char*>(&metadata), sizeof(Metadata));
}

template<class T>
concept FileHandler = invocable<T, ifstream&, const FilesSection&, FileInfo>;

void forFile(ifstream& rwdFile, const Metadata& md, FileHandler auto&& handler)
{
	uint64_t currentOffset = md.footer.directoryOffset;
	const uint64_t maxOffset = md.footer.directoryOffset + md.footer.directoryLength1;
	while (currentOffset + sizeof(Metadata::padding) < maxOffset)
	{
		rwdFile.seekg(currentOffset);

		FileInfo info;
		rwdFile.read(info.typeId, sizeof(info.typeId));
		rwdFile.read(reinterpret_cast<char*>(&info.filenameLength), sizeof(info.filenameLength));
		info.filename.resize(info.filenameLength);
		rwdFile.read(reinterpret_cast<char*>(info.filename.data()), info.filenameLength * 2);
		rwdFile.read(reinterpret_cast<char*>(&info.offsetFromStartOfFileData), sizeof(info.offsetFromStartOfFileData));
		rwdFile.read(reinterpret_cast<char*>(&info.size), sizeof(info.size));
		rwdFile.read(info.zeros, sizeof(info.zeros));

		currentOffset = rwdFile.tellg();

		handler(rwdFile, md.files, info);
	}
}

void writeFileContent(ostream& rwdOut, const FilesSection& filesSection, const fs::path& sourceDir, FileInfo& info)
{
	ifstream file((sourceDir / info.filename).lexically_normal(), ifstream::binary);
	file.exceptions(ifstream::badbit | ifstream::failbit | ifstream::eofbit);

	file.seekg(0, ifstream::end);
	info.size = file.tellg();
	file.seekg(0);

	if (info.size > 0)
		info.offsetFromStartOfFileData = static_cast<uint64_t>(rwdOut.tellp()) - filesSection.fileDataOffset;

	copy(istreambuf_iterator(file), istreambuf_iterator<char>(), ostreambuf_iterator(rwdOut));
}

void writeDirectoryContent(ostream& rwdOut, const FileInfo& info)
{
	rwdOut.write(info.typeId, sizeof(info.typeId));
	rwdOut.write(reinterpret_cast<const char*>(&info.filenameLength), sizeof(info.filenameLength));
	rwdOut.write(reinterpret_cast<const char*>(info.filename.c_str()), info.filename.size() * 2);
	rwdOut.write(reinterpret_cast<const char*>(&info.offsetFromStartOfFileData), sizeof(info.offsetFromStartOfFileData));
	rwdOut.write(reinterpret_cast<const char*>(&info.size), sizeof(info.size));
	rwdOut.write(info.zeros, sizeof(info.zeros));
}

void printFilePaths(ifstream&, const FilesSection&, const FileInfo& info)
{
	println("{}", fs::path(info.filename).generic_string());
}

class Unpack final
{
public:
	Unpack(fs::path targetDirectory) : myTargetDirectory(move(targetDirectory))
	{}

	void operator()(ifstream& rwdFile, const FilesSection& filesSection, const FileInfo& info) const
	{
		println("Extracting: {}", fs::path(info.filename).generic_string());
		rwdFile.seekg(filesSection.fileDataOffset + info.offsetFromStartOfFileData);

		auto outputPath = (myTargetDirectory / fs::path(info.filename)).lexically_normal();
		fs::create_directories(outputPath.parent_path());
		ofstream output(outputPath, ofstream::binary);
		output.exceptions(ofstream::badbit | ofstream::failbit | ofstream::eofbit);
		copy_n(istreambuf_iterator(rwdFile), info.size, ostreambuf_iterator(output));
	}

private:
	fs::path myTargetDirectory;
};

void assertArgs(bool c, string msg)
{
	static const char* const USAGE_STR =
		"\n\n"
		"Usage: rwd_edit <mode> <mode_arguments>\n\n"
		"Modes:\n"
		"  list <rwd_file>\n"
		"      lists the content of rwd_file\n\n"
		"  pack <rwd_file> <directory>\n"
		"      replaces the content of rwd_file with that in directory\n\n"
		"  unpack <rwd_file> <empty_directory>\n"
		"      extracts the content of rwd_file to empty_directory\n";

	if (!c)
		throw runtime_error(move(msg) + USAGE_STR);
}

int main(int argc, char** argv) try
{
	assertArgs(argc >= 3, "missing or invalid arguments");

	const string mode = argv[1];
	assertArgs(
		mode == "list" && argc == 3 || (mode == "pack" || mode == "unpack") && argc == 4,
		"missing or invalid arguments"
	);

	const fs::path rwdPath(argv[2]);
	assertArgs(fs::is_regular_file(rwdPath), rwdPath.string() + " must be an existing regular file");
	ifstream rwdFile(rwdPath, ifstream::binary);
	rwdFile.exceptions(ifstream::badbit | ifstream::failbit | ifstream::eofbit);

	auto intro = readIntro(rwdFile);
	auto metadata = readMetadata(rwdFile);

	if (mode == "list")
	{
		forFile(rwdFile, *metadata, printFilePaths);
	}
	else if (mode == "pack")
	{
		fs::path sourceDirectory(argv[3]);
		assertArgs(fs::is_directory(sourceDirectory), sourceDirectory.string() + " must be a directory");

		fs::path tmpPath = rwdPath.string() + ".tmp";
		{
			println("Parsing {}...", rwdPath.string());
			list<FileInfo> infosInOriginalOrder;
			forFile(rwdFile, *metadata, [&](ifstream&, const FilesSection&, FileInfo info)
			{
				infosInOriginalOrder.emplace_back(move(info));
			});

			println("Packing files into {}...", tmpPath.string());
			ofstream rwdOut(tmpPath, ofstream::binary | ofstream::noreplace);
			if (rwdOut.fail())
				throw runtime_error(tmpPath.string() + " may have been left in place from a failed run - plase clean up manually");
			rwdOut.exceptions(ofstream::badbit | ofstream::failbit | ofstream::eofbit);

			writeIntro(rwdOut, *intro);

			vector<FileInfo*> infosInOffsetOrder
				= infosInOriginalOrder
				| views::transform([](FileInfo& info) { return &info; })
				| ranges::to<vector>();
			ranges::sort(
				infosInOffsetOrder,
				[](auto lhs, auto rhs) { return lhs->offsetFromStartOfFileData < rhs->offsetFromStartOfFileData; }
			);

			for (FileInfo* info : infosInOffsetOrder)
				writeFileContent(rwdOut, metadata->files, sourceDirectory, *info);

			const uint64_t directoryOffset = rwdOut.tellp();

			for (const FileInfo& info : infosInOriginalOrder)
				writeDirectoryContent(rwdOut, info);

			metadata->footer.directoryOffset = directoryOffset;
			writeMetadata(rwdOut, *metadata);
		}

		rwdFile.close();

		println("Renaming {} to {}...", tmpPath.string(), rwdPath.string());
		fs::rename(tmpPath, rwdPath);
		println("Finished.");
	}
	else if (mode == "unpack")
	{
		fs::path targetDirectory(argv[3]);
		assertArgs(
			fs::is_directory(targetDirectory) && fs::is_empty(targetDirectory),
			targetDirectory.string() + " must be an empty directory"
		);
		forFile(rwdFile, *metadata, Unpack(targetDirectory));
	}
}
catch (const exception& ex)
{
	println(stderr, "{}", ex.what());
	return EXIT_FAILURE;
}
catch (...)
{
	println(stderr, "Fatal error!");
	return EXIT_FAILURE;
}
