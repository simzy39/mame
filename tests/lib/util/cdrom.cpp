#include "catch.hpp"

#include "cdrom.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>


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

TEST_CASE("CD-ROM coordinate types remain distinct", "[util][cdrom]")
{
	cdrom_file::sector_position sector{ 321 };
	cdrom_file::channel_position channel{ 1234, 7 };
	cdrom_file::subcode_position subcode{ 5678 };
	cdrom_file::disc_position disc{ 42 };

	REQUIRE(sector.frame == 321);
	REQUIRE(channel.frame == 1234);
	REQUIRE(channel.byte_offset == 7);
	REQUIRE(subcode.frame == 5678);
	REQUIRE(disc.frame == 42);

	cdrom_file::captured_position sector_backed{
			sector,
			std::nullopt,
			subcode };

	REQUIRE(sector_backed.sector_data.has_value());
	REQUIRE(sector_backed.sector_data->frame == 321);
	REQUIRE_FALSE(sector_backed.main_channel.has_value());
	REQUIRE(sector_backed.subcode.has_value());
	REQUIRE(sector_backed.subcode->frame == 5678);

	cdrom_file::captured_position channel_backed{
			std::nullopt,
			channel,
			subcode };

	REQUIRE_FALSE(channel_backed.sector_data.has_value());
	REQUIRE(channel_backed.main_channel.has_value());
	REQUIRE(channel_backed.main_channel->frame == 1234);
	REQUIRE(channel_backed.main_channel->byte_offset == 7);
	REQUIRE(channel_backed.subcode.has_value());
	REQUIRE(channel_backed.subcode->frame == 5678);

	cdrom_file::captured_position subcode_only{
			std::nullopt,
			std::nullopt,
			subcode };

	REQUIRE_FALSE(subcode_only.sector_data.has_value());
	REQUIRE_FALSE(subcode_only.main_channel.has_value());
	REQUIRE(subcode_only.subcode.has_value());
	REQUIRE(subcode_only.subcode->frame == 5678);

		cdrom_file::backing_span span{
			{ 1000 },
			100,
			{
				cdrom_file::sector_position{ 500 },
				std::nullopt,
				cdrom_file::subcode_position{ 700 }
			}
	};

	REQUIRE(span.start.frame == 1000);
	REQUIRE(span.frames.has_value());
	REQUIRE(*span.frames == 100);
	REQUIRE(span.captured.sector_data.has_value());
	REQUIRE(span.captured.sector_data->frame == 500);
	REQUIRE_FALSE(span.captured.main_channel.has_value());
	REQUIRE(span.captured.subcode.has_value());
	REQUIRE(span.captured.subcode->frame == 700);
}

