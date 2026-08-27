// license:BSD-3-Clause
// copyright-holders:Aaron Giles,R. Belmont
/***************************************************************************

    cdrom.h

    Generic MAME cd-rom implementation

***************************************************************************/
#ifndef MAME_LIB_UTIL_CDROM_H
#define MAME_LIB_UTIL_CDROM_H

#pragma once

#include "chd.h"
#include "ioprocs.h"
#include "osdcore.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>


class cdrom_file {
public:
	// tracks are padded to a multiple of this many frames
	static constexpr uint32_t TRACK_PADDING    = 4;

	static constexpr uint32_t MAX_TRACKS       = 99;        /* AFAIK the theoretical limit */
	static constexpr uint32_t MAX_SECTOR_DATA  = 2352;
	static constexpr uint32_t MAX_SUBCODE_DATA = 96;
	static constexpr uint32_t MAX_INDEX        = 99;

	static constexpr uint32_t FRAME_SIZE       = MAX_SECTOR_DATA + MAX_SUBCODE_DATA;
	static constexpr uint32_t FRAMES_PER_HUNK  = 8;

	static constexpr uint32_t METADATA_WORDS   = 1 + MAX_TRACKS * 6;

	static constexpr uint32_t GDI_HIGH_DENSITY_AREA = 45000;

	enum
	{
		CD_TRACK_MODE1 = 0,         /* mode 1 2048 bytes/sector */
		CD_TRACK_MODE1_RAW,         /* mode 1 2352 bytes/sector */
		CD_TRACK_MODE2,             /* mode 2 2336 bytes/sector */
		CD_TRACK_MODE2_FORM1,       /* mode 2 2048 bytes/sector */
		CD_TRACK_MODE2_FORM2,       /* mode 2 2324 bytes/sector */
		CD_TRACK_MODE2_FORM_MIX,    /* mode 2 2336 bytes/sector */
		CD_TRACK_MODE2_RAW,         /* mode 2 2352 bytes/sector */
		CD_TRACK_AUDIO,             /* redbook audio track 2352 bytes/sector (588 samples) */

		CD_TRACK_RAW_DONTCARE       /* special flag for cdrom_read_data: just return me whatever is there */
	};

	enum
	{
		CD_SUB_NORMAL = 0,          /* "cooked" 96 bytes per sector */
		CD_SUB_RAW,                 /* raw uninterleaved 96 bytes per sector */
		CD_SUB_NONE                 /* no subcode data stored */
	};

	enum
	{
		CD_FLAG_GDROM        = 0x00000001,  // disc is a GD-ROM, all tracks should be stored with GD-ROM metadata
		CD_FLAG_GDROMLE      = 0x00000002,  // legacy GD-ROM, with little-endian CDDA data
		CD_FLAG_MULTISESSION = 0x00000004,  // multisession CD-ROM
	};

	enum
	{
		CD_FLAG_CONTROL_PREEMPHASIS = 1,
		CD_FLAG_CONTROL_DIGITAL_COPY_PERMITTED = 2,
		CD_FLAG_CONTROL_DATA_TRACK = 4,
		CD_FLAG_CONTROL_4CH = 8,
	};

	enum
	{
		CD_FLAG_ADR_START_TIME = 1,
		CD_FLAG_ADR_CATALOG_CODE,
		CD_FLAG_ADR_ISRC_CODE,
	};

	struct sector_position
	{
		int64_t frame;
	};

	struct channel_position
	{
		int64_t frame;
		uint8_t byte_offset;
	};

	struct subcode_position
	{
		int64_t frame;
	};

	struct disc_position
	{
		int32_t frame;
	};

	struct captured_position
	{
		std::optional<sector_position> sector_data;
		std::optional<channel_position> main_channel;
		std::optional<subcode_position> subcode;
	};

	struct backing_span
	{
		disc_position start;
		std::optional<uint32_t> frames;
		captured_position captured;
	};

        // CDM1 canonical CD metadata schema.
        static constexpr uint16_t CDM1_VERSION_MAJOR = 1;
        static constexpr uint16_t CDM1_VERSION_MINOR = 0;

        enum class cdm1_section : uint32_t
        {
                disc     = 0x44495343, // DISC
                sessions = 0x53455353, // SESS
                tracks   = 0x5452414b, // TRAK
                indexes  = 0x494e4458, // INDX
                regions  = 0x5245474e, // REGN
                evidence = 0x45564944, // EVID
                mappings = 0x4d415053  // MAPS
        };

        enum class cdm1_section_flag : uint16_t
        {
                required = 0x0001
        };

