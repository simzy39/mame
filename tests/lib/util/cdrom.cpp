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

static void interleave_q_raw(const uint8_t *q, uint8_t *sub)
{
	for (int i = 0; i < 96; i++)
		sub[i] = ((q[i >> 3] >> (7 - (i & 7))) & 1) ? 0x40 : 0x00;
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

TEST_CASE("CD-ROM Q subchannel virtual pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-q-virtual-pregap-test";
	const std::filesystem::path binpath = tempdir / "virtual.bin";
	const std::filesystem::path tocpath = tempdir / "virtual.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	// The pregap is virtual: it is described by the TOC but is not present
	// in the source data.
	{
		std::ofstream bin(binpath, std::ios::binary);
		const std::vector<char> data(6 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO\n"
				"START 00:02:00\n"
				"DATAFILE \"virtual.bin\" 00:06:00\n";
	}

	cdrom_file cd(tocpath.string());

	uint8_t q[12];

	// Logical access to the virtual pregap synthesizes INDEX 00.
	REQUIRE(cd.get_subcode_q(0, q));
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x02);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x00);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Last virtual pregap frame.
	REQUIRE(cd.get_subcode_q(149, q));
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x01);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x01);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// Logical INDEX 01.
	REQUIRE(cd.get_subcode_q(150, q));
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x02);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Physical sector zero is already INDEX 01 because the pregap is not
	// actually stored in the source image.
	REQUIRE(cd.get_subcode_q(0, q, true));
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x02);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM Q subchannel encode decode round trip", "[util][cdrom]")
{
	cdrom_file::q_position input;
	input.adr_control =
			(cdrom_file::CD_FLAG_ADR_START_TIME << 4)
				| cdrom_file::CD_FLAG_CONTROL_DATA_TRACK;
	input.track = 12;
	input.index = 3;
	input.relative_frame = (4 * 60 * 75) + (23 * 75) + 17;
	input.absolute_frame = (37 * 60 * 75) + (41 * 75) + 62;

	uint8_t q[12];
	cdrom_file::encode_subcode_q(input, q);

	require_valid_q_crc(q);

	cdrom_file::q_position output;

	REQUIRE(cdrom_file::decode_subcode_q(q, output));

	REQUIRE(output.adr_control == input.adr_control);
	REQUIRE(output.track == input.track);
	REQUIRE(output.index == input.index);
	REQUIRE(output.relative_frame == input.relative_frame);
	REQUIRE(output.absolute_frame == input.absolute_frame);
}


TEST_CASE("CD-ROM Q subchannel raw packing round trip", "[util][cdrom]")
{
	cdrom_file::q_position position;
	position.adr_control =
			(cdrom_file::CD_FLAG_ADR_START_TIME << 4)
				| cdrom_file::CD_FLAG_CONTROL_PREEMPHASIS
				| cdrom_file::CD_FLAG_CONTROL_DIGITAL_COPY_PERMITTED;
	position.track = 7;
	position.index = 4;
	position.relative_frame = (2 * 60 * 75) + (11 * 75) + 37;
	position.absolute_frame = (18 * 60 * 75) + (52 * 75) + 9;

	uint8_t original_q[12];
	cdrom_file::encode_subcode_q(position, original_q);

	uint8_t raw_subcode[96];
	cdrom_file::pack_subcode_q(original_q, raw_subcode);

	uint8_t unpacked_q[12];
	cdrom_file::unpack_subcode_q(raw_subcode, unpacked_q);

	for (int i = 0; i < 12; i++)
		REQUIRE(unpacked_q[i] == original_q[i]);

	require_valid_q_crc(unpacked_q);
}


TEST_CASE("CD-ROM Q subchannel decoder rejects invalid CRC", "[util][cdrom]")
{
	cdrom_file::q_position input;
	input.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
	input.track = 1;
	input.index = 2;
	input.relative_frame = 225;
	input.absolute_frame = 375;

	uint8_t q[12];
	cdrom_file::encode_subcode_q(input, q);

	require_valid_q_crc(q);

	// Corrupt a protected byte without updating the CRC.
	q[2] ^= 0x01;

	cdrom_file::q_position output;
	REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, output));
}

TEST_CASE("CD-ROM Q subchannel decoder rejects invalid position data", "[util][cdrom]")
{
	cdrom_file::q_position input;
	input.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
	input.track = 1;
	input.index = 1;
	input.relative_frame = 0;
	input.absolute_frame = 150;

	uint8_t q[12];
	cdrom_file::encode_subcode_q(input, q);

	SECTION("invalid BCD")
	{
		q[1] = 0x1a;

		const uint16_t crc = reference_q_crc(q);
		q[10] = uint8_t(crc >> 8);
		q[11] = uint8_t(crc);

		cdrom_file::q_position output;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, output));
	}

	SECTION("invalid frame number")
	{
		q[5] = 0x75;

		const uint16_t crc = reference_q_crc(q);
		q[10] = uint8_t(crc >> 8);
		q[11] = uint8_t(crc);

		cdrom_file::q_position output;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, output));
	}

	SECTION("reserved byte is nonzero")
	{
		q[6] = 1;

		const uint16_t crc = reference_q_crc(q);
		q[10] = uint8_t(crc >> 8);
		q[11] = uint8_t(crc);

		cdrom_file::q_position output;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, output));
	}
}

TEST_CASE("CD-ROM Q subchannel stored RW_RAW", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-q-raw-test";
	const std::filesystem::path binpath = tempdir / "raw.bin";
	const std::filesystem::path tocpath = tempdir / "raw.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	// Make a distinctive, valid Q packet that cannot be confused with
	// the Q data synthesized from this track's TOC.
	uint8_t stored_q[12] =
	{
		0x01,       // ADR=1, CONTROL=0
		0x01,       // track 1
		0x07,       // deliberately use INDEX 07
		0x00, 0x12, 0x34,
		0x00,
		0x00, 0x56, 0x12,
		0x00, 0x00
	};

	const uint16_t crc = reference_q_crc(stored_q);
	stored_q[10] = uint8_t(crc >> 8);
	stored_q[11] = uint8_t(crc);

	uint8_t raw_subcode[96];
	interleave_q_raw(stored_q, raw_subcode);

	// One AUDIO RW_RAW sector is 2352 bytes of CDDA followed by
	// 96 bytes of raw interleaved P-W subchannel data.
	{
		std::ofstream bin(binpath, std::ios::binary);

		const std::vector<char> audio(2352, 0);
		bin.write(audio.data(), audio.size());
		bin.write(
				reinterpret_cast<const char *>(raw_subcode),
				sizeof(raw_subcode));
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO RW_RAW\n"
				"DATAFILE \"raw.bin\" 00:00:01\n";
	}

	cdrom_file cd(tocpath.string());

	uint8_t q[12];

	REQUIRE(cd.get_subcode_q(0, q));

	// Stored RW_RAW Q must take precedence over synthesized Q.
	for (int i = 0; i < 12; i++)
		REQUIRE(q[i] == stored_q[i]);

	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}