TEST_CASE("CD-ROM region supports multiple backing spans", "[util][cdrom]")
{
	cdrom_file::region program;
	program.kind = cdrom_file::region_kind::program;
	program.start = { 1000 };
	program.frames = 200;
	program.main_data = cdrom_file::region_presence::captured;
	program.subcode = cdrom_file::region_presence::captured;

	program.backing.push_back(
			{
				{ 1000 },
				100,
				{
					cdrom_file::sector_position{ 500 },
					std::nullopt,
					cdrom_file::subcode_position{ 700 }
				}
			});

	program.backing.push_back(
			{
				{ 1100 },
				100,
				{
					cdrom_file::sector_position{ 600 },
					std::nullopt,
					cdrom_file::subcode_position{ 799 }
				}
			});

	REQUIRE(program.backing.size() == 2);

	REQUIRE(program.backing[0].start.frame == 1000);
	REQUIRE(program.backing[0].frames.has_value());
	REQUIRE(*program.backing[0].frames == 100);
	REQUIRE(program.backing[0].captured.sector_data.has_value());
	REQUIRE(program.backing[0].captured.sector_data->frame == 500);
	REQUIRE(program.backing[0].captured.subcode.has_value());
	REQUIRE(program.backing[0].captured.subcode->frame == 700);

	REQUIRE(program.backing[1].start.frame == 1100);
	REQUIRE(program.backing[1].frames.has_value());
	REQUIRE(*program.backing[1].frames == 100);
	REQUIRE(program.backing[1].captured.sector_data.has_value());
	REQUIRE(program.backing[1].captured.sector_data->frame == 600);
	REQUIRE(program.backing[1].captured.subcode.has_value());
	REQUIRE(program.backing[1].captured.subcode->frame == 799);

	const cdrom_file::backing_span *span;

	span = cdrom_file::find_backing_span(program, { 999 });
	REQUIRE(span == nullptr);

	span = cdrom_file::find_backing_span(program, { 1000 });
	REQUIRE(span == &program.backing[0]);

	span = cdrom_file::find_backing_span(program, { 1099 });
	REQUIRE(span == &program.backing[0]);

	span = cdrom_file::find_backing_span(program, { 1100 });
	REQUIRE(span == &program.backing[1]);

	span = cdrom_file::find_backing_span(program, { 1199 });
	REQUIRE(span == &program.backing[1]);

	span = cdrom_file::find_backing_span(program, { 1200 });
	REQUIRE(span == nullptr);

		cdrom_file::region open_ended;
	open_ended.kind = cdrom_file::region_kind::lead_out;
	open_ended.start = { 2000 };
	open_ended.frames = std::nullopt;
	open_ended.main_data = cdrom_file::region_presence::unknown;
	open_ended.subcode = cdrom_file::region_presence::unknown;

	open_ended.backing.push_back(
			{
				{ 2000 },
				std::nullopt,
				{
					cdrom_file::sector_position{ 900 },
					std::nullopt,
					std::nullopt
				}
			});

	REQUIRE(cdrom_file::find_backing_span(open_ended, { 1999 }) == nullptr);
	REQUIRE(
			cdrom_file::find_backing_span(open_ended, { 2000 })
				== &open_ended.backing[0]);
	REQUIRE(
			cdrom_file::find_backing_span(open_ended, { 5000 })
				== &open_ended.backing[0]);

	REQUIRE(cdrom_file::validate_backing_spans(program));
	REQUIRE(cdrom_file::validate_backing_spans(open_ended));

	cdrom_file::region overlap = program;
	overlap.backing[1].start = { 1050 };
	REQUIRE_FALSE(cdrom_file::validate_backing_spans(overlap));

	cdrom_file::region zero_length = program;
	zero_length.backing[0].frames = 0;
	REQUIRE_FALSE(cdrom_file::validate_backing_spans(zero_length));

	cdrom_file::region before_region = program;
	before_region.backing[0].start = { 999 };
	REQUIRE_FALSE(cdrom_file::validate_backing_spans(before_region));

	cdrom_file::region past_region = program;
	past_region.backing[1].start = { 1150 };
	past_region.backing[1].frames = 100;
	REQUIRE_FALSE(cdrom_file::validate_backing_spans(past_region));

	cdrom_file::region finite_open_ended = program;
	finite_open_ended.backing[1].frames = std::nullopt;
	REQUIRE_FALSE(cdrom_file::validate_backing_spans(finite_open_ended));

	cdrom_file::region open_ended_not_last = open_ended;
	open_ended_not_last.backing.push_back(
			{
				{ 3000 },
				100,
				{
					cdrom_file::sector_position{ 1900 },
					std::nullopt,
					std::nullopt
				}
			});
	REQUIRE_FALSE(cdrom_file::validate_backing_spans(open_ended_not_last));

		cdrom_file::region with_gap = program;
	with_gap.backing[0].frames = 50;
	with_gap.backing[1].start = { 1100 };
	REQUIRE(cdrom_file::validate_backing_spans(with_gap));

	REQUIRE_FALSE(
		cdrom_file::backing_sector_position(program, { 999 }).has_value());

	auto sector =
			cdrom_file::backing_sector_position(program, { 1000 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 500);

	sector = cdrom_file::backing_sector_position(program, { 1099 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 599);

	sector = cdrom_file::backing_sector_position(program, { 1100 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 600);

	sector = cdrom_file::backing_sector_position(program, { 1199 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 699);

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(program, { 1200 }).has_value());

	cdrom_file::region no_sector = program;
	no_sector.backing[0].captured.sector_data = std::nullopt;

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(no_sector, { 1000 }).has_value());

	sector = cdrom_file::backing_sector_position(no_sector, { 1100 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 600);

	REQUIRE_FALSE(
		cdrom_file::backing_subcode_position(program, { 999 }).has_value());

	auto subcode =
			cdrom_file::backing_subcode_position(program, { 1000 });
	REQUIRE(subcode.has_value());
	REQUIRE(subcode->frame == 700);

	subcode = cdrom_file::backing_subcode_position(program, { 1099 });
	REQUIRE(subcode.has_value());
	REQUIRE(subcode->frame == 799);

	subcode = cdrom_file::backing_subcode_position(program, { 1100 });
	REQUIRE(subcode.has_value());
	REQUIRE(subcode->frame == 799);

	subcode = cdrom_file::backing_subcode_position(program, { 1199 });
	REQUIRE(subcode.has_value());
	REQUIRE(subcode->frame == 898);

	REQUIRE_FALSE(
			cdrom_file::backing_subcode_position(program, { 1200 }).has_value());

	cdrom_file::region no_subcode = program;
	no_subcode.backing[0].captured.subcode = std::nullopt;

	REQUIRE_FALSE(
			cdrom_file::backing_subcode_position(no_subcode, { 1000 }).has_value());

	subcode = cdrom_file::backing_subcode_position(no_subcode, { 1100 });
	REQUIRE(subcode.has_value());
	REQUIRE(subcode->frame == 799);

		cdrom_file::region channel_program = program;
	channel_program.backing[0].captured.main_channel =
			cdrom_file::channel_position{ 800, 12 };
	channel_program.backing[1].captured.main_channel =
			cdrom_file::channel_position{ 900, 34 };

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					channel_program, { 999 }).has_value());

	auto channel =
			cdrom_file::backing_channel_position(
					channel_program, { 1000 });
	REQUIRE(channel.has_value());
	REQUIRE(channel->frame == 800);
	REQUIRE(channel->byte_offset == 12);

	channel =
			cdrom_file::backing_channel_position(
					channel_program, { 1099 });
	REQUIRE(channel.has_value());
	REQUIRE(channel->frame == 899);
	REQUIRE(channel->byte_offset == 12);

	channel =
			cdrom_file::backing_channel_position(
					channel_program, { 1100 });
	REQUIRE(channel.has_value());
	REQUIRE(channel->frame == 900);
	REQUIRE(channel->byte_offset == 34);

	channel =
			cdrom_file::backing_channel_position(
					channel_program, { 1199 });
	REQUIRE(channel.has_value());
	REQUIRE(channel->frame == 999);
	REQUIRE(channel->byte_offset == 34);

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					channel_program, { 1200 }).has_value());

	channel_program.backing[0].captured.main_channel = std::nullopt;

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					channel_program, { 1000 }).has_value());

	channel =
			cdrom_file::backing_channel_position(
					channel_program, { 1100 });
	REQUIRE(channel.has_value());
	REQUIRE(channel->frame == 900);
	REQUIRE(channel->byte_offset == 34);

		cdrom_file::disc_track lookup_track;
	lookup_track.regions.push_back(
			{
				cdrom_file::region_kind::pregap,
				{ 900 },
				100,
				cdrom_file::region_presence::unknown,
				cdrom_file::region_presence::unknown,
				{}
			});
	lookup_track.regions.push_back(program);

	const cdrom_file::region *found_region;

	found_region = cdrom_file::find_region(lookup_track, { 899 });
	REQUIRE(found_region == nullptr);

	found_region = cdrom_file::find_region(lookup_track, { 900 });
	REQUIRE(found_region == &lookup_track.regions[0]);

	found_region = cdrom_file::find_region(lookup_track, { 999 });
	REQUIRE(found_region == &lookup_track.regions[0]);

	found_region = cdrom_file::find_region(lookup_track, { 1000 });
	REQUIRE(found_region == &lookup_track.regions[1]);

	found_region = cdrom_file::find_region(lookup_track, { 1199 });
	REQUIRE(found_region == &lookup_track.regions[1]);

	found_region = cdrom_file::find_region(lookup_track, { 1200 });
	REQUIRE(found_region == nullptr);

	cdrom_file::disc_track gapped_track;

	cdrom_file::region first_region = program;
	first_region.start = { 1000 };
	first_region.frames = 50;

	cdrom_file::region second_region = program;
	second_region.start = { 1100 };
	second_region.frames = 50;

	gapped_track.regions.push_back(first_region);
	gapped_track.regions.push_back(second_region);

	REQUIRE(cdrom_file::find_region(gapped_track, { 1049 }) == &gapped_track.regions[0]);
	REQUIRE(cdrom_file::find_region(gapped_track, { 1050 }) == nullptr);
	REQUIRE(cdrom_file::find_region(gapped_track, { 1099 }) == nullptr);
	REQUIRE(cdrom_file::find_region(gapped_track, { 1100 }) == &gapped_track.regions[1]);

		REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					lookup_track, { 899 }).has_value());

	auto track_sector =
			cdrom_file::backing_sector_position(
					lookup_track, { 1000 });
	REQUIRE(track_sector.has_value());
	REQUIRE(track_sector->frame == 500);

	track_sector =
			cdrom_file::backing_sector_position(
					lookup_track, { 1100 });
	REQUIRE(track_sector.has_value());
	REQUIRE(track_sector->frame == 600);

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					lookup_track, { 1000 }).has_value());

	auto track_subcode =
			cdrom_file::backing_subcode_position(
					lookup_track, { 1000 });
	REQUIRE(track_subcode.has_value());
	REQUIRE(track_subcode->frame == 700);

	track_subcode =
			cdrom_file::backing_subcode_position(
					lookup_track, { 1100 });
	REQUIRE(track_subcode.has_value());
	REQUIRE(track_subcode->frame == 799);

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					gapped_track, { 1050 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_subcode_position(
					gapped_track, { 1099 }).has_value());

		cdrom_file::disc lookup_disc;
	lookup_disc.tracks.push_back(lookup_track);
	lookup_disc.tracks.push_back(gapped_track);

	const cdrom_file::disc_track *found_track;

	found_track = cdrom_file::find_track(lookup_disc, { 899 });
	REQUIRE(found_track == nullptr);

	found_track = cdrom_file::find_track(lookup_disc, { 900 });
	REQUIRE(found_track == &lookup_disc.tracks[0]);

	found_track = cdrom_file::find_track(lookup_disc, { 1000 });
	REQUIRE(found_track == &lookup_disc.tracks[0]);

	found_track = cdrom_file::find_track(lookup_disc, { 1049 });
	REQUIRE(found_track == &lookup_disc.tracks[0]);

	found_track = cdrom_file::find_track(lookup_disc, { 1199 });
	REQUIRE(found_track == &lookup_disc.tracks[0]);

	found_track = cdrom_file::find_track(lookup_disc, { 1200 });
	REQUIRE(found_track == nullptr);

		REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					lookup_disc, { 899 }).has_value());

	auto disc_sector =
			cdrom_file::backing_sector_position(
					lookup_disc, { 1000 });
	REQUIRE(disc_sector.has_value());
	REQUIRE(disc_sector->frame == 500);

	disc_sector =
			cdrom_file::backing_sector_position(
					lookup_disc, { 1100 });
	REQUIRE(disc_sector.has_value());
	REQUIRE(disc_sector->frame == 600);

		auto disc_position =
			cdrom_file::disc_position_from_sector_position(
					lookup_disc, { 500 });
	REQUIRE(disc_position.has_value());
	REQUIRE(disc_position->frame == 1000);

	disc_position =
			cdrom_file::disc_position_from_sector_position(
					lookup_disc, { 599 });
	REQUIRE(disc_position.has_value());
	REQUIRE(disc_position->frame == 1099);

	disc_position =
			cdrom_file::disc_position_from_sector_position(
					lookup_disc, { 600 });
	REQUIRE(disc_position.has_value());
	REQUIRE(disc_position->frame == 1100);

	disc_position =
			cdrom_file::disc_position_from_sector_position(
					lookup_disc, { 699 });
	REQUIRE(disc_position.has_value());
	REQUIRE(disc_position->frame == 1199);

	REQUIRE_FALSE(
			cdrom_file::disc_position_from_sector_position(
					lookup_disc, { 499 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::disc_position_from_sector_position(
					lookup_disc, { 700 }).has_value());
	
	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					lookup_disc, { 1000 }).has_value());

	auto disc_subcode =
			cdrom_file::backing_subcode_position(
					lookup_disc, { 1000 });
	REQUIRE(disc_subcode.has_value());
	REQUIRE(disc_subcode->frame == 700);

	disc_subcode =
			cdrom_file::backing_subcode_position(
					lookup_disc, { 1100 });
	REQUIRE(disc_subcode.has_value());
	REQUIRE(disc_subcode->frame == 799);

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					lookup_disc, { 1200 }).has_value());
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

	// Runtime index lookup must also recognize Track 1's virtual pregap.
	REQUIRE(cd.get_track_index(0) == 0);
	REQUIRE(cd.get_track_index(149) == 0);
	REQUIRE(cd.get_track_index(150) == 1);
	
	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM Q subchannel later-track virtual pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-q-later-virtual-pregap-test";
	const std::filesystem::path track1path = tempdir / "track1.bin";
	const std::filesystem::path track2path = tempdir / "track2.bin";
	const std::filesystem::path tocpath = tempdir / "later-virtual.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(track1path, std::ios::binary);
		const std::vector<char> data(3 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream bin(track2path, std::ios::binary);
		const std::vector<char> data(4 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO\n"
				"DATAFILE \"track1.bin\" 00:03:00\n"
				"\n"
				"TRACK AUDIO\n"
				"START 00:02:00\n"
				"DATAFILE \"track2.bin\" 00:00:00 00:04:00\n";
	}

	cdrom_file cd(tocpath.string());

	uint8_t q[12];

	// Track 2's virtual pregap begins immediately after track 1.
	REQUIRE(cd.get_subcode_q(225, q));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x02);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x05);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Last frame of track 2's virtual pregap.
	REQUIRE(cd.get_subcode_q(374, q));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x01);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x06);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// Track 2 INDEX 01 begins after the two-second virtual pregap.
	REQUIRE(cd.get_subcode_q(375, q));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x07);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Canonical track lookup must assign the virtual pregap to track 2.
	REQUIRE(cd.get_track(224) == 0);
	REQUIRE(cd.get_track(225) == 1);
	REQUIRE(cd.get_track(374) == 1);
	REQUIRE(cd.get_track(375) == 1);
	
	// Runtime index lookup must also associate the virtual pregap with track 2.
	REQUIRE(cd.get_track_index(225) == 0);
	REQUIRE(cd.get_track_index(374) == 0);
	REQUIRE(cd.get_track_index(375) == 1);

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM raw subcode later-track virtual pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-raw-later-virtual-pregap-test";
	const std::filesystem::path track1path = tempdir / "track1.bin";
	const std::filesystem::path track2path = tempdir / "track2.bin";
	const std::filesystem::path tocpath = tempdir / "later-virtual-raw.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	// Give track 1 captured RW_RAW subcode.  Make the Q data distinctive so
	// it cannot be confused with synthesized Q for track 2's virtual pregap.
	uint8_t stored_q[12] =
	{
		0x01,
		0x01,
		0x07,
		0x00, 0x00, 0x00,
		0x00,
		0x00, 0x02, 0x00,
		0x00, 0x00
	};

	const uint16_t crc = reference_q_crc(stored_q);
	stored_q[10] = uint8_t(crc >> 8);
	stored_q[11] = uint8_t(crc);

	uint8_t raw_subcode[96];
	interleave_q_raw(stored_q, raw_subcode);

	{
		std::ofstream bin(track1path, std::ios::binary);
		const std::vector<char> audio(2352, 0);

		// Track 1 is three seconds long.
		for (int frame = 0; frame < 3 * 75; frame++)
		{
			bin.write(audio.data(), audio.size());
			bin.write(
					reinterpret_cast<const char *>(raw_subcode),
					sizeof(raw_subcode));
		}
	}

	{
		std::ofstream bin(track2path, std::ios::binary);
		const std::vector<char> data(4 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO RW_RAW\n"
				"DATAFILE \"track1.bin\" 00:03:00\n"
				"\n"
				"TRACK AUDIO\n"
				"START 00:02:00\n"
				"DATAFILE \"track2.bin\" 00:00:00 00:04:00\n";
	}

	cdrom_file cd(tocpath.string());

	const cdrom_file::disc disc = cd.get_disc();

	REQUIRE(disc.tracks.size() == 2);

	const cdrom_file::disc_track &track1 = disc.tracks[0];
	REQUIRE(track1.regions.size() == 1);

	const cdrom_file::region &track1_program = track1.regions[0];
	REQUIRE(track1_program.kind == cdrom_file::region_kind::program);
	REQUIRE(track1_program.subcode == cdrom_file::region_presence::captured);

	REQUIRE(track1_program.backing.size() == 1);
	REQUIRE(track1_program.backing[0].start.frame == 0);
	REQUIRE(track1_program.backing[0].frames.has_value());
	REQUIRE(*track1_program.backing[0].frames == 225);
	REQUIRE(track1_program.backing[0].captured.sector_data.has_value());
	REQUIRE(track1_program.backing[0].captured.sector_data->frame == 0);
	REQUIRE_FALSE(track1_program.backing[0].captured.main_channel.has_value());
	REQUIRE(track1_program.backing[0].captured.subcode.has_value());
	REQUIRE(track1_program.backing[0].captured.subcode->frame == 0);

	const cdrom_file::disc_track &track2 = disc.tracks[1];
	REQUIRE(track2.regions.size() >= 2);

	const cdrom_file::region &track2_pregap = track2.regions[0];
	REQUIRE(track2_pregap.kind == cdrom_file::region_kind::pregap);
	REQUIRE(track2_pregap.subcode == cdrom_file::region_presence::unknown);
	REQUIRE(track2_pregap.backing.empty());

	const cdrom_file::region &track2_program = track2.regions[1];
	REQUIRE(track2_program.kind == cdrom_file::region_kind::program);
	REQUIRE(track2_program.subcode == cdrom_file::region_presence::unknown);

	REQUIRE(track2_program.backing.size() == 1);
	REQUIRE(track2_program.backing[0].start.frame == 375);
	REQUIRE(track2_program.backing[0].frames.has_value());
	REQUIRE(*track2_program.backing[0].frames == 150);
	REQUIRE(track2_program.backing[0].captured.sector_data.has_value());
	REQUIRE(track2_program.backing[0].captured.sector_data->frame == 225);
	REQUIRE_FALSE(track2_program.backing[0].captured.main_channel.has_value());
	REQUIRE_FALSE(track2_program.backing[0].captured.subcode.has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					track2_pregap, { 225 }).has_value());

	auto sector =
			cdrom_file::backing_sector_position(
					track2_program, { 375 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 225);

	sector =
			cdrom_file::backing_sector_position(
					track2_program, { 376 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 226);

	sector =
			cdrom_file::backing_sector_position(
					track2_program, { 524 });
	REQUIRE(sector.has_value());
	REQUIRE(sector->frame == 374);

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					track2_program, { 525 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					track2_program, { 375 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_subcode_position(
					track2_program, { 375 }).has_value());

		auto track_sector =
			cdrom_file::backing_sector_position(
					track2, { 375 });
	REQUIRE(track_sector.has_value());
	REQUIRE(track_sector->frame == 225);

	track_sector =
			cdrom_file::backing_sector_position(
					track2, { 524 });
	REQUIRE(track_sector.has_value());
	REQUIRE(track_sector->frame == 374);

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					track2, { 225 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					track2, { 525 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					track2, { 375 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_subcode_position(
					track2, { 375 }).has_value());

		auto disc_sector =
			cdrom_file::backing_sector_position(
					disc, { 375 });
	REQUIRE(disc_sector.has_value());
	REQUIRE(disc_sector->frame == 225);

	disc_sector =
			cdrom_file::backing_sector_position(
					disc, { 524 });
	REQUIRE(disc_sector.has_value());
	REQUIRE(disc_sector->frame == 374);

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					disc, { 225 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_sector_position(
					disc, { 525 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_channel_position(
					disc, { 375 }).has_value());

	REQUIRE_FALSE(
			cdrom_file::backing_subcode_position(
					disc, { 375 }).has_value());
	
	uint8_t subcode[96];
	uint8_t q[12];

	// Logical frame 225 is already track 2's virtual INDEX 00 pregap.
	REQUIRE(cd.get_subcode_raw(225, subcode));

	cdrom_file::unpack_subcode_q(subcode, q);

	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x02);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x05);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM canonical disc model distinguishes virtual and stored pregaps", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-disc-model-pregap-test";
	const std::filesystem::path track1path = tempdir / "track1.bin";
	const std::filesystem::path track2path = tempdir / "track2.bin";
	const std::filesystem::path cuepath = tempdir / "stored.cue";
	const std::filesystem::path tocpath = tempdir / "virtual.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(track1path, std::ios::binary);
		const std::vector<char> data(3 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream bin(track2path, std::ios::binary);
		const std::vector<char> data(4 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"track2.bin\" BINARY\n"
				"  TRACK 01 AUDIO\n"
				"    INDEX 00 00:00:00\n"
				"    INDEX 01 00:02:00\n";
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO\n"
				"START 00:02:00\n"
				"DATAFILE \"track1.bin\" 00:00:00 00:03:00\n";
	}

		{
		cdrom_file cd(cuepath.string());
		const cdrom_file::disc disc = cd.get_disc();

		REQUIRE(disc.tracks.size() == 1);
		REQUIRE(disc.tracks[0].regions.size() >= 2);

		const auto &pregap = disc.tracks[0].regions[0];

		REQUIRE(pregap.kind == cdrom_file::region_kind::pregap);
		REQUIRE(pregap.frames.has_value());
		REQUIRE(*pregap.frames == 150);
		REQUIRE(pregap.main_data == cdrom_file::region_presence::captured);
		REQUIRE(pregap.start.frame == 0);

		REQUIRE(pregap.backing.size() == 1);
		REQUIRE(pregap.backing[0].start.frame == 0);
		REQUIRE(pregap.backing[0].frames.has_value());
		REQUIRE(*pregap.backing[0].frames == 150);
		REQUIRE(pregap.backing[0].captured.sector_data.has_value());
		REQUIRE(pregap.backing[0].captured.sector_data->frame == 0);
		REQUIRE_FALSE(pregap.backing[0].captured.main_channel.has_value());
		REQUIRE_FALSE(pregap.backing[0].captured.subcode.has_value());

		const auto &program = disc.tracks[0].regions[1];

		REQUIRE(program.kind == cdrom_file::region_kind::program);
		REQUIRE(program.start.frame == 150);

		REQUIRE(program.backing.size() == 1);
		REQUIRE(program.backing[0].start.frame == 150);
		REQUIRE(program.backing[0].frames.has_value());
		REQUIRE(*program.backing[0].frames == 150);
		REQUIRE(program.backing[0].captured.sector_data.has_value());
		REQUIRE(program.backing[0].captured.sector_data->frame == 150);
		REQUIRE_FALSE(program.backing[0].captured.main_channel.has_value());
		REQUIRE_FALSE(program.backing[0].captured.subcode.has_value());

		REQUIRE(disc.tracks[0].indexes.size() >= 1);
		REQUIRE(disc.tracks[0].indexes[0].number == 1);
		REQUIRE(disc.tracks[0].indexes[0].start.frame == 150);
	}

	{
		cdrom_file cd(tocpath.string());
		const cdrom_file::disc disc = cd.get_disc();

		REQUIRE(disc.tracks.size() == 1);
		REQUIRE(disc.tracks[0].regions.size() >= 2);

		const auto &pregap = disc.tracks[0].regions[0];

		REQUIRE(pregap.kind == cdrom_file::region_kind::pregap);
		REQUIRE(pregap.frames.has_value());
		REQUIRE(*pregap.frames == 150);
		REQUIRE(pregap.main_data == cdrom_file::region_presence::unknown);
		REQUIRE(pregap.start.frame == 0);
		REQUIRE(pregap.backing.empty());

		const auto &program = disc.tracks[0].regions[1];

		REQUIRE(program.kind == cdrom_file::region_kind::program);
		REQUIRE(program.start.frame == 150);

		REQUIRE(program.backing.size() == 1);
		REQUIRE(program.backing[0].start.frame == 150);
		REQUIRE(program.backing[0].frames.has_value());
		REQUIRE(*program.backing[0].frames == 75);
		REQUIRE(program.backing[0].captured.sector_data.has_value());
		REQUIRE(program.backing[0].captured.sector_data->frame == 0);
		REQUIRE_FALSE(program.backing[0].captured.main_channel.has_value());
		REQUIRE_FALSE(program.backing[0].captured.subcode.has_value());

		REQUIRE(disc.tracks[0].indexes.size() >= 1);
		REQUIRE(disc.tracks[0].indexes[0].number == 1);
		REQUIRE(disc.tracks[0].indexes[0].start.frame == 150);
	}

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM canonical sector reads do not double-apply stored pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-canonical-stored-pregap-read-test";
	const std::filesystem::path binpath = tempdir / "stored.bin";
	const std::filesystem::path cuepath = tempdir / "stored.cue";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(binpath, std::ios::binary);
		std::array<uint8_t, 2352> sector{};

		for (int frame = 0; frame < 300; frame++)
		{
			sector.fill(uint8_t(frame));
			bin.write(
					reinterpret_cast<const char *>(sector.data()),
					sector.size());
		}
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"stored.bin\" BINARY\n"
				"  TRACK 01 AUDIO\n"
				"    INDEX 00 00:00:00\n"
				"    INDEX 01 00:02:00\n";
	}

	cdrom_file cd(cuepath.string());
	std::array<uint8_t, 2352> sector{};

	// The first stored pregap sector is backing frame 0.
	REQUIRE(cd.read_data(
			0,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO));

	REQUIRE(sector[0] == 0);
	REQUIRE(sector[2351] == 0);

	// INDEX 01 is logical frame 150 and is backed by source frame 150.
	// It must not have the 150-frame pregap applied a second time.
	REQUIRE(cd.read_data(
			150,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO));

	REQUIRE(sector[0] == uint8_t(150));
	REQUIRE(sector[2351] == uint8_t(150));

	// Check another program-area frame so the mapping is demonstrably linear.
	REQUIRE(cd.read_data(
			151,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO));

	REQUIRE(sector[0] == uint8_t(151));
	REQUIRE(sector[2351] == uint8_t(151));

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM physical sector reads use canonical disc mapping", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-canonical-physical-read-test";
	const std::filesystem::path binpath = tempdir / "virtual.bin";
	const std::filesystem::path tocpath = tempdir / "virtual.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(binpath, std::ios::binary);
		std::array<uint8_t, 2352> sector{};

		for (int frame = 0; frame < 75; frame++)
		{
			sector.fill(uint8_t(frame + 1));
			bin.write(
					reinterpret_cast<const char *>(sector.data()),
					sector.size());
		}
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO\n"
				"START 00:02:00\n"
				"DATAFILE \"virtual.bin\" 00:00:00 00:01:00\n";
	}

	cdrom_file cd(tocpath.string());
	std::array<uint8_t, 2352> sector{};

	// Physical frame zero is backing storage for logical disc frame 150.
	// Physical reads must first resolve that canonical disc position.
	REQUIRE(cd.read_data(
			0,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO,
			true));

	REQUIRE(sector[0] == uint8_t(1));
	REQUIRE(sector[2351] == uint8_t(1));

	// The mapping remains linear within the captured program region.
	REQUIRE(cd.read_data(
			1,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO,
			true));

	REQUIRE(sector[0] == uint8_t(2));
	REQUIRE(sector[2351] == uint8_t(2));

	REQUIRE(cd.read_data(
			74,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO,
			true));

	REQUIRE(sector[0] == uint8_t(75));
	REQUIRE(sector[2351] == uint8_t(75));

	// There is no backing sector 75.
	REQUIRE_FALSE(cd.read_data(
			75,
			sector.data(),
			cdrom_file::CD_TRACK_AUDIO,
			true));

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM canonical subcode reads do not double-apply stored pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-canonical-stored-pregap-subcode-test";
	const std::filesystem::path binpath = tempdir / "stored.bin";
	const std::filesystem::path cuepath = tempdir / "stored.cue";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(binpath, std::ios::binary);
		std::array<uint8_t, 2352> sector{};

		for (int frame = 0; frame < 300; frame++)
		{
			uint8_t q[12] =
			{
				0x01,
				0x01,
				0x01,
				0x00, 0x00, uint8_t(frame),
				0x00,
				0x00, 0x00, uint8_t(frame),
				0x00, 0x00
			};

			const uint16_t crc = reference_q_crc(q);
			q[10] = uint8_t(crc >> 8);
			q[11] = uint8_t(crc);

			uint8_t subcode[96];
			interleave_q_raw(q, subcode);

			bin.write(
					reinterpret_cast<const char *>(sector.data()),
					sector.size());
			bin.write(
					reinterpret_cast<const char *>(subcode),
					sizeof(subcode));
		}
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"stored.bin\" BINARY\n"
				"  TRACK 01 AUDIO RW_RAW\n"
				"    INDEX 00 00:00:00\n"
				"    INDEX 01 00:02:00\n";
	}

	cdrom_file cd(cuepath.string());

	uint8_t subcode[96];
	uint8_t q[12];

	// The first stored pregap subcode is backing frame 0.
	REQUIRE(cd.get_subcode_raw(0, subcode));
	cdrom_file::unpack_subcode_q(subcode, q);
	REQUIRE(q[5] == uint8_t(0));
	REQUIRE(q[9] == uint8_t(0));
	require_valid_q_crc(q);

	// INDEX 01 is logical frame 150 and is backed by subcode frame 150.
	// It must not have the 150-frame pregap applied a second time.
	REQUIRE(cd.get_subcode_raw(150, subcode));
	cdrom_file::unpack_subcode_q(subcode, q);
	REQUIRE(q[5] == uint8_t(150));
	REQUIRE(q[9] == uint8_t(150));
	require_valid_q_crc(q);

	// Check another program-area frame so the mapping is demonstrably linear.
	REQUIRE(cd.get_subcode_raw(151, subcode));
	cdrom_file::unpack_subcode_q(subcode, q);
	REQUIRE(q[5] == uint8_t(151));
	REQUIRE(q[9] == uint8_t(151));
	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM Q subchannel later-track stored pregap", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-q-later-stored-pregap-test";
	const std::filesystem::path track1path = tempdir / "track1.bin";
	const std::filesystem::path track2path = tempdir / "track2.bin";
	const std::filesystem::path cuepath = tempdir / "later-stored.cue";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(track1path, std::ios::binary);
		const std::vector<char> data(3 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream bin(track2path, std::ios::binary);
		const std::vector<char> data(6 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"track1.bin\" BINARY\n"
				"  TRACK 01 AUDIO\n"
				"    INDEX 01 00:00:00\n"
				"FILE \"track2.bin\" BINARY\n"
				"  TRACK 02 AUDIO\n"
				"    INDEX 00 00:00:00\n"
				"    INDEX 01 00:02:00\n";
	}

	cdrom_file cd(cuepath.string());

		const cdrom_file::disc disc = cd.get_disc();

	auto position =
			cdrom_file::disc_position_from_sector_position(
					disc, { 225 });
	REQUIRE(position.has_value());
	REQUIRE(position->frame == 225);

	position =
			cdrom_file::disc_position_from_sector_position(
					disc, { 374 });
	REQUIRE(position.has_value());
	REQUIRE(position->frame == 374);

	position =
			cdrom_file::disc_position_from_sector_position(
					disc, { 375 });
	REQUIRE(position.has_value());
	REQUIRE(position->frame == 375);
	
	uint8_t q[12];

	// Track 2's physically stored pregap begins at physical frame 225.
	REQUIRE(cd.get_subcode_q(225, q, true));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x02);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x05);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	// Last stored pregap frame.
	REQUIRE(cd.get_subcode_q(374, q, true));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x00);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x01);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x06);
	REQUIRE(q[9] == 0x74);
	require_valid_q_crc(q);

	// Track 2 INDEX 01 begins after the stored pregap.
	REQUIRE(cd.get_subcode_q(375, q, true));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x01);
	REQUIRE(q[3] == 0x00);
	REQUIRE(q[4] == 0x00);
	REQUIRE(q[5] == 0x00);
	REQUIRE(q[7] == 0x00);
	REQUIRE(q[8] == 0x07);
	REQUIRE(q[9] == 0x00);
	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM Q subchannel packet classification", "[util][cdrom]")
{
	auto update_crc = [] (uint8_t *q)
	{
		const uint16_t crc = reference_q_crc(q);
		q[10] = uint8_t(crc >> 8);
		q[11] = uint8_t(crc);
	};

	SECTION("program-area position")
	{
		cdrom_file::q_position position;
		position.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		position.track = 1;
		position.index = 1;
		position.relative_frame = 0;
		position.absolute_frame = 150;

		uint8_t q[12];
		cdrom_file::encode_subcode_q(position, q);

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::position);
	}

	SECTION("lead-in TOC")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0xa0,
			0x00, 0x00, 0x00,
			0x00,
			0x01, 0x00, 0x00,
			0x00, 0x00
		};

		update_crc(q);

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::lead_in_toc);

		cdrom_file::q_position position;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, position));
	}

	SECTION("lead-out")
	{
		uint8_t q[12] =
		{
			0x01,
			0xaa,
			0x01,
			0x00, 0x00, 0x00,
			0x00,
			0x10, 0x00, 0x00,
			0x00, 0x00
		};

		update_crc(q);

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::lead_out);

		cdrom_file::q_position position;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, position));
	}

	SECTION("catalog")
	{
		uint8_t q[12] = {};
		q[0] = cdrom_file::CD_FLAG_ADR_CATALOG_CODE;

		update_crc(q);

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::catalog);
	}

	SECTION("ISRC")
	{
		uint8_t q[12] = {};
		q[0] = cdrom_file::CD_FLAG_ADR_ISRC_CODE;

		update_crc(q);

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::isrc);
	}

	SECTION("unknown ADR")
	{
		uint8_t q[12] = {};
		q[0] = 0x04;

		update_crc(q);

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::unknown);
	}

	SECTION("invalid CRC")
	{
		cdrom_file::q_position position;
		position.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		position.track = 1;
		position.index = 1;
		position.relative_frame = 0;
		position.absolute_frame = 150;

		uint8_t q[12];
		cdrom_file::encode_subcode_q(position, q);

		q[10] ^= 0x01;

		REQUIRE(
				cdrom_file::classify_subcode_q(q)
					== cdrom_file::q_type::invalid);
	}
}