                enum class cdm1_region_owner : uint16_t
        {
                session = 1,
                track = 2
        };

        enum class cdm1_region_kind : uint16_t
        {
                lead_in = 1,
                pregap = 2,
                program = 3,
                postgap = 4,
                lead_out = 5
        };

        enum class cdm1_region_flag : uint16_t
        {
                length_known = 0x0001
        };

        enum class cdm1_track_type : uint16_t
        {
                unknown = 0,
                mode1 = 1,
                mode1_raw = 2,
                mode2 = 3,
                mode2_form1 = 4,
                mode2_form2 = 5,
                mode2_form_mix = 6,
                mode2_raw = 7,
                audio = 8
        };

        enum class cdm1_evidence_class : uint16_t
        {
                decoded_main = 1,
                raw_pw = 2
        };

        enum class cdm1_storage_class : uint16_t
        {
                chdv5_unit_slice = 1
        };

        enum class cdm1_coordinate_class : uint16_t
        {
                decoded_frame = 1
        };

        enum class cdm1_mapping_class : uint16_t
        {
                linear_interval = 1
        };

        enum class cdm1_provenance : uint16_t
        {
                unknown = 0,
                captured = 1,
                derived = 2,
                generated = 3
        };

        enum class cdm1_semantic_source : uint16_t
        {
                unknown = 0,
                explicit_fallback = 1,
                derived_capture = 2
        };

        static constexpr uint32_t CDM1_MAGIC = 0x43444d31; // CDM1

        static constexpr uint32_t CDM1_HEADER_BYTES = 40;
        static constexpr uint32_t CDM1_SECTION_ENTRY_BYTES = 24;
        static constexpr uint32_t CDM1_TABLE_HEADER_BYTES = 16;

        static constexpr uint32_t CDM1_DISC_RECORD_BYTES = 32;
        static constexpr uint32_t CDM1_SESSION_RECORD_BYTES = 32;
        static constexpr uint32_t CDM1_TRACK_RECORD_BYTES = 24;
        static constexpr uint32_t CDM1_INDEX_RECORD_BYTES = 24;
        static constexpr uint32_t CDM1_REGION_RECORD_BYTES = 40;
        static constexpr uint32_t CDM1_EVIDENCE_RECORD_BYTES = 48;
        static constexpr uint32_t CDM1_MAPPING_RECORD_BYTES = 48;

	enum class region_kind
	{
		lead_in,
		pregap,
		program,
		postgap,
		lead_out
	};

	enum class region_presence
	{
		unknown,
		generated,
		captured
	};

	struct region
	{
	    region_kind kind;
	    disc_position start;
	    std::optional<uint32_t> frames;
	    region_presence main_data;
	    region_presence subcode;
		std::vector<backing_span> backing;
	};

	struct index
	{
		uint8_t number;
		disc_position start;
	};

	struct disc_track
	{
		uint8_t number;
		uint8_t session;
		uint32_t type;
		uint32_t control_flags;

		std::vector<index> indexes;
		std::vector<region> regions;
	};

	struct disc_session
	{
		uint8_t number;
		uint8_t first_track;
		uint8_t last_track;

		disc_position program_start;

		std::optional<region> lead_in;
		std::optional<region> lead_out;
	};

	struct disc
	{
		std::vector<disc_session> sessions;
		std::vector<disc_track> tracks;
	};

	struct track_info
	{
		/* fields used by CHDMAN and in MAME */
		uint32_t trktype;       /* track type */
		uint32_t subtype;       /* subcode data type */
		uint32_t datasize;      /* size of data in each sector of this track */
		uint32_t subsize;       /* size of subchannel data in each sector of this track */
		uint32_t frames;        /* number of frames in this track */
		uint32_t extraframes;   /* number of "spillage" frames in this track */
		uint32_t pregap;        /* number of pregap frames */
		uint32_t postgap;       /* number of postgap frames */
		uint32_t pgtype;        /* type of sectors in pregap */
		uint32_t pgsub;         /* type of subchannel data in pregap */
		uint32_t pgdatasize;    /* size of data in each sector of the pregap */
		uint32_t pgsubsize;     /* size of subchannel data in each sector of the pregap */
		uint32_t control_flags; /* metadata flags associated with each track */
		uint32_t session;       /* session number */
		int32_t idx[MAX_INDEX + 1]; /* index positions relative to the track */

		/* fields used in CHDMAN only */
		uint32_t padframes;   /* number of frames of padding to add to the end of the track; needed for GDI */
		uint32_t splitframes; /* number of frames from the next file to add to the end of the current track after padding; needed for Redump split-bin GDI */

