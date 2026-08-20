#include "catch.hpp"

#include "cdrom.h"
#include "chd.h"

#include <filesystem>
#include <vector>


static void make_position_q(
		uint8_t index,
		uint32_t relative_frame,
		uint32_t absolute_frame,
		uint8_t *subcode,
		bool corrupt_crc = false,
		uint8_t track_number = 1)
{
	cdrom_file::q_position position;
	position.adr_control = cdrom_file::CD_FLAG_ADR_START_TIME << 4;
	position.track = track_number;
	position.index = index;
	position.relative_frame = relative_frame;
	position.absolute_frame = absolute_frame;

	uint8_t q[12];
	cdrom_file::encode_subcode_q(position, q);

	if (corrupt_crc)
		q[10] ^= 0x01;

	cdrom_file::pack_subcode_q(q, subcode);
}

TEST_CASE("CD CHD canonical disc model reconstructs sessions", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-session-model-test";
	const std::filesystem::path chdpath = tempdir / "sessions.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t track_frames = 4;
	constexpr uint32_t track_count = 4;
	constexpr uint32_t frame_count = track_frames * track_count;
	constexpr uint32_t hunk_bytes =
			cdrom_file::FRAME_SIZE * cdrom_file::FRAMES_PER_HUNK;

	const chd_codec_type compression[4] =
	{
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE
	};

	chd_file chd;

	REQUIRE_FALSE(chd.create(
			chdpath.string(),
			uint64_t(frame_count) * cdrom_file::FRAME_SIZE,
			hunk_bytes,
			cdrom_file::FRAME_SIZE,
			compression));

	cdrom_file::toc toc{};
	toc.numtrks = track_count;
	toc.numsessions = 2;
	toc.flags = cdrom_file::CD_FLAG_MULTISESSION;

	for (uint32_t tracknum = 0; tracknum < track_count; tracknum++)
	{
		cdrom_file::track_info &track = toc.tracks[tracknum];

		track.trktype = cdrom_file::CD_TRACK_AUDIO;
		track.subtype = cdrom_file::CD_SUB_NONE;
		track.datasize = cdrom_file::MAX_SECTOR_DATA;
		track.subsize = 0;
		track.frames = track_frames;
		track.extraframes = 0;
		track.pregap = 0;
		track.postgap = 0;
		track.pgtype = cdrom_file::CD_TRACK_AUDIO;
		track.pgsub = cdrom_file::CD_SUB_NONE;
		track.pgdatasize = 0;
		track.pgsubsize = 0;
		track.control_flags = 0;
		track.session = (tracknum < 2) ? 0 : 1;

		std::fill(std::begin(track.idx), std::end(track.idx), -1);
	}

	REQUIRE_FALSE(cdrom_file::write_metadata(&chd, toc));

	std::vector<uint8_t> frame(cdrom_file::FRAME_SIZE, 0);

	for (uint32_t framenum = 0; framenum < frame_count; framenum++)
		REQUIRE_FALSE(chd.write_units(framenum, frame.data()));

	cdrom_file cd(&chd);
	const cdrom_file::disc disc = cd.get_disc();

	REQUIRE(disc.sessions.size() == 2);
	REQUIRE(disc.tracks.size() == 4);

	REQUIRE(disc.sessions[0].number == 1);
	REQUIRE(disc.sessions[0].first_track == 1);
	REQUIRE(disc.sessions[0].last_track == 2);
	REQUIRE(disc.sessions[0].program_start_frame == 0);
	REQUIRE_FALSE(disc.sessions[0].lead_in_start_frame.has_value());
	REQUIRE_FALSE(disc.sessions[0].lead_out_start_frame.has_value());

	REQUIRE(disc.sessions[1].number == 2);
	REQUIRE(disc.sessions[1].first_track == 3);
	REQUIRE(disc.sessions[1].last_track == 4);
	REQUIRE(disc.sessions[1].program_start_frame == 8);
	REQUIRE_FALSE(disc.sessions[1].lead_in_start_frame.has_value());
	REQUIRE_FALSE(disc.sessions[1].lead_out_start_frame.has_value());

	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD CHD reconstructs track indexes from stored Q", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-q-index-test";
	const std::filesystem::path chdpath = tempdir / "indexes.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t frame_count = 18;
	constexpr uint32_t hunk_bytes =
			cdrom_file::FRAME_SIZE * cdrom_file::FRAMES_PER_HUNK;

	const chd_codec_type compression[4] =
	{
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE
	};

	chd_file chd;

	REQUIRE_FALSE(chd.create(
			chdpath.string(),
			uint64_t(frame_count) * cdrom_file::FRAME_SIZE,
			hunk_bytes,
			cdrom_file::FRAME_SIZE,
			compression));

	cdrom_file::toc toc{};
	toc.numtrks = 1;
	toc.numsessions = 1;

	cdrom_file::track_info &track = toc.tracks[0];
	track.trktype = cdrom_file::CD_TRACK_AUDIO;
	track.subtype = cdrom_file::CD_SUB_RAW;
	track.datasize = cdrom_file::MAX_SECTOR_DATA;
	track.subsize = cdrom_file::MAX_SUBCODE_DATA;
	track.frames = frame_count;
	track.pregap = 0;
	track.postgap = 0;
	track.pgtype = cdrom_file::CD_TRACK_AUDIO;
	track.pgsub = cdrom_file::CD_SUB_NONE;
	track.pgdatasize = 0;
	track.pgsubsize = 0;
	track.control_flags = 0;
	track.session = 0;

	std::fill(std::begin(track.idx), std::end(track.idx), -1);

	REQUIRE_FALSE(cdrom_file::write_metadata(&chd, toc));

	std::vector<uint8_t> frame(cdrom_file::FRAME_SIZE, 0);

	auto write_frame =
		[&chd, &frame](
				uint32_t framenum,
				uint8_t index,
				bool corrupt_crc = false,
				uint8_t track_number = 1)
		{
			std::fill(frame.begin(), frame.end(), 0);

			make_position_q(
					index,
					framenum,
					150 + framenum,
					frame.data() + cdrom_file::MAX_SECTOR_DATA,
					corrupt_crc,
					track_number);

			REQUIRE_FALSE(chd.write_units(framenum, frame.data()));
		};

	// Establish INDEX 01.
	write_frame(0, 1);
	write_frame(1, 1);

	// A one-frame INDEX 02 glitch must not be reconstructed.
	write_frame(2, 2);
	write_frame(3, 1);

	// Two consecutive INDEX 02 frames confirm the transition.
	write_frame(4, 2);
	write_frame(5, 2);

	// Likewise for INDEX 03.
	write_frame(6, 3);
	write_frame(7, 3);

	// A backwards transition remains visible in raw Q but must not alter
	// the monotonically ordered semantic index table.
	write_frame(8, 2);
	write_frame(9, 2);

	// One invalid-CRC INDEX 04 followed by one valid INDEX 04 is not
	// enough to confirm an INDEX 04 transition.
	write_frame(10, 4, true);
	write_frame(11, 4);

	// An invalid Q frame must also break a pending transition.  Two valid
	// INDEX 04 observations separated by invalid Q are not consecutive.
	write_frame(12, 4, true);
	write_frame(13, 4);

	// Separate this case from the preceding interrupted transition.
	write_frame(14, 3, true);

	// A valid position-Q frame for the wrong track must also interrupt a
	// pending transition.
	write_frame(15, 4);
	write_frame(16, 4, false, 2);
	write_frame(17, 4);

	// The CHD constructor must derive its TOC from CHD metadata and
	// reconstruct additional indexes from stored position Q.
	cdrom_file cd(&chd);

	const cdrom_file::toc &after = cd.get_toc();

	REQUIRE(after.tracks[0].idx[2] == 4);
	REQUIRE(after.tracks[0].idx[3] == 6);
	REQUIRE(after.tracks[0].idx[4] == -1);

	// Runtime lookup still follows valid captured Q directly, including
	// unusual/backwards Q that cannot be represented in the semantic table.
	REQUIRE(cd.get_track_index(4) == 2);
	REQUIRE(cd.get_track_index(6) == 3);
	REQUIRE(cd.get_track_index(8) == 2);

	// The invalid-CRC INDEX 04 sector falls back to reconstructed semantics.
	REQUIRE(cd.get_track_index(10) == 3);

	// The following valid captured INDEX 04 is authoritative at runtime even
	// though it was not sufficiently confirmed for semantic reconstruction.
	REQUIRE(cd.get_track_index(11) == 4);

	// The final valid INDEX 04 is authoritative at runtime, even though the
	// interrupted transition must not be reconstructed semantically.
	REQUIRE(cd.get_track_index(13) == 4);

	// The valid INDEX 04 frames on either side of the wrong-track Q are still
	// authoritative individually at runtime.
	REQUIRE(cd.get_track_index(15) == 4);
	REQUIRE(cd.get_track_index(17) == 4);

	// Wrong-track Q is rejected for semantic lookup, so this sector falls back
	// to the reconstructed semantic table.
	REQUIRE(cd.get_track_index(16) == 3);
	
	chd.close();
	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD CHD reconstructs indexes relative to stored pregap", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-q-pregap-test";
	const std::filesystem::path chdpath = tempdir / "pregap.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t frame_count = 8;
	constexpr uint32_t pregap = 2;
	constexpr uint32_t hunk_bytes =
			cdrom_file::FRAME_SIZE * cdrom_file::FRAMES_PER_HUNK;

	const chd_codec_type compression[4] =
	{
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE
	};

	chd_file chd;

	REQUIRE_FALSE(chd.create(
			chdpath.string(),
			uint64_t(frame_count) * cdrom_file::FRAME_SIZE,
			hunk_bytes,
			cdrom_file::FRAME_SIZE,
			compression));

	cdrom_file::toc toc{};
	toc.numtrks = 1;
	toc.numsessions = 1;

	cdrom_file::track_info &track = toc.tracks[0];
	track.trktype = cdrom_file::CD_TRACK_AUDIO;
	track.subtype = cdrom_file::CD_SUB_RAW;
	track.datasize = cdrom_file::MAX_SECTOR_DATA;
	track.subsize = cdrom_file::MAX_SUBCODE_DATA;
	track.frames = frame_count;
	track.pregap = pregap;
	track.postgap = 0;

	// Non-zero pgdatasize means the pregap sectors are physically stored.
	track.pgtype = cdrom_file::CD_TRACK_AUDIO;
	track.pgsub = cdrom_file::CD_SUB_RAW;
	track.pgdatasize = cdrom_file::MAX_SECTOR_DATA;
	track.pgsubsize = cdrom_file::MAX_SUBCODE_DATA;

	track.control_flags = 0;
	track.session = 0;

	std::fill(std::begin(track.idx), std::end(track.idx), -1);

	REQUIRE_FALSE(cdrom_file::write_metadata(&chd, toc));

	std::vector<uint8_t> frame(cdrom_file::FRAME_SIZE, 0);

	auto write_frame =
			[&chd, &frame](
					uint32_t framenum,
					uint8_t index,
					uint32_t relative_frame)
			{
				std::fill(frame.begin(), frame.end(), 0);

				make_position_q(
						index,
						relative_frame,
						150 + framenum - 2,
						frame.data() + cdrom_file::MAX_SECTOR_DATA);

				REQUIRE_FALSE(chd.write_units(framenum, frame.data()));
			};

	// Two physically stored pregap sectors.
	write_frame(0, 0, 2);
	write_frame(1, 0, 1);

	// INDEX 01 begins at physical frame 2.
	write_frame(2, 1, 0);
	write_frame(3, 1, 1);
	write_frame(4, 1, 2);

	// INDEX 02 begins three frames after INDEX 01.
	write_frame(5, 2, 3);
	write_frame(6, 2, 4);
	write_frame(7, 2, 5);

		cdrom_file cd(&chd);

	// The physical transition occurs at frame 5, but idx[] coordinates are
	// relative to INDEX 01 at physical frame 2.
	REQUIRE(cd.get_toc().tracks[0].idx[2] == 3);

	chd.close();
	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD CHD reconstructs indexes on later track", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-q-later-track-test";
	const std::filesystem::path chdpath = tempdir / "later-track.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t track1_frames = 4;
	constexpr uint32_t track2_frames = 8;
	constexpr uint32_t frame_count = track1_frames + track2_frames;
	constexpr uint32_t hunk_bytes =
			cdrom_file::FRAME_SIZE * cdrom_file::FRAMES_PER_HUNK;

	const chd_codec_type compression[4] =
	{
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE
	};

	chd_file chd;

	REQUIRE_FALSE(chd.create(
			chdpath.string(),
			uint64_t(frame_count) * cdrom_file::FRAME_SIZE,
			hunk_bytes,
			cdrom_file::FRAME_SIZE,
			compression));

	cdrom_file::toc toc{};
	toc.numtrks = 2;
	toc.numsessions = 1;

	for (int tracknum = 0; tracknum < 2; tracknum++)
	{
		cdrom_file::track_info &track = toc.tracks[tracknum];

		track.trktype = cdrom_file::CD_TRACK_AUDIO;
		track.subtype = cdrom_file::CD_SUB_RAW;
		track.datasize = cdrom_file::MAX_SECTOR_DATA;
		track.subsize = cdrom_file::MAX_SUBCODE_DATA;
		track.frames = (tracknum == 0) ? track1_frames : track2_frames;
		track.pregap = 0;
		track.postgap = 0;
		track.pgtype = cdrom_file::CD_TRACK_AUDIO;
		track.pgsub = cdrom_file::CD_SUB_NONE;
		track.pgdatasize = 0;
		track.pgsubsize = 0;
		track.control_flags = 0;
		track.session = 0;

		std::fill(std::begin(track.idx), std::end(track.idx), -1);
	}

	REQUIRE_FALSE(cdrom_file::write_metadata(&chd, toc));

	std::vector<uint8_t> frame(cdrom_file::FRAME_SIZE, 0);

	auto write_frame =
			[&chd, &frame](
					uint32_t physical_frame,
					uint8_t track_number,
					uint8_t index,
					uint32_t relative_frame)
			{
				std::fill(frame.begin(), frame.end(), 0);

				make_position_q(
						index,
						relative_frame,
						150 + physical_frame,
						frame.data() + cdrom_file::MAX_SECTOR_DATA,
						false,
						track_number);

				REQUIRE_FALSE(chd.write_units(physical_frame, frame.data()));
			};

	// Track 1 occupies physical frames 0-3 and remains entirely INDEX 01.
	for (uint32_t frame = 0; frame < track1_frames; frame++)
		write_frame(frame, 1, 1, frame);

	// Track 2 begins at physical frame 4.
	// Its first three sectors are INDEX 01.
	write_frame(4, 2, 1, 0);
	write_frame(5, 2, 1, 1);
	write_frame(6, 2, 1, 2);

	// INDEX 02 begins at Track 2-relative frame 3, which is physical
	// disc frame 7.  Two consecutive Q frames confirm the transition.
	write_frame(7, 2, 2, 3);
	write_frame(8, 2, 2, 4);
	write_frame(9, 2, 2, 5);
	write_frame(10, 2, 2, 6);
	write_frame(11, 2, 2, 7);

	cdrom_file cd(&chd);

	// Confirm the track boundary itself is where the fixture expects it.
	REQUIRE(cd.get_track(3) == 0);
	REQUIRE(cd.get_track(4) == 1);

	const cdrom_file::toc &after = cd.get_toc();

	// Track 1 must remain untouched.
	REQUIRE(after.tracks[0].idx[2] == -1);

	// The transition occurs at absolute/physical frame 7, but idx[] must
	// store the position relative to Track 2 INDEX 01 at frame 4.
	REQUIRE(after.tracks[1].idx[2] == 3);

	// Runtime lookup agrees across the Track 2 INDEX 01/02 boundary.
	REQUIRE(cd.get_track_index(6) == 1);
	REQUIRE(cd.get_track_index(7) == 2);
	REQUIRE(cd.get_track_index(8) == 2);

	chd.close();
	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD CHD reconstructs later-track index relative to stored pregap", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-q-later-pregap-test";
	const std::filesystem::path chdpath = tempdir / "later-pregap.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t track1_frames = 4;
	constexpr uint32_t track2_frames = 10;
	constexpr uint32_t track2_pregap = 2;
	constexpr uint32_t frame_count = track1_frames + track2_frames;
	constexpr uint32_t hunk_bytes =
			cdrom_file::FRAME_SIZE * cdrom_file::FRAMES_PER_HUNK;

	const chd_codec_type compression[4] =
	{
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE
	};

	chd_file chd;

	REQUIRE_FALSE(chd.create(
			chdpath.string(),
			uint64_t(frame_count) * cdrom_file::FRAME_SIZE,
			hunk_bytes,
			cdrom_file::FRAME_SIZE,
			compression));

	cdrom_file::toc toc{};
	toc.numtrks = 2;
	toc.numsessions = 1;

	for (int tracknum = 0; tracknum < 2; tracknum++)
	{
		cdrom_file::track_info &track = toc.tracks[tracknum];

		track.trktype = cdrom_file::CD_TRACK_AUDIO;
		track.subtype = cdrom_file::CD_SUB_RAW;
		track.datasize = cdrom_file::MAX_SECTOR_DATA;
		track.subsize = cdrom_file::MAX_SUBCODE_DATA;
		track.frames = (tracknum == 0) ? track1_frames : track2_frames;
		track.pregap = (tracknum == 0) ? 0 : track2_pregap;
		track.postgap = 0;
		track.control_flags = 0;
		track.session = 0;

		if (tracknum == 0)
		{
			track.pgtype = cdrom_file::CD_TRACK_AUDIO;
			track.pgsub = cdrom_file::CD_SUB_NONE;
			track.pgdatasize = 0;
			track.pgsubsize = 0;
		}
		else
		{
			// Track 2's pregap is physically stored in the CHD.
			track.pgtype = cdrom_file::CD_TRACK_AUDIO;
			track.pgsub = cdrom_file::CD_SUB_RAW;
			track.pgdatasize = cdrom_file::MAX_SECTOR_DATA;
			track.pgsubsize = cdrom_file::MAX_SUBCODE_DATA;
		}

		std::fill(std::begin(track.idx), std::end(track.idx), -1);
	}

	REQUIRE_FALSE(cdrom_file::write_metadata(&chd, toc));

	std::vector<uint8_t> frame(cdrom_file::FRAME_SIZE, 0);

	auto write_frame =
			[&chd, &frame](
					uint32_t physical_frame,
					uint8_t track_number,
					uint8_t index,
					uint32_t relative_frame,
					uint32_t absolute_frame)
			{
				std::fill(frame.begin(), frame.end(), 0);

				make_position_q(
						index,
						relative_frame,
						absolute_frame,
						frame.data() + cdrom_file::MAX_SECTOR_DATA,
						false,
						track_number);

				REQUIRE_FALSE(chd.write_units(physical_frame, frame.data()));
			};

	// Track 1 occupies physical frames 0-3.
	for (uint32_t frame = 0; frame < track1_frames; frame++)
		write_frame(frame, 1, 1, frame, 150 + frame);

	// Track 2 begins physically at frame 4 with a two-frame stored pregap.
	// Its INDEX 01 therefore begins at physical frame 6.
	write_frame(4, 2, 0, 2, 154);
	write_frame(5, 2, 0, 1, 155);

	write_frame(6, 2, 1, 0, 156);
	write_frame(7, 2, 1, 1, 157);
	write_frame(8, 2, 1, 2, 158);

	// INDEX 02 begins three frames after Track 2 INDEX 01.
	// Physically this is frame 9, but semantic idx[2] must be 3.
	write_frame(9, 2, 2, 3, 159);
	write_frame(10, 2, 2, 4, 160);
	write_frame(11, 2, 2, 5, 161);
	write_frame(12, 2, 2, 6, 162);
	write_frame(13, 2, 2, 7, 163);

	cdrom_file cd(&chd);

	const cdrom_file::toc &after = cd.get_toc();

	REQUIRE(after.tracks[1].pregap == track2_pregap);
	REQUIRE(after.tracks[1].pgdatasize != 0);
	REQUIRE(after.tracks[1].physframeofs == track1_frames);

	// Physical transition: frame 9.
	// Track 2 INDEX 01: physical frame 6.
	// Therefore semantic INDEX 02 offset must be 3.
	REQUIRE(after.tracks[1].idx[2] == 3);

	// Track 1 must remain untouched.
	REQUIRE(after.tracks[0].idx[2] == -1);

	// Runtime lookup agrees across the Track 2 INDEX 01/02 boundary.
	REQUIRE(cd.get_track_index(8) == 1);
	REQUIRE(cd.get_track_index(9) == 2);
	REQUIRE(cd.get_track_index(10) == 2);

	chd.close();
	std::filesystem::remove_all(tempdir);
}