TEST_CASE("CD-ROM Q subchannel lead-in TOC decode", "[util][cdrom]")
{
	auto update_crc = [] (uint8_t *q)
	{
		const uint16_t crc = reference_q_crc(q);
		q[10] = uint8_t(crc >> 8);
		q[11] = uint8_t(crc);
	};

	SECTION("A0 first track")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0xa0,
			0x00, 0x00, 0x00,
			0x00,
			0x01, 0x00, 0x00,
			0x00, 0x00
		};

		update_crc(q);

		cdrom_file::q_toc toc;
		REQUIRE(cdrom_file::decode_subcode_q_toc(q, toc));

		REQUIRE(toc.point == 0xa0);
		REQUIRE(toc.minute == 1);
		REQUIRE(toc.second == 0);
		REQUIRE(toc.frame == 0);
	}

	SECTION("A1 last track")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0xa1,
			0x00, 0x00, 0x00,
			0x00,
			0x12, 0x00, 0x00,
			0x00, 0x00
		};

		update_crc(q);

		cdrom_file::q_toc toc;
		REQUIRE(cdrom_file::decode_subcode_q_toc(q, toc));

		REQUIRE(toc.point == 0xa1);
		REQUIRE(toc.minute == 12);
		REQUIRE(toc.second == 0);
		REQUIRE(toc.frame == 0);
	}

	SECTION("A2 lead-out position")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0xa2,
			0x00, 0x00, 0x00,
			0x00,
			0x42, 0x17, 0x23,
			0x00, 0x00
		};

		update_crc(q);

		cdrom_file::q_toc toc;
		REQUIRE(cdrom_file::decode_subcode_q_toc(q, toc));

		REQUIRE(toc.point == 0xa2);
		REQUIRE(toc.minute == 42);
		REQUIRE(toc.second == 17);
		REQUIRE(toc.frame == 23);
	}

	SECTION("track point")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0x03,
			0x00, 0x00, 0x00,
			0x00,
			0x12, 0x34, 0x56,
			0x00, 0x00
		};

		update_crc(q);

		cdrom_file::q_toc toc;
		REQUIRE(cdrom_file::decode_subcode_q_toc(q, toc));

		REQUIRE(toc.point == 0x03);
		REQUIRE(toc.minute == 12);
		REQUIRE(toc.second == 34);
		REQUIRE(toc.frame == 56);
	}

	SECTION("invalid CRC")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0xa0,
			0x00, 0x00, 0x00,
			0x00,
			0x01, 0x00, 0x00,
			0x00, 0x00
		};

		update_crc(q);
		q[10] ^= 0x01;

		cdrom_file::q_toc toc;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q_toc(q, toc));
	}

	SECTION("invalid MSF")
	{
		uint8_t q[12] =
		{
			0x01,
			0x00,
			0xa2,
			0x00, 0x00, 0x00,
			0x00,
			0x12, 0x60, 0x00,
			0x00, 0x00
		};

		update_crc(q);

		cdrom_file::q_toc toc;
		REQUIRE_FALSE(cdrom_file::decode_subcode_q_toc(q, toc));
	}
}