		/* fields used in MAME/MESS only */
		uint32_t logframeofs; /* logical frame of actual track data - offset by pregap size if pregap not physically present */
		uint32_t physframeofs; /* physical frame of actual track data in CHD data */
		uint32_t chdframeofs; /* frame number this track starts at on the CHD */
		uint32_t logframes; /* number of frames from logframeofs until end of track data */

		/* fields used in multi-cue GDI */
		uint32_t multicuearea;
	};


	struct toc
	{
		uint32_t numtrks;     /* number of tracks */
		uint32_t numsessions; /* number of sessions */
		uint32_t flags;       /* see FLAG_ above */
		track_info tracks[MAX_TRACKS + 1];
	};

	struct track_input_entry
	{
		track_input_entry() { reset(); }
		void reset() { fname.clear(); offset = 0; leadin = leadout = -1; swap = false; std::fill(std::begin(idx), std::end(idx), -1); }

		std::string fname;      // filename for each track
		uint32_t offset;      // offset in the data file for each track
		bool swap;          // data needs to be byte swapped
		int32_t idx[MAX_INDEX + 1];
		int32_t leadin, leadout; // TODO: these should probably be their own tracks entirely
	};

	struct track_input_info
	{
		void reset() { for (auto & elem : track) elem.reset(); }

		track_input_entry track[MAX_TRACKS];
	};

	enum class q_type
	{
		invalid,
		position,
		lead_in_toc,
		lead_out,
		catalog,
		isrc,
		unknown
	};

	struct q_position
	{
		uint8_t adr_control;
		uint8_t track;
		uint8_t index;
		uint32_t relative_frame;
		uint32_t absolute_frame;
	};

	struct q_catalog
	{
		uint8_t adr_control;
		std::array<char, 13> number;
		uint8_t absolute_frame;
	};

	struct q_isrc
	{
		uint8_t adr_control;
		std::array<char, 12> code;
		uint8_t absolute_frame;
	};

	struct q_toc
	{
	uint8_t adr_control;
	uint8_t point;
	uint8_t minute;
	uint8_t second;
	uint8_t frame;
	};

	enum class q_toc_kind
	{
		track,
		first_track,
		last_track,
		lead_out,
		special
	};

	struct q_toc_semantics
	{
		uint8_t adr_control;
		q_toc_kind kind;
		uint8_t point;

		std::optional<uint8_t> track;
		std::optional<uint32_t> start_frame;
		std::optional<uint8_t> disc_type;
	};

	struct q_toc_accumulator
	{
		std::optional<uint8_t> first_track;
		std::optional<uint8_t> last_track;
		std::optional<uint32_t> lead_out_start_frame;
		std::optional<uint8_t> disc_type;
		std::vector<q_toc_semantics> tracks;
		bool conflict = false;

		bool complete() const;
	};

	cdrom_file(chd_file *chd);
	cdrom_file(std::string_view inputfile);
	~cdrom_file();


	/* core read access */
	bool read_data(uint32_t lbasector, void *buffer, uint32_t datatype, bool phys=false);
	bool read_subcode(uint32_t lbasector, void *buffer, bool phys=false);
	bool get_subcode_q(uint32_t lbasector, uint8_t *buffer, bool phys=false) const;
	bool get_subcode_raw(uint32_t lbasector, uint8_t *buffer, bool phys=false) const;

	static q_type classify_subcode_q(const uint8_t *q);
	static void encode_subcode_q(const q_position &position, uint8_t *buffer);
	static bool decode_subcode_q(const uint8_t *q, q_position &position);
	static bool decode_subcode_q_catalog(
			const uint8_t *q,
			q_catalog &catalog);
	static bool decode_subcode_q_isrc(
			const uint8_t *q,
			q_isrc &isrc);
	static bool decode_subcode_q_toc(const uint8_t *q, q_toc &toc);
	static bool interpret_subcode_q_toc(
			const q_toc &toc,
			q_toc_semantics &semantics);
	static bool accumulate_subcode_q_toc(
			const uint8_t *q,
			q_toc_accumulator &accumulator);
	static bool accumulate_q_toc_semantics(
			const q_toc_semantics &semantics,
			q_toc_accumulator &accumulator);
	static bool apply_q_toc_accumulator(
			const q_toc_accumulator &accumulator,
			disc &disc,
			uint8_t session_number);
	static bool apply_q_toc_semantics(
			const q_toc_semantics &semantics,
			disc &disc,
			uint8_t session_number);
	static void pack_subcode_q(const uint8_t *q, uint8_t *subcode);
	static void unpack_subcode_q(const uint8_t *subcode, uint8_t *q);
	static bool make_subcode_q_position(
			const disc_track &track,
			disc_position position,
			int64_t track_frame,
			int64_t absolute_frame,
			q_position &q);
	static bool make_subcode_q_position(
			const disc &disc,
			disc_position position,
			q_position &q);
	static bool make_subcode_q_lead_out(
			const disc_track &track,
			const region &lead_out,
			disc_position position,
			int64_t absolute_frame,
			uint8_t *q);