TEST_CASE("CD CHD reconstructs one-frame monotonic index from stored Q", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-q-one-frame-index-test";
	const std::filesystem::path chdpath = tempdir / "one-frame-index.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t frame_count = 6;
	constexpr uint32_t hunk_bytes =
			cdrom_file::FRAME_SIZE * cdrom_file::FRAMES_PER_HUNK;

	const chd_codec_type compression[4] =
	{
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE,
		CHD_CODEC_NONE
	};

	chd_file chd;

	REQUIRE_FALSE(chd.create(
			chdpath.string(),
			uint64_t(frame_count) * cdrom_file::FRAME_SIZE,
			hunk_bytes,
			cdrom_file::FRAME_SIZE,
			compression));

	cdrom_file::toc toc{};
	toc.numtrks = 1;
	toc.numsessions = 1;

	cdrom_file::track_info &track = toc.tracks[0];
	track.trktype = cdrom_file::CD_TRACK_AUDIO;
	track.subtype = cdrom_file::CD_SUB_RAW;
	track.datasize = cdrom_file::MAX_SECTOR_DATA;
	track.subsize = cdrom_file::MAX_SUBCODE_DATA;
	track.frames = frame_count;
	track.pregap = 0;
	track.postgap = 0;
	track.pgtype = cdrom_file::CD_TRACK_AUDIO;
	track.pgsub = cdrom_file::CD_SUB_NONE;
	track.pgdatasize = 0;
	track.pgsubsize = 0;
	track.control_flags = 0;
	track.session = 0;

	std::fill(std::begin(track.idx), std::end(track.idx), -1);

	REQUIRE_FALSE(cdrom_file::write_metadata(&chd, toc));

	std::vector<uint8_t> frame(cdrom_file::FRAME_SIZE, 0);

	auto write_frame =
			[&chd, &frame](uint32_t framenum, uint8_t index)
			{
				std::fill(frame.begin(), frame.end(), 0);

				make_position_q(
						index,
						framenum,
						150 + framenum,
						frame.data() + cdrom_file::MAX_SECTOR_DATA);

				REQUIRE_FALSE(chd.write_units(framenum, frame.data()));
			};

	write_frame(0, 1);
	write_frame(1, 1);

	// INDEX 02 exists for exactly one sector, followed by a forward
	// transition to INDEX 03.
	write_frame(2, 2);
	write_frame(3, 3);
	write_frame(4, 3);
	write_frame(5, 3);

	cdrom_file cd(&chd);

	const cdrom_file::toc &result = cd.get_toc();

	REQUIRE(result.tracks[0].idx[2] == 2);
	REQUIRE(result.tracks[0].idx[3] == 3);

	REQUIRE(cd.get_track_index(1) == 1);
	REQUIRE(cd.get_track_index(2) == 2);
	REQUIRE(cd.get_track_index(3) == 3);

	chd.close();
	std::filesystem::remove_all(tempdir);
}