TEST_CASE("CD-ROM Q subchannel lead-in TOC semantics", "[util][cdrom]")
{
	SECTION("A0 identifies first track and disc type")
	{
		cdrom_file::q_toc toc;
		toc.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		toc.point = 0xa0;
		toc.minute = 2;
		toc.second = 20;
		toc.frame = 0;

		cdrom_file::q_toc_semantics semantics;

		REQUIRE(cdrom_file::interpret_subcode_q_toc(toc, semantics));

		REQUIRE(semantics.kind == cdrom_file::q_toc_kind::first_track);
		REQUIRE(semantics.track.has_value());
		REQUIRE(*semantics.track == 2);
		REQUIRE(semantics.disc_type.has_value());
		REQUIRE(*semantics.disc_type == 0x20);
		REQUIRE_FALSE(semantics.start_frame.has_value());
	}

	SECTION("A1 identifies last track")
	{
		cdrom_file::q_toc toc;
		toc.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		toc.point = 0xa1;
		toc.minute = 12;
		toc.second = 0;
		toc.frame = 0;

		cdrom_file::q_toc_semantics semantics;

		REQUIRE(cdrom_file::interpret_subcode_q_toc(toc, semantics));

		REQUIRE(semantics.kind == cdrom_file::q_toc_kind::last_track);
		REQUIRE(semantics.track.has_value());
		REQUIRE(*semantics.track == 12);
		REQUIRE_FALSE(semantics.start_frame.has_value());
		REQUIRE_FALSE(semantics.disc_type.has_value());
	}

	SECTION("A2 identifies lead-out position")
	{
		cdrom_file::q_toc toc;
		toc.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		toc.point = 0xa2;
		toc.minute = 42;
		toc.second = 17;
		toc.frame = 23;

		cdrom_file::q_toc_semantics semantics;

		REQUIRE(cdrom_file::interpret_subcode_q_toc(toc, semantics));

		REQUIRE(semantics.kind == cdrom_file::q_toc_kind::lead_out);
		REQUIRE_FALSE(semantics.track.has_value());
		REQUIRE(semantics.start_frame.has_value());
		REQUIRE(
				*semantics.start_frame
					== (42U * 60U * 75U) + (17U * 75U) + 23U);
	}

	SECTION("track point identifies track start")
	{
		cdrom_file::q_toc toc;
		toc.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		toc.point = 0x03;
		toc.minute = 12;
		toc.second = 34;
		toc.frame = 56;

		cdrom_file::q_toc_semantics semantics;

		REQUIRE(cdrom_file::interpret_subcode_q_toc(toc, semantics));

		REQUIRE(semantics.kind == cdrom_file::q_toc_kind::track);
		REQUIRE(semantics.track.has_value());
		REQUIRE(*semantics.track == 3);
		REQUIRE(semantics.start_frame.has_value());
		REQUIRE(
				*semantics.start_frame
					== (12U * 60U * 75U) + (34U * 75U) + 56U);
	}

	SECTION("non-track special point is preserved")
	{
		cdrom_file::q_toc toc;
		toc.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		toc.point = 0xb0;
		toc.minute = 0;
		toc.second = 0;
		toc.frame = 0;

		cdrom_file::q_toc_semantics semantics;

		REQUIRE(cdrom_file::interpret_subcode_q_toc(toc, semantics));

		REQUIRE(semantics.kind == cdrom_file::q_toc_kind::special);
		REQUIRE(semantics.point == 0xb0);
		REQUIRE_FALSE(semantics.track.has_value());
		REQUIRE_FALSE(semantics.start_frame.has_value());
	}

	SECTION("invalid A1 is rejected")
	{
		cdrom_file::q_toc toc;
		toc.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		toc.point = 0xa1;
		toc.minute = 12;
		toc.second = 1;
		toc.frame = 0;

		cdrom_file::q_toc_semantics semantics;

		REQUIRE_FALSE(
				cdrom_file::interpret_subcode_q_toc(toc, semantics));
	}
}

