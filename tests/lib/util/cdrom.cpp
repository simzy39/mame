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


TEST_CASE("CD-ROM Q subchannel pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-q-pregap-test";
	const std::filesystem::path binpath = tempdir / "pregap.bin";
	const std::filesystem::path cuepath = tempdir / "pregap.cue";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(binpath, std::ios::binary);
		const std::vector<char> data(8 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"pregap.bin\" BINARY\n"
				"  TRACK 01 AUDIO\n"
				"    INDEX 00 00:00:00\n"
				"    INDEX 01 00:02:00\n"
				"    INDEX 02 00:05:00\n";
	}

	cdrom_file cd(cuepath.string());

	uint8_t q[12];

	// First frame of the pregap:
	// INDEX 00 and two seconds remaining until INDEX 01.
	REQUIRE(cd.get_subcode_q(0, q, true));
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x02);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[10] == 0x96);
	REQUIRE(q[11] == 0xbb);

	// Last frame before INDEX 01:
	// INDEX 00 with one frame remaining.
	REQUIRE(cd.get_subcode_q(149, q, true));
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x01);
	REQUIRE(q[10] == 0xba);
	REQUIRE(q[11] == 0x88);

	// First frame of INDEX 01:
	// relative time resets to 00:00:00.
	REQUIRE(cd.get_subcode_q(150, q, true));
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[10] == 0xf0);
	REQUIRE(q[11] == 0x8e);

	// Last frame of INDEX 01 before INDEX 02.
	REQUIRE(cd.get_subcode_q(374, q, true));
	REQUIRE(q[2] == 0x01);

	// INDEX 02 begins three seconds after INDEX 01.
	REQUIRE(cd.get_subcode_q(375, q, true));
	REQUIRE(q[2] == 0x02);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x03);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[10] == 0xa3);
	REQUIRE(q[11] == 0x48);

	std::filesystem::remove_all(tempdir);
}
