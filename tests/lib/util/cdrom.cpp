#include "catch.hpp"

#include "cdrom.h"

#include <filesystem>
#include <fstream>
#include <vector>


static uint16_t reference_q_crc(const uint8_t *data)
{
	uint16_t crc = 0;

	for (int byte = 0; byte < 10; byte++)
	{
		crc ^= uint16_t(data[byte]) << 8;

		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 0x8000)
					? uint16_t((crc << 1) ^ 0x1021)
					: uint16_t(crc << 1);
	}

	return crc ^ 0xffff;
}


static void require_valid_q_crc(const uint8_t *q)
{
	const uint16_t crc = reference_q_crc(q);

	REQUIRE(q[10] == uint8_t(crc >> 8));
	REQUIRE(q[11] == uint8_t(crc));
}


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
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x04);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// First frame of INDEX 02.
	REQUIRE(cd.get_subcode_q(225, q));
	REQUIRE(q[2] == 0x02);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x05);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Last frame of INDEX 02.
	REQUIRE(cd.get_subcode_q(449, q));
	REQUIRE(q[2] == 0x02);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x07);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// First frame of INDEX 03.
	REQUIRE(cd.get_subcode_q(450, q));
	REQUIRE(q[2] == 0x03);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x08);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

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

	// First frame of INDEX 00.
	// Relative time counts backwards to INDEX 01.
	// Absolute disc position starts at 00:00:00.
	REQUIRE(cd.get_subcode_q(0, q, true));
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x02);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x00);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Last frame of INDEX 00.
	REQUIRE(cd.get_subcode_q(149, q, true));
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x01);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x01);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// INDEX 01 begins at disc LBA 0 / absolute MSF 00:02:00.
	REQUIRE(cd.get_subcode_q(150, q, true));
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x02);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Last frame before INDEX 02.
	REQUIRE(cd.get_subcode_q(374, q, true));
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x04);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// INDEX 02 begins three seconds after INDEX 01.
	// Relative address remains track-relative.
	REQUIRE(cd.get_subcode_q(375, q, true));
	REQUIRE(q[2] == 0x02);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x03);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x05);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}