TEST_CASE("CD-ROM Q TOC semantics update canonical disc model", "[util][cdrom]")
{
    cdrom_file::disc disc;

    cdrom_file::disc_session session;
    session.number = 2;
    session.first_track = 3;
    session.last_track = 4;
    session.program_start = { 1000 };
    session.lead_in = std::nullopt;
	session.lead_out = std::nullopt;
    disc.sessions.push_back(session);

    cdrom_file::disc_track track;
    track.number = 3;
    track.session = 2;
    track.type = cdrom_file::CD_TRACK_AUDIO;
    track.control_flags = 0;
	cdrom_file::region program;
    program.kind = cdrom_file::region_kind::program;
    program.start = { 1000 };
    program.frames = 500;
    program.main_data = cdrom_file::region_presence::captured;
    program.subcode = cdrom_file::region_presence::unknown;
    track.regions.push_back(program);
    track.indexes.push_back({ 1, { 1000 } });
    disc.tracks.push_back(track);

    SECTION("A0 confirms first track")
    {
        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::first_track;
        semantics.point = 0xa0;
        semantics.track = 3;
        semantics.start_frame = std::nullopt;
        semantics.disc_type = 0x20;

        REQUIRE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 2));

        REQUIRE(disc.sessions[0].first_track == 3);
        REQUIRE(disc.sessions[0].last_track == 4);
    }

    SECTION("A1 confirms last track")
    {
        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::last_track;
        semantics.point = 0xa1;
        semantics.track = 4;
        semantics.start_frame = std::nullopt;
        semantics.disc_type = std::nullopt;

        REQUIRE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 2));

        REQUIRE(disc.sessions[0].first_track == 3);
        REQUIRE(disc.sessions[0].last_track == 4);
    }

    SECTION("A2 updates lead-out")
    {
        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::lead_out;
        semantics.point = 0xa2;
        semantics.track = std::nullopt;
        semantics.start_frame = 12345;
        semantics.disc_type = std::nullopt;

        REQUIRE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 2));

        REQUIRE(disc.sessions[0].lead_out.has_value());
		REQUIRE(
       			disc.sessions[0].lead_out->kind
            		== cdrom_file::region_kind::lead_out);
		REQUIRE(disc.sessions[0].lead_out->start.frame == 12345);
		REQUIRE_FALSE(disc.sessions[0].lead_out->frames.has_value());
		REQUIRE(
      		  	disc.sessions[0].lead_out->main_data
           			== cdrom_file::region_presence::unknown);
		REQUIRE(
       		 	disc.sessions[0].lead_out->subcode
           		 	== cdrom_file::region_presence::unknown);
    }

    SECTION("track point updates INDEX 01")
    {
        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::track;
        semantics.point = 0x03;
        semantics.track = 3;
        semantics.start_frame = 2345;
        semantics.disc_type = std::nullopt;

        REQUIRE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 2));

        REQUIRE(disc.tracks[0].indexes.size() == 1);
        REQUIRE(disc.tracks[0].indexes[0].number == 1);
        REQUIRE(disc.tracks[0].indexes[0].start.frame == 2345);
		REQUIRE(disc.tracks[0].regions[0].start.frame == 2345);
        REQUIRE(disc.sessions[0].program_start.frame == 2345);
    }

    SECTION("track point can create missing INDEX 01")
    {
        disc.tracks[0].indexes.clear();

        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::track;
        semantics.point = 0x03;
        semantics.track = 3;
        semantics.start_frame = 2345;
        semantics.disc_type = std::nullopt;

        REQUIRE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 2));

        REQUIRE(disc.tracks[0].indexes.size() == 1);
        REQUIRE(disc.tracks[0].indexes[0].number == 1);
        REQUIRE(disc.tracks[0].indexes[0].start.frame == 2345);
		REQUIRE(disc.tracks[0].regions[0].start.frame == 2345);
        REQUIRE(disc.sessions[0].program_start.frame == 2345);
    }

    SECTION("special point does not alter canonical model")
    {
        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::special;
        semantics.point = 0xb0;
        semantics.track = std::nullopt;
        semantics.start_frame = std::nullopt;
        semantics.disc_type = std::nullopt;

        REQUIRE_FALSE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 2));

        REQUIRE(disc.sessions[0].first_track == 3);
        REQUIRE(disc.sessions[0].last_track == 4);
        REQUIRE(disc.tracks[0].indexes[0].start.frame == 1000);
    }

    SECTION("wrong session is rejected")
    {
        cdrom_file::q_toc_semantics semantics;
        semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
        semantics.kind = cdrom_file::q_toc_kind::first_track;
        semantics.point = 0xa0;
        semantics.track = 2;
        semantics.start_frame = std::nullopt;
        semantics.disc_type = std::nullopt;

        REQUIRE_FALSE(cdrom_file::apply_q_toc_semantics(
                semantics, disc, 1));

        REQUIRE(disc.sessions[0].first_track == 3);
    }
}

