#include "catch.hpp"

#include "cdrom.h"

#include <filesystem>
#include <fstream>
#include <vector>


TEST_CASE("CD-ROM Q subchannel indexes", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-q-test";
	const std::filesystem::path binpath = tempdir / "indexes.bin";
	const std::filesystem::path cuepath = tempdir / "indexes.cue";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	// Nine seconds of zero-filled CDDA are enough to cover INDEX 01/02/03.
	{
		std::ofstream bin(binpath, std::ios::binary);
		const std::vector<char> data(9 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"indexes.bin\" BINARY\n"
				"  TRACK 01 AUDIO\n"
				"    INDEX 01 00:00:00\n"
				"    INDEX 02 00:03:00\n"
				"    INDEX 03 00:06:00\n";
	}

	cdrom_file cd(cuepath.string());

	uint8_t q[12];

	// Last frame of INDEX 01.
	REQUIRE(cd.get_subcode_q(224, q));
	REQUIRE(q[2] == 0x01);

	// First frame of INDEX 02.
	REQUIRE(cd.get_subcode_q(225, q));
	REQUIRE(q[2] == 0x02);

	// Last frame of INDEX 02.
	REQUIRE(cd.get_subcode_q(449, q));
	REQUIRE(q[2] == 0x02);

	// First frame of INDEX 03.
	REQUIRE(cd.get_subcode_q(450, q));
	REQUIRE(q[2] == 0x03);

	std::filesystem::remove_all(tempdir);
}