	/* handy utilities */
	uint32_t get_track(uint32_t frame) const;
	uint32_t get_track_start(uint32_t track) const;
	uint32_t get_track_start_phys(uint32_t track) const { return cdtoc.tracks[track == 0xaa ? cdtoc.numtrks : track].physframeofs; }
	uint32_t get_track_index(uint32_t frame) const;

	/* TOC utilities */
	static std::error_condition parse_nero(std::string_view tocfname, toc &outtoc, track_input_info &outinfo);
	static std::error_condition parse_iso(std::string_view tocfname, toc &outtoc, track_input_info &outinfo);
	static std::error_condition parse_gdi(std::string_view tocfname, toc &outtoc, track_input_info &outinfo);
	static std::error_condition parse_cue(std::string_view tocfname, toc &outtoc, track_input_info &outinfo);
	static std::error_condition adjust_high_density_area(toc& outtoc, track_input_info& outinfo);
	static bool is_gdicue(std::string_view tocfname);
	static std::error_condition parse_toc(std::string_view tocfname, toc &outtoc, track_input_info &outinfo);
	int get_last_session() const { return cdtoc.numsessions ? cdtoc.numsessions : 1; }
	int get_last_track() const { return cdtoc.numtrks; }
	int get_adr_control(int track) const
	{
		if (track == 0xaa)
			track = get_last_track() - 1; // use last track's flags
		int adrctl = (CD_FLAG_ADR_START_TIME << 4) | (cdtoc.tracks[track].control_flags & 0x0f);
		if (cdtoc.tracks[track].trktype != CD_TRACK_AUDIO)
			adrctl |= CD_FLAG_CONTROL_DATA_TRACK;
		return adrctl;
	}
	int get_track_type(int track) const { return cdtoc.tracks[track].trktype; }
	const toc &get_toc() const { return cdtoc; }

	const disc &get_disc() const;
	disc build_disc() const;

	static std::error_condition validate_cdm1_metadata(
			const std::vector<uint8_t> &metadata);

	static const disc_track *find_track(
			const disc &disc,
			disc_position position);
	static std::optional<sector_position> backing_sector_position(
			const disc &disc,
			disc_position position);
	static std::optional<disc_position> disc_position_from_sector_position(
			const disc &disc,
			sector_position position);
	static std::optional<channel_position> backing_channel_position(
			const disc &disc,
			disc_position position);
	static std::optional<subcode_position> backing_subcode_position(
			const disc &disc,
			disc_position position);
	static const region *find_region(
			const disc_track &track,
			disc_position position);
	static std::optional<sector_position> backing_sector_position(
			const disc_track &track,
			disc_position position);
	static std::optional<channel_position> backing_channel_position(
			const disc_track &track,
			disc_position position);
	static std::optional<subcode_position> backing_subcode_position(
			const disc_track &track,
			disc_position position);
	static const backing_span *find_backing_span(
			const region &region,
			disc_position position);
	static std::optional<sector_position> backing_sector_position(
			const region &region,
			disc_position position);
	static std::optional<channel_position> backing_channel_position(
			const region &region,
			disc_position position);
	static std::optional<subcode_position> backing_subcode_position(
			const region &region,
			disc_position position);
	static bool validate_backing_spans(const region &region);
	void reconstruct_track_indexes();

	/* extra utilities */
	static void convert_type_string_to_track_info(const char *typestring, track_info *info);
	static void convert_type_string_to_pregap_info(const char *typestring, track_info *info);
	static void convert_subtype_string_to_track_info(const char *typestring, track_info *info);
	static void convert_subtype_string_to_pregap_info(const char *typestring, track_info *info);
	static const char *get_type_string(uint32_t trktype);
	static const char *get_subtype_string(uint32_t subtype);
	static std::error_condition parse_metadata(chd_file *chd, toc &toc);
	static std::error_condition write_metadata(chd_file *chd, const toc &toc);
	bool is_gdrom() const { return cdtoc.flags & (CD_FLAG_GDROM|CD_FLAG_GDROMLE); }

