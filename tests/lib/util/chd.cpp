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


TEST_CASE("CD CHD reconstructs track indexes from stored Q", "[util][chd][cdrom]")
{
	const std::filesystem::path tempdir =
			std::filesystem::temp_directory_path() / "mame-chd-cd-q-index-test";
	const std::filesystem::path chdpath = tempdir / "indexes.chd";

	std::filesystem::remove_all(tempdir);
	std::filesystem::create_directories(tempdir);

	constexpr uint32_t frame_count = 17;
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

	// A valid position-Q frame for the wrong track must also interrupt a
	// pending transition.
	write_frame(14, 4);
	write_frame(15, 4, false, 2);
	write_frame(16, 4);

	// The CHD constructor must derive its TOC from CHD metadata rather than
	// from the source structure above.
	cdrom_file cd(&chd);

	const cdrom_file::toc &before = cd.get_toc();

	REQUIRE(before.tracks[0].idx[2] == -1);
	REQUIRE(before.tracks[0].idx[3] == -1);
	REQUIRE(before.tracks[0].idx[4] == -1);

	cd.reconstruct_track_indexes();

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
	REQUIRE(cd.get_track_index(14) == 4);
	REQUIRE(cd.get_track_index(16) == 4);

	// Wrong-track Q is rejected for semantic lookup, so this sector falls back
	// to the reconstructed semantic table.
	REQUIRE(cd.get_track_index(15) == 3);

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

	REQUIRE(cd.get_toc().tracks[0].idx[2] == -1);

	cd.reconstruct_track_indexes();

	// The physical transition occurs at frame 5, but idx[] coordinates are
	// relative to INDEX 01 at physical frame 2.
	REQUIRE(cd.get_toc().tracks[0].idx[2] == 3);

	chd.close();
	std::filesystem::remove_all(tempdir);
}