TEST_CASE("CD-ROM Q TOC accumulator", "[util][cdrom]")
{
	cdrom_file::q_toc_accumulator accumulator;

	auto make_first_track = [] (uint8_t track)
	{
		cdrom_file::q_toc_semantics semantics;
		semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		semantics.kind = cdrom_file::q_toc_kind::first_track;
		semantics.point = 0xa0;
		semantics.track = track;
		semantics.start_frame = std::nullopt;
		semantics.disc_type = 0x20;
		return semantics;
	};

	auto make_last_track = [] (uint8_t track)
	{
		cdrom_file::q_toc_semantics semantics;
		semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		semantics.kind = cdrom_file::q_toc_kind::last_track;
		semantics.point = 0xa1;
		semantics.track = track;
		semantics.start_frame = std::nullopt;
		semantics.disc_type = std::nullopt;
		return semantics;
	};

	auto make_lead_out = [] (uint32_t frame)
	{
		cdrom_file::q_toc_semantics semantics;
		semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		semantics.kind = cdrom_file::q_toc_kind::lead_out;
		semantics.point = 0xa2;
		semantics.track = std::nullopt;
		semantics.start_frame = frame;
		semantics.disc_type = std::nullopt;
		return semantics;
	};

	auto make_track = [] (uint8_t track, uint32_t frame)
	{
		cdrom_file::q_toc_semantics semantics;
		semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		semantics.kind = cdrom_file::q_toc_kind::track;
		semantics.point = track;
		semantics.track = track;
		semantics.start_frame = frame;
		semantics.disc_type = std::nullopt;
		return semantics;
	};

	SECTION("collects a complete TOC")
	{
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_first_track(3), accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_last_track(4), accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_lead_out(5000), accumulator));

		REQUIRE_FALSE(accumulator.complete());

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_track(3, 1000), accumulator));

		REQUIRE_FALSE(accumulator.complete());

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_track(4, 3000), accumulator));

		REQUIRE(accumulator.complete());
		REQUIRE_FALSE(accumulator.conflict);
		REQUIRE(accumulator.tracks.size() == 2);
	}

	SECTION("accepts repeated identical observations")
	{
		const auto first = make_first_track(3);
		const auto track = make_track(3, 1000);

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				first, accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				first, accumulator));

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				track, accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				track, accumulator));

		REQUIRE_FALSE(accumulator.conflict);
		REQUIRE(accumulator.tracks.size() == 1);
	}

	SECTION("rejects conflicting session observations")
	{
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_first_track(3), accumulator));

		REQUIRE_FALSE(cdrom_file::accumulate_q_toc_semantics(
				make_first_track(4), accumulator));

		REQUIRE(accumulator.conflict);
		REQUIRE_FALSE(accumulator.complete());
	}

	SECTION("rejects conflicting track observations")
	{
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_track(3, 1000), accumulator));

		REQUIRE_FALSE(cdrom_file::accumulate_q_toc_semantics(
				make_track(3, 1001), accumulator));

		REQUIRE(accumulator.conflict);
		REQUIRE_FALSE(accumulator.complete());
	}

	SECTION("requires every track point")
	{
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_first_track(3), accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_last_track(5), accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_lead_out(6000), accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_track(3, 1000), accumulator));
		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_track(5, 4000), accumulator));

		REQUIRE_FALSE(accumulator.complete());

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_track(4, 2500), accumulator));

		REQUIRE(accumulator.complete());
	}
}