	// ECC utilities
	static bool ecc_verify(const uint8_t *sector);
	static void ecc_generate(uint8_t *sector);
	static void ecc_clear(uint8_t *sector);



	static inline uint32_t msf_to_lba(uint32_t msf)
	{
		return ( ((msf&0x00ff0000)>>16) * 60 * 75) + (((msf&0x0000ff00)>>8) * 75) + ((msf&0x000000ff)>>0);
	}

	static inline uint32_t lba_to_msf(uint32_t lba)
	{
		uint8_t m, s, f;

		m = lba / (60 * 75);
		lba -= m * (60 * 75);
		s = lba / 75;
		f = lba % 75;

		return ((m / 10) << 20) | ((m % 10) << 16) |
			((s / 10) << 12) | ((s % 10) <<  8) |
			((f / 10) <<  4) | ((f % 10) <<  0);
	}

	// segacd needs it like this.. investigate
	// Angelo also says PCE tracks often start playing at the
	// wrong address.. related?
	static inline uint32_t lba_to_msf_alt(int lba)
	{
		uint32_t ret = 0;

		ret |= ((lba / (60 * 75))&0xff)<<16;
		ret |= (((lba / 75) % 60)&0xff)<<8;
		ret |= ((lba % 75)&0xff)<<0;

		return ret;
	}

private:
	enum gdi_area {
		SINGLE_DENSITY,
		HIGH_DENSITY
	};

	enum gdi_pattern {
		TYPE_UNKNOWN = 0,
		TYPE_I,
		TYPE_II,
		TYPE_III,
		TYPE_III_SPLIT
	};

	/** @brief  offset within sector. */
	static constexpr int SYNC_OFFSET = 0x000;
	/** @brief  12 bytes. */
	static constexpr int SYNC_NUM_BYTES = 12;

	/** @brief  offset within sector. */
	static constexpr int MODE_OFFSET = 0x00f;

	/** @brief  offset within sector. */
	static constexpr int ECC_P_OFFSET = 0x81c;
	/** @brief  2 lots of 86. */
	static constexpr int ECC_P_NUM_BYTES = 86;
	/** @brief  24 bytes each. */
	static constexpr int ECC_P_COMP = 24;

	/** @brief  The ECC q offset. */
	static constexpr int ECC_Q_OFFSET = ECC_P_OFFSET + 2 * ECC_P_NUM_BYTES;
	/** @brief  2 lots of 52. */
	static constexpr int ECC_Q_NUM_BYTES = 52;
	/** @brief  43 bytes each. */
	static constexpr int ECC_Q_COMP = 43;

	// ECC tables
	static const uint8_t ecclow[256];
	static const uint8_t ecchigh[256];
	static const uint16_t poffsets[ECC_P_NUM_BYTES][ECC_P_COMP];
	static const uint16_t qoffsets[ECC_Q_NUM_BYTES][ECC_Q_COMP];

	/** @brief  The chd. */
	chd_file *           chd;                /* CHD file */
	/** @brief  The cdtoc. */
	toc                  cdtoc;              /* TOC for the CD */
	/** @brief  The canonical disc model. */
	disc                 m_disc;             /* canonical disc model */
	/** @brief  Information describing the track. */
	track_input_info     cdtrack_info;       /* track info */
	/** @brief  The fhandle[ CD maximum tracks]. */
	util::random_read::ptr fhandle[MAX_TRACKS];/* file handle */

	inline uint32_t physical_to_chd_lba(
		uint32_t physlba,
		uint32_t tracknum) const;
	static uint16_t subcode_q_crc(const uint8_t *data);

	static void reset_toc(toc &toc);
	static void get_info_from_type_string(const char *typestring, uint32_t *trktype, uint32_t *datasize);
	static uint8_t ecc_source_byte(const uint8_t *sector, uint32_t offset);
	static void ecc_compute_bytes(const uint8_t *sector, const uint16_t *row, int rowlen, uint8_t &val1, uint8_t &val2);
	std::error_condition read_partial_sector(void *dest, uint32_t chdsector, uint32_t tracknum, uint32_t startoffs, uint32_t length);

	static std::string get_file_path(std::string &path);
	static uint64_t get_file_size(std::string_view filename);
	static int tokenize(const char *linebuffer, int i, int linebuffersize, char *token, int tokensize);
	static int msf_to_frames(const char *token);
	static uint32_t parse_wav_sample(std::string_view filename, uint32_t *dataoffs);
	static uint16_t read_uint16(FILE *infile);
	static uint32_t read_uint32(FILE *infile);
	static uint64_t read_uint64(FILE *infile);
};

#endif // MAME_LIB_UTIL_CDROM_H