TEST_CASE("CD-ROM raw Q TOC accumulation", "[util][cdrom]")
{
	auto make_q = [] (
			uint8_t point,
			uint8_t minute,
			uint8_t second,
			uint8_t frame)
	{
		std::array<uint8_t, 12> q =
		{
			0x01,
			0x00,
			point,
			0x00, 0x00, 0x00,
			0x00,
			minute,
			second,
			frame,
			0x00, 0x00
		};

		const uint16_t crc = reference_q_crc(q.data());
		q[10] = uint8_t(crc >> 8);
		q[11] = uint8_t(crc);

		return q;
	};

	SECTION("collects complete TOC from raw Q packets")
	{
		cdrom_file::q_toc_accumulator accumulator;

		const auto a0 = make_q(0xa0, 0x03, 0x20, 0x00);
		const auto a1 = make_q(0xa1, 0x04, 0x00, 0x00);
		const auto a2 = make_q(0xa2, 0x01, 0x06, 0x50);
		const auto track3 = make_q(0x03, 0x00, 0x13, 0x25);
		const auto track4 = make_q(0x04, 0x00, 0x40, 0x00);

		REQUIRE(cdrom_file::accumulate_subcode_q_toc(
				a0.data(), accumulator));
		REQUIRE(cdrom_file::accumulate_subcode_q_toc(
				a1.data(), accumulator));
		REQUIRE(cdrom_file::accumulate_subcode_q_toc(
				a2.data(), accumulator));
		REQUIRE(cdrom_file::accumulate_subcode_q_toc(
				track3.data(), accumulator));

		REQUIRE_FALSE(accumulator.complete());

		REQUIRE(cdrom_file::accumulate_subcode_q_toc(
				track4.data(), accumulator));

		REQUIRE(accumulator.complete());
		REQUIRE_FALSE(accumulator.conflict);

		REQUIRE(accumulator.first_track.has_value());
		REQUIRE(*accumulator.first_track == 3);

		REQUIRE(accumulator.last_track.has_value());
		REQUIRE(*accumulator.last_track == 4);

		REQUIRE(accumulator.disc_type.has_value());
		REQUIRE(*accumulator.disc_type == 0x20);

		REQUIRE(accumulator.lead_out_start_frame.has_value());
		REQUIRE(
				*accumulator.lead_out_start_frame
					== (1U * 60U * 75U) + (6U * 75U) + 50U);

		REQUIRE(accumulator.tracks.size() == 2);
		REQUIRE(accumulator.tracks[0].track.has_value());
		REQUIRE(*accumulator.tracks[0].track == 3);
		REQUIRE(accumulator.tracks[0].start_frame.has_value());
		REQUIRE(
				*accumulator.tracks[0].start_frame
					== (13U * 75U) + 25U);

		REQUIRE(accumulator.tracks[1].track.has_value());
		REQUIRE(*accumulator.tracks[1].track == 4);
		REQUIRE(accumulator.tracks[1].start_frame.has_value());
		REQUIRE(
				*accumulator.tracks[1].start_frame
					== 40U * 75U);
	}

	SECTION("rejects invalid CRC without changing accumulator")
	{
		cdrom_file::q_toc_accumulator accumulator;

		auto q = make_q(0xa0, 0x03, 0x20, 0x00);
		q[7] ^= 0x01;

		REQUIRE_FALSE(cdrom_file::accumulate_subcode_q_toc(
				q.data(), accumulator));

		REQUIRE_FALSE(accumulator.first_track.has_value());
		REQUIRE_FALSE(accumulator.conflict);
		REQUIRE(accumulator.tracks.empty());
	}

	SECTION("rejects non-TOC Q without changing accumulator")
	{
		cdrom_file::q_toc_accumulator accumulator;

		cdrom_file::q_position position;
		position.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		position.track = 1;
		position.index = 1;
		position.relative_frame = 0;
		position.absolute_frame = 150;

		uint8_t q[12];
		cdrom_file::encode_subcode_q(position, q);

		REQUIRE_FALSE(cdrom_file::accumulate_subcode_q_toc(
				q, accumulator));

		REQUIRE_FALSE(accumulator.first_track.has_value());
		REQUIRE_FALSE(accumulator.conflict);
		REQUIRE(accumulator.tracks.empty());
	}

	SECTION("propagates semantic conflicts")
	{
		cdrom_file::q_toc_accumulator accumulator;

		const auto first3 = make_q(0xa0, 0x03, 0x20, 0x00);
		const auto first4 = make_q(0xa0, 0x04, 0x20, 0x00);

		REQUIRE(cdrom_file::accumulate_subcode_q_toc(
				first3.data(), accumulator));

		REQUIRE_FALSE(cdrom_file::accumulate_subcode_q_toc(
				first4.data(), accumulator));

		REQUIRE(accumulator.conflict);
	}
}

TEST_CASE("CD-ROM Q TOC accumulator updates canonical disc model", "[util][cdrom]")
{
	auto make_semantics = [] (
			cdrom_file::q_toc_kind kind,
			uint8_t point,
			std::optional<uint8_t> track,
			std::optional<uint32_t> start_frame)
	{
		cdrom_file::q_toc_semantics semantics;
		semantics.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
		semantics.kind = kind;
		semantics.point = point;
		semantics.track = track;
		semantics.start_frame = start_frame;
		semantics.disc_type =
				(kind == cdrom_file::q_toc_kind::first_track)
						? std::optional<uint8_t>(0x20)
						: std::nullopt;
		return semantics;
	};

	auto make_disc = [] ()
	{
		cdrom_file::disc disc;

		cdrom_file::disc_session session;
		session.number = 2;
		session.first_track = 1;
		session.last_track = 2;
		session.program_start = { 900 };
		session.lead_in = std::nullopt;
		session.lead_out = cdrom_file::region{
        		cdrom_file::region_kind::lead_out,
        		{ 4500 },
        		750,
        		cdrom_file::region_presence::generated,
        		cdrom_file::region_presence::captured };
		disc.sessions.push_back(session);

		cdrom_file::disc_track track3;
		track3.number = 3;
		track3.session = 2;
		track3.type = cdrom_file::CD_TRACK_AUDIO;
		track3.control_flags = 0;
		track3.indexes.push_back({ 1, { 900 } });
		track3.regions.push_back({
				cdrom_file::region_kind::program,
				{ 900 },
				1000,
				cdrom_file::region_presence::captured,
				cdrom_file::region_presence::unknown });
		disc.tracks.push_back(track3);

		cdrom_file::disc_track track4;
		track4.number = 4;
		track4.session = 2;
		track4.type = cdrom_file::CD_TRACK_AUDIO;
		track4.control_flags = 0;
		track4.indexes.push_back({ 1, { 1900 } });
		track4.regions.push_back({
				cdrom_file::region_kind::program,
				{ 1900 },
				1000,
				cdrom_file::region_presence::captured,
				cdrom_file::region_presence::unknown });
		disc.tracks.push_back(track4);

		return disc;
	};

	auto make_complete_accumulator = [&] ()
	{
		cdrom_file::q_toc_accumulator accumulator;

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_semantics(
						cdrom_file::q_toc_kind::first_track,
						0xa0, 3, std::nullopt),
				accumulator));

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_semantics(
						cdrom_file::q_toc_kind::last_track,
						0xa1, 4, std::nullopt),
				accumulator));

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_semantics(
						cdrom_file::q_toc_kind::lead_out,
						0xa2, std::nullopt, 5000),
				accumulator));

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_semantics(
						cdrom_file::q_toc_kind::track,
						0x03, 3, 1000),
				accumulator));

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_semantics(
						cdrom_file::q_toc_kind::track,
						0x04, 4, 3000),
				accumulator));

		REQUIRE(accumulator.complete());

		return accumulator;
	};

	SECTION("applies complete coherent TOC")
	{
		cdrom_file::disc disc = make_disc();
		const auto accumulator = make_complete_accumulator();

		REQUIRE(cdrom_file::apply_q_toc_accumulator(
				accumulator, disc, 2));

		REQUIRE(disc.sessions[0].first_track == 3);
		REQUIRE(disc.sessions[0].last_track == 4);
		REQUIRE(disc.sessions[0].program_start.frame == 1000);
		REQUIRE(disc.sessions[0].lead_out.has_value());
		REQUIRE(disc.sessions[0].lead_out->start.frame == 5000);
		REQUIRE(disc.sessions[0].lead_out->frames.has_value());
		REQUIRE(*disc.sessions[0].lead_out->frames == 750);
		REQUIRE(
      			disc.sessions[0].lead_out->main_data
            		== cdrom_file::region_presence::generated);
		REQUIRE(
        		disc.sessions[0].lead_out->subcode
         			== cdrom_file::region_presence::captured);

		REQUIRE(disc.tracks[0].indexes[0].start.frame == 1000);
		REQUIRE(disc.tracks[0].regions[0].start.frame == 1000);

		REQUIRE(disc.tracks[1].indexes[0].start.frame == 3000);
		REQUIRE(disc.tracks[1].regions[0].start.frame == 3000);
	}

	SECTION("rejects incomplete accumulator without mutation")
	{
		cdrom_file::disc disc = make_disc();
		cdrom_file::q_toc_accumulator accumulator;

		REQUIRE(cdrom_file::accumulate_q_toc_semantics(
				make_semantics(
						cdrom_file::q_toc_kind::first_track,
						0xa0, 3, std::nullopt),
				accumulator));

		REQUIRE_FALSE(cdrom_file::apply_q_toc_accumulator(
				accumulator, disc, 2));

		REQUIRE(disc.sessions[0].first_track == 1);
		REQUIRE(disc.sessions[0].last_track == 2);
		REQUIRE(disc.sessions[0].program_start.frame == 900);
		REQUIRE(disc.sessions[0].lead_out.has_value());
		REQUIRE(disc.sessions[0].lead_out->start.frame == 4500);
		REQUIRE(disc.sessions[0].lead_out->frames.has_value());
		REQUIRE(*disc.sessions[0].lead_out->frames == 750);
	}

	SECTION("rejects wrong session without mutation")
	{
		cdrom_file::disc disc = make_disc();
		const auto accumulator = make_complete_accumulator();

		REQUIRE_FALSE(cdrom_file::apply_q_toc_accumulator(
				accumulator, disc, 1));

		REQUIRE(disc.sessions[0].first_track == 1);
		REQUIRE(disc.sessions[0].last_track == 2);
		REQUIRE(disc.sessions[0].program_start.frame == 900);
		REQUIRE(disc.sessions[0].lead_out.has_value());
		REQUIRE(disc.sessions[0].lead_out->start.frame == 4500);
		REQUIRE(disc.sessions[0].lead_out->frames.has_value());
		REQUIRE(*disc.sessions[0].lead_out->frames == 750);
		REQUIRE(disc.tracks[0].indexes[0].start.frame == 900);
		REQUIRE(disc.tracks[1].indexes[0].start.frame == 1900);
	}

	SECTION("failed application is atomic")
	{
		cdrom_file::disc disc = make_disc();
		const auto accumulator = make_complete_accumulator();

		// Make the accumulator structurally complete but make the target
		// canonical model unable to accept track 4.
		disc.tracks.pop_back();

		REQUIRE_FALSE(cdrom_file::apply_q_toc_accumulator(
				accumulator, disc, 2));

		REQUIRE(disc.sessions[0].first_track == 1);
		REQUIRE(disc.sessions[0].last_track == 2);
		REQUIRE(disc.sessions[0].program_start.frame == 900);
		REQUIRE(disc.sessions[0].lead_out.has_value());
		REQUIRE(disc.sessions[0].lead_out->start.frame == 4500);
		REQUIRE(disc.sessions[0].lead_out->frames.has_value());
		REQUIRE(*disc.sessions[0].lead_out->frames == 750);
		REQUIRE(disc.tracks[0].indexes[0].start.frame == 900);
		REQUIRE(disc.tracks[0].regions[0].start.frame == 900);
	}
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

TEST_CASE("CD-ROM stored Q later-track index lookup", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-stored-q-later-index-test";
	const std::filesystem::path track1path = tempdir / "track1.bin";
	const std::filesystem::path track2path = tempdir / "track2.bin";
	const std::filesystem::path tocpath = tempdir / "stored-q-later-index.toc";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(track1path, std::ios::binary);
		const std::vector<char> data(3 * 75 * 2352, 0);
		bin.write(data.data(), data.size());
	}

	{
		std::ofstream bin(track2path, std::ios::binary);
		const std::vector<char> audio(2352, 0);

		for (int frame = 0; frame < 6 * 75; frame++)
		{
			uint8_t q[12] =
			{
				0x01,       // ADR=1, CONTROL=0
				0x02,       // track 2
				uint8_t(frame < 3 * 75 ? 0x01 : 0x02),
				0x00, 0x00, 0x00,
				0x00,
				0x00, 0x00, 0x00,
				0x00, 0x00
			};

			const uint16_t crc = reference_q_crc(q);
			q[10] = uint8_t(crc >> 8);
			q[11] = uint8_t(crc);

			uint8_t raw_subcode[96];
			interleave_q_raw(q, raw_subcode);

			bin.write(audio.data(), audio.size());
			bin.write(
					reinterpret_cast<const char *>(raw_subcode),
					sizeof(raw_subcode));
		}
	}

	{
		std::ofstream toc(tocpath);
		toc <<
				"CD_DA\n"
				"\n"
				"TRACK AUDIO\n"
				"DATAFILE \"track1.bin\" 00:03:00\n"
				"\n"
				"TRACK AUDIO RW_RAW\n"
				"DATAFILE \"track2.bin\" 00:00:00 00:06:00\n";
	}

	cdrom_file cd(tocpath.string());

	// Track 2 begins at logical frame 225.  Its stored Q changes
	// from INDEX 01 to INDEX 02 three seconds later.
	REQUIRE(cd.get_track_index(224) == 1);
	REQUIRE(cd.get_track_index(225) == 1);
	REQUIRE(cd.get_track_index(449) == 1);
	REQUIRE(cd.get_track_index(450) == 2);
	REQUIRE(cd.get_track_index(451) == 2);

	uint8_t q[12];

	REQUIRE(cd.get_subcode_q(449, q));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x01);
	require_valid_q_crc(q);

	REQUIRE(cd.get_subcode_q(450, q));
	REQUIRE(q[1] == 0x02);
	REQUIRE(q[2] == 0x02);
	require_valid_q_crc(q);

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM invalid stored Q falls back to semantic index", "[util][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-cdrom-invalid-stored-q-index-test";
	const std::filesystem::path binpath = tempdir / "track.bin";
	const std::filesystem::path cuepath = tempdir / "track.cue";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	{
		std::ofstream bin(binpath, std::ios::binary);
		const std::vector<char> audio(2352, 0);

		for (int frame = 0; frame < 6 * 75; frame++)
		{
			uint8_t q[12] =
			{
				0x01,
				0x01,
				0x07,       // deliberately conflicts with semantic INDEX 01/02
				0x00, 0x00, 0x00,
				0x00,
				0x00, 0x00, 0x00,
				0x00, 0x00
			};

			// Deliberately leave the CRC invalid.
			uint8_t raw_subcode[96];
			interleave_q_raw(q, raw_subcode);

			bin.write(audio.data(), audio.size());
			bin.write(
					reinterpret_cast<const char *>(raw_subcode),
					sizeof(raw_subcode));
		}
	}

	{
		std::ofstream cue(cuepath);
		cue <<
				"FILE \"track.bin\" BINARY\n"
				"  TRACK 01 AUDIO RW_RAW\n"
				"    INDEX 01 00:00:00\n"
				"    INDEX 02 00:03:00\n";
	}

	cdrom_file cd(cuepath.string());

	const cdrom_file::toc &parsed_toc = cd.get_toc();

	// Make sure this fixture really exercises captured raw subcode.
	REQUIRE(parsed_toc.tracks[0].subtype == cdrom_file::CD_SUB_RAW);
	REQUIRE(parsed_toc.tracks[0].subsize == 96);
	REQUIRE(parsed_toc.tracks[0].idx[2] == 225);

	// The stored Q claims INDEX 07, but its CRC is invalid.  Runtime index
	// lookup must reject it and fall back to the descriptor's INDEX 01/02.
	REQUIRE(cd.get_track_index(224) == 1);
	REQUIRE(cd.get_track_index(225) == 2);
	REQUIRE(cd.get_track_index(449) == 2);

	// Captured Q itself is still returned unchanged; validation happens
	// when it is decoded for semantic index lookup.
	uint8_t q[12];
	REQUIRE(cd.get_subcode_q(225, q));
	REQUIRE(q[2] == 0x07);

	cdrom_file::q_position position;
	REQUIRE_FALSE(cdrom_file::decode_subcode_q(q, position));

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD-ROM Q position generation", "[util][cdrom]")
{
	cdrom_file::disc_track track;
	track.number = 1;
	track.session = 1;
	track.type = cdrom_file::CD_TRACK_AUDIO;
	track.control_flags = 0;

	track.regions.push_back(
			{
				cdrom_file::region_kind::pregap,
				{ 0 },
				150,
				cdrom_file::region_presence::unknown,
				cdrom_file::region_presence::unknown,
				{}
			});

	track.regions.push_back(
			{
				cdrom_file::region_kind::program,
				{ 150 },
				600,
				cdrom_file::region_presence::captured,
				cdrom_file::region_presence::unknown,
				{}
			});

	track.indexes.push_back({ 1, { 150 } });

	// Deliberately skip INDEX 02. Additional indexes do not need to be
	// numerically contiguous.
	track.indexes.push_back({ 3, { 375 } });
	track.indexes.push_back({ 5, { 600 } });

	cdrom_file::q_position position;

	SECTION("pregap position")
	{
		REQUIRE(cdrom_file::make_subcode_q_position(
				track,
				{ 0 },
				-150,
				0,
				position));

		REQUIRE(position.track == 1);
		REQUIRE(position.index == 0);
		REQUIRE(position.relative_frame == 150);
		REQUIRE(position.absolute_frame == 0);
	}

	SECTION("index 1")
	{
		REQUIRE(cdrom_file::make_subcode_q_position(
				track,
				{ 374 },
				224,
				374,
				position));

		REQUIRE(position.track == 1);
		REQUIRE(position.index == 1);
		REQUIRE(position.relative_frame == 224);
		REQUIRE(position.absolute_frame == 374);
	}

	SECTION("skipped index 3")
	{
		REQUIRE(cdrom_file::make_subcode_q_position(
				track,
				{ 375 },
				225,
				375,
				position));

		REQUIRE(position.track == 1);
		REQUIRE(position.index == 3);
		REQUIRE(position.relative_frame == 225);
		REQUIRE(position.absolute_frame == 375);
	}

	SECTION("index 5")
	{
		REQUIRE(cdrom_file::make_subcode_q_position(
				track,
				{ 600 },
				450,
				600,
				position));

		REQUIRE(position.track == 1);
		REQUIRE(position.index == 5);
		REQUIRE(position.relative_frame == 450);
		REQUIRE(position.absolute_frame == 600);
	}

	SECTION("negative absolute position")
	{
		REQUIRE_FALSE(cdrom_file::make_subcode_q_position(
				track,
				{ 150 },
				0,
				-1,
				position));
	}
}
